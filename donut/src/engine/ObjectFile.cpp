#ifdef DONUT_WITH_ASSIMP
    #include "assimp/scene.h"
    #include "assimp/cimport.h"
    #include "assimp/cfileio.h"
    #include "assimp/postprocess.h"
#endif // DONUT_WITH_ASSIMP

#include <donut/engine/ObjectFile.h>
#include <donut/engine/TextureCache.h>
#include <donut/core/chunk/chunk.h>
#include <donut/core/chunk/chunkFile.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/json.h>
#include <donut/core/log.h>
#include <json/writer.h>

#include <fstream>

using namespace donut::math;
#include <donut/shaders/material_cb.h>

using namespace donut::vfs;

namespace donut::engine
{

    void UpdateMaterialConstantBuffer(nvrhi::ICommandList* commandList, const Material* material, nvrhi::IBuffer* buffer)
    {
        MaterialConstants materialConstants = {};
        materialConstants.useDiffuseTexture = material->diffuseTexture ? 1 : 0;
        materialConstants.specularTextureType = material->specularTextureType;
        materialConstants.useEmissiveTexture = material->emissiveTexture ? 1 : 0;
        materialConstants.useNormalsTexture = material->normalsTexture ? 1 : 0;
        materialConstants.diffuseColor = material->diffuseColor;
        materialConstants.specularColor = material->specularColor;
        materialConstants.emissiveColor = material->emissiveColor;
        materialConstants.roughness = 1.0f - material->shininess;
        materialConstants.opacity = material->opacity;
        materialConstants.materialID = material->materialID;

        commandList->writeBuffer(buffer, &materialConstants, sizeof(materialConstants));
    }

    void SetJsonFloat3(float3 value, Json::Value& node)
    {
        node.append(Json::Value(value.x));
        node.append(Json::Value(value.y));
        node.append(Json::Value(value.z));
    }

    bool SaveMaterialsAsJson(const char* jsonFileName, const std::vector<Material*> materials)
    {
        Json::Value root(Json::objectValue);

        for (auto material : materials)
        {
            Json::Value& materialNode = root[material->name];

            SetJsonFloat3(material->diffuseColor, materialNode["Diffuse"]);
            SetJsonFloat3(material->specularColor, materialNode["Specular"]);
            SetJsonFloat3(material->emissiveColor, materialNode["Emittance"]);

            materialNode["Shininess"] = Json::Value(material->shininess);
            materialNode["Opacity"] = Json::Value(material->opacity);

            Json::Value& texturesNode = materialNode["Textures"];
            if (material->diffuseTexture && material->diffuseTexture->texture)
                texturesNode["Diffuse"] = Json::Value(material->diffuseTexture->relativePath);
            if (material->specularTexture && material->specularTexture->texture)
                texturesNode["Specular"] = Json::Value(material->specularTexture->relativePath);
            if (material->emissiveTexture && material->emissiveTexture->texture)
                texturesNode["Emittance"] = Json::Value(material->emissiveTexture->relativePath);
            if (material->normalsTexture && material->normalsTexture->texture)
                texturesNode["Bumpmap"] = Json::Value(material->normalsTexture->relativePath);
        }

        std::ofstream file(jsonFileName);
        if (!file.is_open())
            return false;

        file << root << std::endl;

        file.close();

        return true;
    }
}

using namespace donut::engine;

ObjectFile::ObjectFile(std::shared_ptr<IFileSystem> fs)
    : m_AttributeMask(VertexAttribute::ALL)
    , m_fs(fs)
{
}

ObjectFile::~ObjectFile()
{
    // not using shared_ptr's in these array because of performance concerns
    for (auto instance : m_MeshInstances)
        delete instance;
    for (auto mesh : m_Meshes)
        delete mesh;
    for (auto material : m_Materials)
        delete material;
}

bool ObjectFile::Load(const std::filesystem::path& fileName,
                      const std::filesystem::path& materialFileName,
                      VertexAttribute::Enum attributeMask,
                      TextureCache& textureCache,
                      SceneLoadingStats& stats,
                      concurrency::task_group& taskGroup,
                      ObjectLoadFlags::Enum _flags)
{
    m_ScenePath = fileName.parent_path();
    m_AttributeMask = attributeMask;
    m_LoadFlags = _flags;

    bool useMaterialFile = !materialFileName.empty();
    if (useMaterialFile)
    {
        // Start loading materials as early as possible.
        LoadMaterialsFromJson(materialFileName, textureCache, stats, taskGroup);
    }

    if (fileName.extension() == ".chk")
    {
        std::shared_ptr<IBlob> objectFileData = m_fs->readFile(fileName);
        if (!objectFileData)
        {
            log::error("Couldn't read file `%s`", fileName.generic_string().c_str());
            return false;
        }

        return LoadChunkObjectFile(objectFileData, fileName);
    }
#ifdef DONUT_WITH_ASSIMP
    else
        return LoadAssimpObjectFile(fileName, useMaterialFile, textureCache, stats, taskGroup);
#else
    else
    {
        log::error("unsupported file type (%s)", fileName.generic_string().c_str());
        return false;
    }
#endif // DONUT_WITH_ASSIMP
}

