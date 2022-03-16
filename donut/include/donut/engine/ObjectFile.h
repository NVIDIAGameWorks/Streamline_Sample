#pragma once

#include <donut/engine/SceneTypes.h>
#include <donut/core/math/math.h>
#include <donut/core/taskgroup.h>
#include <nvrhi/nvrhi.h>
#include <vector>
#include <map>
#ifdef _WIN32
#include <ppl.h>
#endif // _WIN32
#include <memory>
#include <filesystem>

struct aiScene;
struct aiNode;

namespace Json
{
    class Value;
}

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::chunk
{
    struct MeshSet;
}

namespace donut::engine
{
    class TextureCache;

    struct ObjectLoadFlags
    {
        enum Enum
        {
            JOIN_IDENTICAL_VERTICES = 0x01,
            IMPROVE_CACHE_LOCALITY = 0x02,
            TRIANGULATE = 0x04,
            CALC_TANGENT_SPACE = 0x08,
            GEN_NORMALS = 0x10,
            FLIP_UVS = 0x20,
            METAL_ROUGH_MODEL = 0x40,

            DEFAULT = 0x3f
        };
    };

    void UpdateMaterialConstantBuffer(nvrhi::ICommandList* commandList, const Material* material, nvrhi::IBuffer* buffer);

    bool SaveMaterialsAsJson(const char* jsonFileName, const std::vector<Material*> materials);

    class ObjectFile : public IMeshSet
    {
    protected:
        dm::box3							m_SceneBounds;
        std::vector<MeshInfo*>              m_Meshes;
        std::vector<MeshInstance*>          m_MeshInstances;
        std::vector<Material*>				m_Materials;
        std::map<std::string, Material*>    m_MaterialsByName;
        BufferGroup                         m_Buffers;
        ObjectLoadFlags::Enum               m_LoadFlags;

        std::shared_ptr<vfs::IFileSystem>   m_fs;
        std::filesystem::path				m_ScenePath;

        std::vector<dm::affine3>			m_ObjectInstanceTransforms;

        VertexAttribute::Enum               m_AttributeMask;
        std::vector<uint32_t>				m_Indices;
        std::vector<dm::float3>				m_AttrPosition;
        std::vector<dm::float2>				m_AttrTexcoord1;
        std::vector<dm::float2>				m_AttrTexcoord2;
        std::vector<uint32_t>				m_AttrNormal;
        std::vector<uint32_t>				m_AttrTangent;
        std::vector<uint32_t>				m_AttrBitangent;

        std::shared_ptr<const chunk::MeshSet> m_ChunkMeshSet;

        void LoadMaterialsFromJson(const std::filesystem::path& materialFileName, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup);
#ifdef DONUT_WITH_ASSIMP
        bool LoadAssimpObjectFile(const std::filesystem::path& fileName, bool useMaterialFile, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup);
        void InitMeshInfos(aiScene* pAssimpScene);
        void RecursivelyCollectMeshInstances(aiScene* pAssimpScene, const aiNode* pNode, const dm::affine3& transform, uint32_t level, const std::string& prefix);
        void LoadMaterialsFromAssimp(aiScene* pAssimpScene, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup);
        void ConnectMaterialsWithJson(aiScene* pAssimpScene);
#endif //DONUT_WITH_ASSIMP
        bool LoadChunkObjectFile(std::weak_ptr<const vfs::IBlob> data, const std::filesystem::path& fileName);

        virtual MeshInfo* CreateMeshInfo();
        virtual MeshInstance* CreateMeshInstance();
        virtual Material* CreateMaterial();

        virtual nvrhi::BufferHandle CreateMaterialConstantBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const Material* material);
        
        virtual void CreateVertexBuffers(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
        virtual void SortMeshInstances();
        virtual void CreateTransformBuffer(nvrhi::IDevice* device);
        virtual void FinalizeMaterials(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);

    public:
        ObjectFile(std::shared_ptr<vfs::IFileSystem> fs);
        ~ObjectFile();

        virtual bool Load(const std::filesystem::path& fileName, const std::filesystem::path& materialFileName, VertexAttribute::Enum attributeMask, TextureCache& textureCache, SceneLoadingStats& stats, concurrency::task_group& taskGroup, ObjectLoadFlags::Enum flags = ObjectLoadFlags::DEFAULT);

        void CreateRenderingResources(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
        void ComputeTransformedMeshBounds();
        dm::box3 GetSceneBounds() { return m_SceneBounds; }

        void AddObjectInstance(const dm::affine3& transform) { m_ObjectInstanceTransforms.push_back(transform); }

        void UpdateMeshBounds();
        void UpdateTransformBuffer(nvrhi::ICommandList* commandList);

        // IMeshSet implementation
        const std::vector<MeshInfo*>& GetMeshes() const override { return m_Meshes; }
        const std::vector<MeshInstance*>& GetMeshInstances() const override { return m_MeshInstances; }
        const std::vector<Material*>& GetMaterials() const override { return m_Materials; }
    };
}