bool ObjectFile::LoadChunkObjectFile(std::weak_ptr<const IBlob> data, const std::filesystem::path& fileName)
{
    std::shared_ptr<const chunk::MeshSetBase> chunkFile = chunk::deserialize(data, fileName.generic_string().c_str());

    if (!chunkFile)
        return false;

    if (chunkFile->type != chunk::MeshSetBase::MESH)
    {
        log::error("unsupported chunk object type (%d)", chunkFile->type);
        return false;
    }

    m_ChunkMeshSet = std::static_pointer_cast<const chunk::MeshSet>(chunkFile);

    // Materials have already been initialized from json, just copy them to the m_Materials array
    for (auto it : m_MaterialsByName)
    {
        m_Materials.push_back(it.second);
    }

    // Create the MeshInfo objects and fill the m_Meshes array
    for (uint meshID = 0; meshID < m_ChunkMeshSet->nmeshInfos; meshID++)
    {
        const chunk::MeshInfo& minfo = m_ChunkMeshSet->meshInfos[meshID];
        MeshInfo* mesh = CreateMeshInfo();

        mesh->buffers = &m_Buffers;
        mesh->name = minfo.name;
        mesh->numIndices = minfo.numIndices;
        mesh->numVertices = minfo.numVertices;
        mesh->indexOffset = minfo.firstIndex;
        mesh->vertexOffset = minfo.firstVertex;
        mesh->objectSpaceBounds = minfo.bbox;
        mesh->material = m_MaterialsByName[minfo.materialName];

        m_Meshes.push_back(mesh);
    }

    // Create the MeshInstance objects and fill the m_MeshInstances array
    for (size_t instanceID = 0; instanceID < m_ObjectInstanceTransforms.size(); instanceID++)
    {
        const affine3& objectInstanceTransform = m_ObjectInstanceTransforms[instanceID];

        for (uint instanceID = 0; instanceID < m_ChunkMeshSet->ninstances; instanceID++)
        {
            const chunk::MeshInstance& minst = m_ChunkMeshSet->instances[instanceID];
            MeshInstance* instance = CreateMeshInstance();

            instance->mesh = m_Meshes[minst.minfoId];
            instance->localTransform = minst.transform * objectInstanceTransform;
            instance->previousTransform = instance->localTransform;
            instance->transformedBounds = minst.bbox;
            instance->transformedCenter = minst.center;

            if (m_ObjectInstanceTransforms.size() == 1)
                instance->name = minst.name;
            else
                instance->name = minst.name + std::to_string(instanceID);

            m_MeshInstances.push_back(instance);
        }
    }

    UpdateMeshBounds();

    return true;
}

#ifdef DONUT_WITH_ASSIMP
struct VfsFile : public aiFile
{
    std::shared_ptr<donut::vfs::IBlob> blob;
    size_t ptr = 0;
    size_t size = 0;

    VfsFile(std::shared_ptr<donut::vfs::IBlob> _blob)
        : blob(_blob)
    {
        size = blob->size();
        ReadProc = FileRead;
        WriteProc = FileWrite;
        TellProc = FileTell;
        FileSizeProc = FileSize;
        SeekProc = FileSeek;
        FlushProc = FileFlush;
    }

    static size_t FileWrite(aiFile* file, const char* data, size_t size, size_t count)
    {
        return 0;
    }

    static size_t FileRead(aiFile* file, char* data, size_t size, size_t count)
    {
        VfsFile* _this = static_cast<VfsFile*>(file);
        size_t avail = (_this->size - _this->ptr) / size;
        size_t actual = std::min(avail, count);
        if (actual > 0)
        {
            memcpy(data, static_cast<const char*>(_this->blob->data()) + _this->ptr, actual * size);
            _this->ptr += actual * size;
        }
        return actual;
    }

    static size_t FileTell(aiFile* file)
    {
        VfsFile* _this = static_cast<VfsFile*>(file);
        return _this->ptr;
    }

    static size_t FileSize(aiFile* file)
    {
        VfsFile* _this = static_cast<VfsFile*>(file);
        return _this->size;
    }

    static void FileFlush(aiFile* file)
    {
    }

    static aiReturn FileSeek(aiFile* file, size_t off, aiOrigin origin)
    {
        VfsFile* _this = static_cast<VfsFile*>(file);
        switch (origin)
        {
        case aiOrigin_SET:
            _this->ptr = std::min(off, _this->size);
            break;
        case aiOrigin_END:
            // offset is unsigned, so any seek is past the end of file
            _this->ptr = _this->size;
            break;
        case aiOrigin_CUR:
            _this->ptr = std::min(_this->ptr + off, _this->size);
            break;
        }

        return aiReturn_SUCCESS;
    }
};

struct VfsFileIO : public aiFileIO
{
    std::shared_ptr<donut::vfs::IFileSystem> m_fs;

    VfsFileIO(std::shared_ptr<donut::vfs::IFileSystem> fs)
        : m_fs(fs)
    {
        OpenProc = Open;
        CloseProc = Close;
        UserData = nullptr;
    }

    static aiFile* Open(aiFileIO* io, const char* name, const char* mode) 
    {
        VfsFileIO* _this = static_cast<VfsFileIO*>(io);

        if (strcmp(mode, "rb") == 0)
        {
            auto blob = _this->m_fs->readFile(name);
            if (blob)
            {
                return new VfsFile(blob);
            }
        }

        return nullptr;
    }

    static void Close(aiFileIO* io, aiFile* file)
    {
        if(file)
            delete file;
    }
};

bool ObjectFile::LoadAssimpObjectFile(const std::filesystem::path& fileName, bool useMaterialFile, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup)
{
    uint flags = 0;
    if (m_LoadFlags & ObjectLoadFlags::JOIN_IDENTICAL_VERTICES)
        flags |= aiProcess_JoinIdenticalVertices;
    if (m_LoadFlags & ObjectLoadFlags::IMPROVE_CACHE_LOCALITY)
        flags |= aiProcess_ImproveCacheLocality;
    if (m_LoadFlags & ObjectLoadFlags::TRIANGULATE)
        flags |= aiProcess_Triangulate;
    if (m_LoadFlags & ObjectLoadFlags::CALC_TANGENT_SPACE)
        flags |= aiProcess_CalcTangentSpace;
    if (m_LoadFlags & ObjectLoadFlags::GEN_NORMALS)
        flags |= aiProcess_GenNormals;
    if (m_LoadFlags & ObjectLoadFlags::FLIP_UVS)
        flags |= aiProcess_FlipUVs;

    VfsFileIO fileIO(m_fs);

    aiScene* pAssimpScene = (aiScene*)aiImportFileExWithProperties(
        fileName.generic_string().c_str(),
        flags,
        &fileIO,
        nullptr
    );

    if (!pAssimpScene)
    {
        log::error("Unable to load scene file `%s`: %s", fileName.generic_string().c_str(), aiGetErrorString());
        return false;
    }

    if (useMaterialFile)
    {
        ConnectMaterialsWithJson(pAssimpScene);
    }
    else
    {
        // Not as early as with the separate material file, but still early.
        LoadMaterialsFromAssimp(pAssimpScene, textureCache, stats, taskGroup);
    }

    InitMeshInfos(pAssimpScene);

    RecursivelyCollectMeshInstances(pAssimpScene, pAssimpScene->mRootNode, affine3::identity(), 0, "");

    aiReleaseImport(pAssimpScene);
    pAssimpScene = nullptr;

    UpdateMeshBounds();

    return true;
}

void ObjectFile::InitMeshInfos(aiScene* pAssimpScene)
{
    m_Meshes.resize(pAssimpScene->mNumMeshes);
    
    uint totalIndices = 0;
    uint totalVertices = 0;
    
    // Count indices and vertices, assign buffer positions, compute object-space bounds
    for (uint assimpMesh = 0; assimpMesh < pAssimpScene->mNumMeshes; assimpMesh++)
    {
        const aiMesh* pMesh = pAssimpScene->mMeshes[assimpMesh];
        MeshInfo* info = CreateMeshInfo();
        m_Meshes[assimpMesh] = info;

        info->objectSpaceBounds = box3(pMesh->mNumVertices, (float3*)pMesh->mVertices);
        info->buffers = &m_Buffers;
        info->indexOffset = totalIndices;
        info->vertexOffset = totalVertices;
        info->numIndices = pMesh->mNumFaces * 3;
        info->numVertices = pMesh->mNumVertices;
        info->material = m_Materials[pMesh->mMaterialIndex];
        info->name = pMesh->mName.C_Str();

        totalIndices += info->numIndices;
        totalVertices += info->numVertices;
    }

    // Create buffer images
    m_Indices.resize(totalIndices);
    if (m_AttributeMask & VertexAttribute::POSITION) m_AttrPosition.resize(totalVertices);
    if (m_AttributeMask & VertexAttribute::TEXCOORD1) m_AttrTexcoord1.resize(totalVertices);
    if (m_AttributeMask & VertexAttribute::TEXCOORD2) m_AttrTexcoord2.resize(totalVertices);
    if (m_AttributeMask & VertexAttribute::NORMAL) m_AttrNormal.resize(totalVertices);
    if (m_AttributeMask & VertexAttribute::TANGENT) m_AttrTangent.resize(totalVertices);
    if (m_AttributeMask & VertexAttribute::BITANGENT) m_AttrBitangent.resize(totalVertices);

    // Copy data into buffer images
    for (uint assimpMesh = 0; assimpMesh < pAssimpScene->mNumMeshes; assimpMesh++)
    {
        const aiMesh* pMesh = pAssimpScene->mMeshes[assimpMesh];
        MeshInfo* info = m_Meshes[assimpMesh];

        
        // Indices
        for (uint nFace = 0; nFace < pMesh->mNumFaces; ++nFace)
        {
            memcpy(&m_Indices[info->indexOffset + nFace * 3], pMesh->mFaces[nFace].mIndices, sizeof(int) * 3);
        }

        // Positions
        if (m_AttributeMask & VertexAttribute::POSITION)
        {
            memcpy(&m_AttrPosition[info->vertexOffset], pMesh->mVertices, sizeof(float3) * pMesh->mNumVertices);
        }

        // Other attributes require conversion
        for (uint nVertex = 0; nVertex < pMesh->mNumVertices; nVertex++)
        {
            uint vertexPos = info->vertexOffset + nVertex;

            if ((m_AttributeMask & VertexAttribute::TEXCOORD1) && pMesh->mTextureCoords[0])
                m_AttrTexcoord1[vertexPos] = *(float2*)&pMesh->mTextureCoords[0][nVertex];

            if ((m_AttributeMask & VertexAttribute::TEXCOORD2) && pMesh->mTextureCoords[1])
                m_AttrTexcoord2[vertexPos] = *(float2*)&pMesh->mTextureCoords[1][nVertex];

            if ((m_AttributeMask & VertexAttribute::NORMAL) && pMesh->mNormals)
                m_AttrNormal[vertexPos] = vectorToSnorm8((float3 &)pMesh->mNormals[nVertex]);

            if ((m_AttributeMask & VertexAttribute::TANGENT) && pMesh->mTangents)
                m_AttrTangent[vertexPos] = vectorToSnorm8((float3 &)pMesh->mTangents[nVertex]);

            if ((m_AttributeMask & VertexAttribute::BITANGENT) && pMesh->mBitangents)
                m_AttrBitangent[vertexPos] = vectorToSnorm8((float3 &)pMesh->mBitangents[nVertex]);
        }
    }
}

void ObjectFile::RecursivelyCollectMeshInstances(aiScene* pAssimpScene, const aiNode* pNode, const affine3& parentTransform, uint level, const std::string& prefix)
{
    affine3 nodeTransform = homogeneousToAffine(transpose(*(float4x4*)&pNode->mTransformation));

    affine3 localTransform = nodeTransform * parentTransform;

#if 0
    {
        std::stringstream ss;
        for (uint n = 0; n < level; n++)
            ss << "  ";
        ss << pNode->mName.C_Str() << "/" << pNode->mNumMeshes << "/" << pNode->mNumChildren;
        LOG("%s", ss.str().c_str());
    }
#endif

    std::string nodeName = (level > 0) ? prefix + pNode->mName.C_Str() : "";

    for (uint nMesh = 0; nMesh < pNode->mNumMeshes; nMesh++)
    {
        for(size_t instanceID = 0; instanceID < m_ObjectInstanceTransforms.size(); instanceID++)
        {
            const affine3& objectInstanceTransform = m_ObjectInstanceTransforms[instanceID];

            MeshInstance* instance = CreateMeshInstance();
            uint assimpMesh = pNode->mMeshes[nMesh];
            instance->localTransform = localTransform * objectInstanceTransform;
            instance->previousTransform = instance->localTransform;
            instance->mesh = m_Meshes[assimpMesh];

            if (m_ObjectInstanceTransforms.size() == 1)
                instance->name = nodeName;
            else
                instance->name = nodeName + std::to_string(instanceID);

            m_MeshInstances.push_back(instance);
        }
    }

    for (uint nChild = 0; nChild < pNode->mNumChildren; nChild++)
    {
        const aiNode* pChild = pNode->mChildren[nChild];
        RecursivelyCollectMeshInstances(pAssimpScene, pChild, localTransform, level + 1, (level > 0) ? nodeName + "/" : "");
    }
}

void ObjectFile::LoadMaterialsFromAssimp(aiScene* pAssimpScene, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup)
{	
    assert(pAssimpScene->HasMaterials());

	m_Materials.resize(pAssimpScene->mNumMaterials);

	for (uint materialIndex = 0; materialIndex < pAssimpScene->mNumMaterials; ++materialIndex)
	{
		aiString texturePath;
		aiMaterial* material = pAssimpScene->mMaterials[materialIndex];
		Material* sceneMaterial = CreateMaterial();
        m_Materials[materialIndex] = sceneMaterial;

        aiString materialName;
        if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS)
        {
            sceneMaterial->name = materialName.C_Str();
        }

		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS)
		{
            auto textureFilePath = m_ScenePath / std::filesystem::path(texturePath.C_Str());
			sceneMaterial->diffuseTexture = textureCache.LoadTextureFromFileAsync(textureFilePath, true, taskGroup);
		}

        if (material->GetTexture(aiTextureType_SPECULAR, 0, &texturePath) == AI_SUCCESS)
		{
            auto textureFilePath = m_ScenePath / std::filesystem::path(texturePath.C_Str());
            sceneMaterial->specularTexture = textureCache.LoadTextureFromFileAsync(textureFilePath, true, taskGroup);
		}

        if (material->GetTexture(aiTextureType_HEIGHT, 0, &texturePath) == AI_SUCCESS)
		{
            auto textureFilePath = m_ScenePath / std::filesystem::path(texturePath.C_Str());
            sceneMaterial->normalsTexture = textureCache.LoadTextureFromFileAsync(textureFilePath, false, taskGroup);
		}
        else if (material->GetTexture(aiTextureType_NORMALS, 0, &texturePath) == AI_SUCCESS)
        {
            auto textureFilePath = m_ScenePath / std::filesystem::path(texturePath.C_Str());
            sceneMaterial->normalsTexture = textureCache.LoadTextureFromFileAsync(textureFilePath, false, taskGroup);
        }

		if (material->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath) == AI_SUCCESS)
		{
            auto textureFilePath = m_ScenePath / std::filesystem::path(texturePath.C_Str());
            sceneMaterial->emissiveTexture = textureCache.LoadTextureFromFileAsync(textureFilePath, true, taskGroup);
		}

        aiColor3D color;

        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        {
            sceneMaterial->diffuseColor = float3(color.r, color.g, color.b);
        }

        if (sceneMaterial->diffuseTexture && all(sceneMaterial->diffuseColor == 0.f))
        {
            sceneMaterial->diffuseColor = 1.f;
        }

        if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
        {
            sceneMaterial->specularColor = float3(color.r, color.g, color.b);
        }

        if (sceneMaterial->specularTexture && all(sceneMaterial->specularColor == 0.f))
        {
            sceneMaterial->specularColor = 1.f;
        }

        float scalar;

        if (material->Get(AI_MATKEY_SHININESS, scalar) == AI_SUCCESS)
        {
            sceneMaterial->shininess = saturate(scalar);
        }
        else
        {
            sceneMaterial->shininess = 0.9f;
        }

        if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
        {
            sceneMaterial->emissiveColor = float3(color.r, color.g, color.b);
        }

        /* if (material->Get(AI_MATKEY_COLOR_TRANSPARENT, color) == AI_SUCCESS)
        {
            if (color.r > 0 || color.g > 0 || color.b > 0) {
                sceneMaterial->opacity = 1.0f - (color.r + color.g + color.b) / 3.0f;
                sceneMaterial->domain = MD_TRANSPARENT;
            }
        } */
	}
}

void ObjectFile::ConnectMaterialsWithJson(aiScene* pAssimpScene)
{
    assert(pAssimpScene->HasMaterials());

    m_Materials.resize(pAssimpScene->mNumMaterials);

    for (uint materialIndex = 0; materialIndex < pAssimpScene->mNumMaterials; ++materialIndex)
    {
        aiMaterial* material = pAssimpScene->mMaterials[materialIndex];

        aiString materialName;
        if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS)
        {
            auto found = m_MaterialsByName.find(materialName.C_Str());
            if (found == m_MaterialsByName.end())
            {
                log::warning("Couldn't find a json entry for material %s", materialName.C_Str());
                continue;
            }

            m_Materials[materialIndex] = found->second;
        }
        else
        {
            log::warning("Material %d doesn't have a name", materialIndex);
        }
    }
}
#endif // DONUT_WITH_ASSIMP

void ObjectFile::LoadMaterialsFromJson(const std::filesystem::path& materialFileName, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup)
{
    Json::Value root;
    if (!json::LoadFromFile(*m_fs, materialFileName, root))
        return;
    
    for (std::string& materialName : root.getMemberNames())
    {
        Json::Value& materialNode = root[materialName];
        Material* sceneMaterial = CreateMaterial();
        m_MaterialsByName[materialName] = sceneMaterial;
        sceneMaterial->name = materialName;

        auto loadColor = [this, &materialNode](const char* name) {
            Json::Value& colorNode = materialNode[name];
            if (colorNode.isArray())
                return float3(
                    float(colorNode[0u].asDouble()),
                    float(colorNode[1u].asDouble()),
                    float(colorNode[2u].asDouble())
                );
            return float3(0.f);
        };
        sceneMaterial->shininess = saturate(float(materialNode["Shininess"].asDouble()));
        sceneMaterial->opacity = saturate(float(materialNode["Opacity"].asDouble()));
        sceneMaterial->diffuseColor = loadColor("Diffuse");
        sceneMaterial->specularColor = loadColor("Specular");
		sceneMaterial->emissiveColor = loadColor("Emittance");

        Json::Value& texturesNode = materialNode["Textures"];

        auto loadTexture = [this, &textureCache, &texturesNode, &stats, &taskGroup](const char* name, bool sRGB) {
            Json::Value& node = texturesNode[name];
            if (node.isString())
                return textureCache.LoadTextureFromFileAsync(m_ScenePath / std::filesystem::path(node.asString()), sRGB, taskGroup);
            else
                return std::shared_ptr<LoadedTexture>();
        };

        sceneMaterial->diffuseTexture = loadTexture("Diffuse", true);
        sceneMaterial->normalsTexture = loadTexture("Bumpmap", false);
        sceneMaterial->specularTexture = loadTexture("Specular", true);
        sceneMaterial->emissiveTexture = loadTexture("Emittance", true);

        if (sceneMaterial->opacity < 1.0f)
            sceneMaterial->domain = MD_TRANSPARENT;
    }
}


MeshInfo* ObjectFile::CreateMeshInfo()
{
    return new MeshInfo();
}

MeshInstance* ObjectFile::CreateMeshInstance()
{
    return new MeshInstance();
}

Material* ObjectFile::CreateMaterial()
{
    return new Material();
}

nvrhi::BufferHandle CreateIndexBufferPtr(
    nvrhi::IDevice* device,
    nvrhi::ICommandList* commandList,
    const uint32_t* data,
    size_t size,
    const std::string& debugName)
{
    if (!data || !size)
        return nullptr;

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.isIndexBuffer = true;
    bufferDesc.byteSize = uint(size) * sizeof(uint32_t);
    bufferDesc.debugName = debugName.c_str();

    nvrhi::BufferHandle buffer = device->createBuffer(bufferDesc);
    
    commandList->beginTrackingBufferState(buffer, nvrhi::ResourceStates::COMMON);
    commandList->writeBuffer(buffer, data, bufferDesc.byteSize);
    commandList->endTrackingBufferState(buffer, nvrhi::ResourceStates::Enum(nvrhi::ResourceStates::INDEX_BUFFER | nvrhi::ResourceStates::SHADER_RESOURCE), true);

    return buffer;
}

template<typename T>
void CreateVertexBufferPtr(
    nvrhi::IDevice* device,
    nvrhi::ICommandList* commandList,
    VertexAttribute::Enum kind,
    VertexAttribute::Enum mask,
    std::unordered_map<VertexAttribute::Enum, nvrhi::BufferHandle>& buffers,
    T* data,
    size_t size,
    const std::string& debugName)
{
    if ((kind & mask) == 0)
        return;

    if (!data || !size)
        return;

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.isVertexBuffer = true;
    bufferDesc.byteSize = uint(size) * sizeof(T);
    bufferDesc.debugName = debugName.c_str();

    nvrhi::BufferHandle buffer = device->createBuffer(bufferDesc);
    buffers[kind] = buffer;

    commandList->beginTrackingBufferState(buffer, nvrhi::ResourceStates::COMMON);
    commandList->writeBuffer(buffer, data, bufferDesc.byteSize);
    commandList->endTrackingBufferState(buffer, nvrhi::ResourceStates::Enum(nvrhi::ResourceStates::VERTEX_BUFFER | nvrhi::ResourceStates::SHADER_RESOURCE), true);
}

template<typename T> 
void CreateVertexBuffer(
    nvrhi::IDevice* device, 
    nvrhi::ICommandList* commandList,
    VertexAttribute::Enum kind, 
    VertexAttribute::Enum mask, 
    std::unordered_map<VertexAttribute::Enum, nvrhi::BufferHandle>& buffers,
    std::vector<T>& data,
    const std::string& debugName)
{
    if (data.empty())
        return;

    CreateVertexBufferPtr(device, commandList, kind, mask, buffers, data.data(), data.size(), debugName);

    // Force deallocate the array
    std::vector<T>().swap(data);
}

void ObjectFile::CreateRenderingResources(nvrhi::IDevice* device, nvrhi::ICommandList* commandList)
{
    assert(device);
    assert(commandList);

    CreateVertexBuffers(device, commandList);
    CreateTransformBuffer(device);

    SortMeshInstances();
    UpdateTransformBuffer(commandList);
    
    FinalizeMaterials(device, commandList);
}

void ObjectFile::CreateVertexBuffers(nvrhi::IDevice* device, nvrhi::ICommandList* commandList)
{
    if (m_ChunkMeshSet)
    {
        m_Buffers.indexBuffer = CreateIndexBufferPtr(device, commandList, m_ChunkMeshSet->indices, m_ChunkMeshSet->nindices, "IndexBuffer");

        CreateVertexBufferPtr(device, commandList, VertexAttribute::POSITION, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.position, m_ChunkMeshSet->nverts, "AttrPosition");
        CreateVertexBufferPtr(device, commandList, VertexAttribute::TEXCOORD1, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.texcoord0, m_ChunkMeshSet->nverts, "AttrTexcoord1");
        CreateVertexBufferPtr(device, commandList, VertexAttribute::TEXCOORD2, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.texcoord1, m_ChunkMeshSet->nverts, "AttrTexcoord2");
        CreateVertexBufferPtr(device, commandList, VertexAttribute::NORMAL, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.normal, m_ChunkMeshSet->nverts, "AttrNormal");
        CreateVertexBufferPtr(device, commandList, VertexAttribute::TANGENT, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.tangent, m_ChunkMeshSet->nverts, "AttrTangent");
        CreateVertexBufferPtr(device, commandList, VertexAttribute::BITANGENT, m_AttributeMask, m_Buffers.vertexBuffers, m_ChunkMeshSet->streams.bitangent, m_ChunkMeshSet->nverts, "AttrBitangent");

        m_ChunkMeshSet = nullptr;
    }
    else
    {
        if (m_Indices.size() > 0)
        {
            m_Buffers.indexBuffer = CreateIndexBufferPtr(device, commandList, m_Indices.data(), m_Indices.size(), "IndexBuffer");

            // Force deallocate the array
            std::vector<uint>().swap(m_Indices);
        }

        CreateVertexBuffer(device, commandList, VertexAttribute::POSITION, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrPosition, "AttrPosition");
        CreateVertexBuffer(device, commandList, VertexAttribute::TEXCOORD1, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrTexcoord1, "AttrTexcoord1");
        CreateVertexBuffer(device, commandList, VertexAttribute::TEXCOORD2, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrTexcoord2, "AttrTexcoord2");
        CreateVertexBuffer(device, commandList, VertexAttribute::NORMAL, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrNormal, "AttrNormal");
        CreateVertexBuffer(device, commandList, VertexAttribute::TANGENT, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrTangent, "AttrTangent");
        CreateVertexBuffer(device, commandList, VertexAttribute::BITANGENT, m_AttributeMask, m_Buffers.vertexBuffers, m_AttrBitangent, "AttrBitangent");
    }
}

void ObjectFile::SortMeshInstances()
{
    auto compareMeshInstances = [](const MeshInstance* a, const MeshInstance* b)
    {
        if (a->mesh->material != b->mesh->material)
            return a->mesh->material < b->mesh->material;
        else
            return a->mesh < b->mesh;
    };

    std::sort(m_MeshInstances.begin(), m_MeshInstances.end(), compareMeshInstances);
}

void ObjectFile::CreateTransformBuffer(nvrhi::IDevice* device)
{
    nvrhi::BufferDesc desc;
    desc.isVertexBuffer = true;
    desc.byteSize = uint(sizeof(float3x4) * m_MeshInstances.size() * 2);
    desc.debugName = "Transforms";
    desc.initialState = nvrhi::ResourceStates::VERTEX_BUFFER;
    desc.keepInitialState = true;

    nvrhi::BufferHandle transformBuffer = device->createBuffer(desc);
    
    m_Buffers.vertexBuffers[VertexAttribute::TRANSFORM] = transformBuffer;
}

void ObjectFile::UpdateMeshBounds()
{
    m_SceneBounds = box3::empty();

    for (auto& instance : m_MeshInstances)
    {
        instance->transformedBounds = instance->mesh->objectSpaceBounds * instance->localTransform;
        instance->transformedCenter = instance->transformedBounds.center();

        m_SceneBounds |= instance->transformedBounds;
    }
}

void ObjectFile::UpdateTransformBuffer(nvrhi::ICommandList* commandList)
{
    nvrhi::IBuffer* transformBuffer = m_Buffers.vertexBuffers[VertexAttribute::TRANSFORM];

    if (transformBuffer == nullptr)
        return;

    std::vector<float3x4> transforms;
    transforms.resize(m_MeshInstances.size() * 2);
    for (uint instanceID = 0; instanceID < uint(m_MeshInstances.size()); instanceID++)
    {
        m_MeshInstances[instanceID]->instanceOffset = instanceID;
        transforms[instanceID * 2 + 0] = float3x4(transpose(affineToHomogeneous(m_MeshInstances[instanceID]->localTransform)));
        transforms[instanceID * 2 + 1] = float3x4(transpose(affineToHomogeneous(m_MeshInstances[instanceID]->previousTransform)));
    }

    size_t dataSize = transforms.size() * sizeof(float3x4);
    assert(dataSize = transformBuffer->GetDesc().byteSize);

    commandList->writeBuffer(transformBuffer, transforms.data(), dataSize);
}

nvrhi::BufferHandle ObjectFile::CreateMaterialConstantBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const Material* material)
{
    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = sizeof(MaterialConstants);
    bufferDesc.debugName = material->name.c_str();
    bufferDesc.isConstantBuffer = true;
    bufferDesc.initialState = nvrhi::ResourceStates::CONSTANT_BUFFER;
    bufferDesc.keepInitialState = true;
    nvrhi::BufferHandle buffer = device->createBuffer(bufferDesc);

    UpdateMaterialConstantBuffer(commandList, material, buffer);

    return buffer;
}

void ObjectFile::FinalizeMaterials(nvrhi::IDevice* device, nvrhi::ICommandList* commandList)
{
    for (auto material : m_Materials)
    {
        if (material->diffuseTexture && material->diffuseTexture->texture == nullptr)
            material->diffuseTexture = nullptr;
        if (material->specularTexture && material->specularTexture->texture == nullptr)
            material->specularTexture = nullptr;
        if (material->normalsTexture && material->normalsTexture->texture == nullptr)
            material->normalsTexture = nullptr;
        if (material->emissiveTexture && material->emissiveTexture->texture == nullptr)
            material->emissiveTexture = nullptr;

        if (material->diffuseTexture && material->diffuseTexture->texture != nullptr)
        {
            if(material->diffuseTexture->originalBitsPerPixel == 32 || material->diffuseTexture->originalBitsPerPixel == 64)
                material->domain = MD_ALPHA_TESTED;
            else
            {
                nvrhi::Format format = material->diffuseTexture->texture->GetDesc().format;
                switch (format)
                {
                case nvrhi::Format::BC1_UNORM:
                case nvrhi::Format::BC1_UNORM_SRGB:
                case nvrhi::Format::BC2_UNORM:
                case nvrhi::Format::BC2_UNORM_SRGB:
                case nvrhi::Format::BC3_UNORM:
                case nvrhi::Format::BC3_UNORM_SRGB:
                case nvrhi::Format::BC7_UNORM:
                case nvrhi::Format::BC7_UNORM_SRGB:
                    material->domain = MD_ALPHA_TESTED;
                    break;
                default:
                    break;
                }
            }
        }

        if (material->specularTexture)
        {
            if (m_LoadFlags & ObjectLoadFlags::METAL_ROUGH_MODEL)
                material->specularTextureType = SpecularType_MetalRough;
            else if (material->specularTexture->originalBitsPerPixel == 8)
                material->specularTextureType = SpecularType_Scalar;
            else
                material->specularTextureType = SpecularType_Color;
        }
        else
            material->specularTextureType = SpecularType_None;

        material->materialConstants = CreateMaterialConstantBuffer(device, commandList, material);
    }
}

