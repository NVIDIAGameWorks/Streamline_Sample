/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/common/containers.h>
#include <nvrhi/d3d12/d3d12.h>
#include <nvrhi/d3d12/internals.h>

#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <assert.h>
#include <list>

namespace nvrhi
{
#if NVRHI_WITH_DXR
namespace dxr
{
    ShaderTable::ShaderTable(Pipeline* _pipeline)
        : pipeline(_pipeline)
    {
    }

    uint32_t ShaderTable::GetNumEntries() const
    {
        return 1 + // rayGeneration
            uint32_t(missShaders.size()) +
            uint32_t(hitGroups.size()) +
            uint32_t(callableShaders.size());
    }

    bool ShaderTable::VerifyExport(const Pipeline::ExportTableEntry* pExport, IBindingSet* bindings)
    {
        auto messageCallback = pipeline->parent->getMessageCallback();

        if (!pExport)
        {
            messageCallback->message(MessageSeverity::Error, "Couldn't find a DXR PSO export with a given name");
            return false;
        }

        if (pExport->bindingLayout && !bindings)
        {
            messageCallback->message(MessageSeverity::Error, "A shader table entry does not provide required local bindings");
            return false;
        }

        if (!pExport->bindingLayout && bindings)
        {
            messageCallback->message(MessageSeverity::Error, "A shader table entry provides local bindings, but none are required");
            return false;
        }

        if (bindings && (static_cast<d3d12::BindingSet*>(bindings)->layout != pExport->bindingLayout))
        {
            messageCallback->message(MessageSeverity::Error, "A shader table entry provides local bindings that do not match the expected layout");
            return false;
        }

        return true;
    }

    void ShaderTable::SetRayGenerationShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const Pipeline::ExportTableEntry* pipelineExport = pipeline->GetExport(exportName);

        if (VerifyExport(pipelineExport, bindings))
        {
            rayGenerationShader.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            rayGenerationShader.localBindings = bindings;

            ++version;
        }
    }

    int ShaderTable::AddMissShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const Pipeline::ExportTableEntry* pipelineExport = pipeline->GetExport(exportName);

        if (VerifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            missShaders.push_back(entry);

            ++version;

            return int(missShaders.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::AddHitGroup(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const Pipeline::ExportTableEntry* pipelineExport = pipeline->GetExport(exportName);

        if (VerifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            hitGroups.push_back(entry);

            ++version;

            return int(hitGroups.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::AddCallableShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const Pipeline::ExportTableEntry* pipelineExport = pipeline->GetExport(exportName);

        if (VerifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            callableShaders.push_back(entry);

            ++version;

            return int(callableShaders.size()) - 1;
        }

        return -1;
    }

    void ShaderTable::ClearMissShaders()
    {
        missShaders.clear();
        ++version;
    }

    void ShaderTable::ClearHitShaders()
    {
        hitGroups.clear();
        ++version;
    }

    void ShaderTable::ClearCallableShaders()
    {
        callableShaders.clear();
        ++version;
    }

    rt::IPipeline* ShaderTable::GetPipeline()
    {
        return pipeline;
    }


    const Pipeline::ExportTableEntry* Pipeline::GetExport(const char* name)
    {
        const auto exportEntryIt = exports.find(name);
        if (exportEntryIt == exports.end())
        {
            return nullptr;
        }

        return &exportEntryIt->second;
    }

    rt::ShaderTableHandle Pipeline::CreateShaderTable()
    { 
        return rt::ShaderTableHandle::Create(new ShaderTable(this));
    }

    uint32_t Pipeline::GetShaderTableEntrySize() const
    {
        uint32_t requiredSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64) * maxLocalRootParameters;
        return Align(requiredSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    }

    Object AccelStruct::getNativeObject(ObjectType objectType)
    {
        return dataBuffer->getNativeObject(objectType);
    }
}

namespace d3d12
{

    rt::AccelStructHandle Device::createBottomLevelAccelStruct(const rt::BottomLevelAccelStructDesc& desc)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3dGeometryDescs;
        d3dGeometryDescs.resize(desc.triangles.size());

        for(uint32_t i = 0; i < desc.triangles.size(); i++)
        {
            const auto& geometryDesc = desc.triangles[i];
            auto& d3dGeometryDesc = d3dGeometryDescs[i];

            d3dGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            d3dGeometryDesc.Flags = (D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryDesc.flags;
            d3dGeometryDesc.Triangles.IndexBuffer = static_cast<Buffer*>(geometryDesc.indexBuffer)->gpuVA + geometryDesc.indexOffset;
            d3dGeometryDesc.Triangles.VertexBuffer.StartAddress = static_cast<Buffer*>(geometryDesc.vertexBuffer)->gpuVA + geometryDesc.vertexOffset;
            d3dGeometryDesc.Triangles.VertexBuffer.StrideInBytes = geometryDesc.vertexStride;
            d3dGeometryDesc.Triangles.IndexFormat = GetFormatMapping(geometryDesc.indexFormat).srvFormat;
            d3dGeometryDesc.Triangles.VertexFormat = GetFormatMapping(geometryDesc.vertexFormat).srvFormat;
            d3dGeometryDesc.Triangles.IndexCount = geometryDesc.indexCount;
            d3dGeometryDesc.Triangles.VertexCount = geometryDesc.vertexCount;
            d3dGeometryDesc.Triangles.Transform3x4 = 0;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
        ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        ASInputs.pGeometryDescs = d3dGeometryDescs.data();
        ASInputs.NumDescs = UINT(d3dGeometryDescs.size());
        ASInputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
        m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

        dxr::AccelStruct* as = new dxr::AccelStruct();

        as->scratchBufferSize = ASPreBuildInfo.ScratchDataSizeInBytes;

        assert(ASPreBuildInfo.ResultDataMaxSizeInBytes <= ~0u);

        BufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = uint32_t(ASPreBuildInfo.ResultDataMaxSizeInBytes);
        bufferDesc.initialState = ResourceStates::RAY_TRACING_AS;
        bufferDesc.debugName = "BottomLevelAS/Data";
        BufferHandle buffer = createBuffer(bufferDesc);
        as->dataBuffer = static_cast<Buffer*>(buffer.Get());

        as->dataBuffer->permanentState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        as->isTopLevel = false;
        as->trackLiveness = desc.trackLiveness;

        return rt::AccelStructHandle::Create(as);
    }

    rt::AccelStructHandle Device::createTopLevelAccelStruct(uint32_t numInstances, rt::AccelStructBuildFlags::Enum buildFlags)
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
        ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        ASInputs.InstanceDescs = 0;
        ASInputs.NumDescs = numInstances;
        ASInputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)buildFlags;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
        m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

        dxr::AccelStruct* as = new dxr::AccelStruct();

        as->scratchBufferSize = ASPreBuildInfo.ScratchDataSizeInBytes;

        assert(ASPreBuildInfo.ResultDataMaxSizeInBytes <= ~0u);

        BufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = uint32_t(ASPreBuildInfo.ResultDataMaxSizeInBytes);
        bufferDesc.initialState = ResourceStates::RAY_TRACING_AS;
        bufferDesc.debugName = "TopLevelAS/Data";
        BufferHandle buffer = createBuffer(bufferDesc);
        as->dataBuffer = static_cast<Buffer*>(buffer.Get());

        as->dataBuffer->permanentState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        as->isTopLevel = true;

        return rt::AccelStructHandle::Create(as);
    }

#define NEW_ON_STACK(T) (T*)alloca(sizeof(T))
    
    rt::PipelineHandle Device::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        dxr::Pipeline* pso = new dxr::Pipeline(this);
        pso->desc = desc;
        pso->maxLocalRootParameters = 0;

        // Collect all DXIL libraries that are referenced in `desc`, and enumerate their exports.
        // Build local root signatures for all referenced local binding layouts.
        // Convert the export names to wstring.

        struct Library
        {
            const void* pBlob = nullptr;
            size_t blobSize = 0;
            std::vector<std::pair<std::wstring, std::wstring>> exports; // vector(originalName, newName)
            std::vector<D3D12_EXPORT_DESC> d3dExports;
        };

        // Go through the individual shaders first.

        std::unordered_map<const void*, Library> dxilLibraries;

        for (const rt::PipelineShaderDesc& shaderDesc : desc.shaders)
        {
            const void* pBlob = nullptr;
            size_t blobSize = 0;
            shaderDesc.shader->GetBytecode(&pBlob, &blobSize);

            // Assuming that no shader is referenced twice, we just add every shader to its library export list.

            Library& library = dxilLibraries[pBlob];
            library.pBlob = pBlob;
            library.blobSize = blobSize;

            std::string originalShaderName = shaderDesc.shader->GetDesc().entryName;
            std::string newShaderName = shaderDesc.exportName.empty() ? originalShaderName : shaderDesc.exportName;

            library.exports.push_back(std::make_pair<std::wstring, std::wstring>(
                std::wstring(originalShaderName.begin(), originalShaderName.end()),
                std::wstring(newShaderName.begin(), newShaderName.end())
                ));

            // Build a local root signature for the shader, if needed.

            if (shaderDesc.bindingLayout)
            {
                RootSignatureHandle& localRS = pso->localRootSignatures[shaderDesc.bindingLayout];
                if (!localRS)
                {
                    localRS = buildRootSignature({ shaderDesc.bindingLayout }, false, true);

                    BindingLayout* layout = static_cast<BindingLayout*>(shaderDesc.bindingLayout.Get());
                    pso->maxLocalRootParameters = std::max(pso->maxLocalRootParameters, uint32_t(layout->rootParameters.size()));
                }
            }
        }

        // Still in the collection phase - go through the hit groups.
        // Rename all exports used in the hit groups to avoid collisions between different libraries.

        std::vector<D3D12_HIT_GROUP_DESC> d3dHitGroups;
        std::unordered_map<IShader*, std::wstring> hitGroupShaderNames;
        std::vector<std::wstring> hitGroupExportNames;

        for (const rt::PipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        {
            for (const ShaderHandle& shader : { hitGroupDesc.closestHitShader, hitGroupDesc.anyHitShader, hitGroupDesc.intersectionShader })
            {
                if (!shader)
                    continue;

                std::wstring& newName = hitGroupShaderNames[shader];

                // See if we've encountered this particular shader before...

                if (newName.empty())
                {
                    // No - add it to the corresponding library, come up with a new name for it.

                    const void* pBlob = nullptr;
                    size_t blobSize = 0;
                    shader->GetBytecode(&pBlob, &blobSize);

                    Library& library = dxilLibraries[pBlob];
                    library.pBlob = pBlob;
                    library.blobSize = blobSize;

                    std::string originalShaderName = shader->GetDesc().entryName;
                    std::string newShaderName = originalShaderName + std::to_string(hitGroupShaderNames.size());

                    library.exports.push_back(std::make_pair<std::wstring, std::wstring>(
                        std::wstring(originalShaderName.begin(), originalShaderName.end()),
                        std::wstring(newShaderName.begin(), newShaderName.end())
                        ));

                    newName = std::wstring(newShaderName.begin(), newShaderName.end());
                }
            }

            // Build a local root signature for the hit group, if needed.

            if (hitGroupDesc.bindingLayout)
            {
                RootSignatureHandle& localRS = pso->localRootSignatures[hitGroupDesc.bindingLayout];
                if (!localRS)
                {
                    localRS = buildRootSignature({ hitGroupDesc.bindingLayout }, false, true);

                    BindingLayout* layout = static_cast<BindingLayout*>(hitGroupDesc.bindingLayout.Get());
                    pso->maxLocalRootParameters = std::max(pso->maxLocalRootParameters, uint32_t(layout->rootParameters.size()));
                }
            }

            // Create a hit group descriptor and store the new export names in it.

            D3D12_HIT_GROUP_DESC d3dHitGroupDesc = {};
            if (hitGroupDesc.anyHitShader)
                d3dHitGroupDesc.AnyHitShaderImport = hitGroupShaderNames[hitGroupDesc.anyHitShader].c_str();
            if (hitGroupDesc.closestHitShader)
                d3dHitGroupDesc.ClosestHitShaderImport = hitGroupShaderNames[hitGroupDesc.closestHitShader].c_str();
            if (hitGroupDesc.intersectionShader)
                d3dHitGroupDesc.IntersectionShaderImport = hitGroupShaderNames[hitGroupDesc.intersectionShader].c_str();

            if (hitGroupDesc.isProceduralPrimitive)
                d3dHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
            else
                d3dHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

            std::wstring hitGroupExportName = std::wstring(hitGroupDesc.exportName.begin(), hitGroupDesc.exportName.end());
            hitGroupExportNames.push_back(hitGroupExportName); // store the wstring so that it's not deallocated
            d3dHitGroupDesc.HitGroupExport = hitGroupExportNames[hitGroupExportNames.size() - 1].c_str();
            d3dHitGroups.push_back(d3dHitGroupDesc);
        }

        // Create descriptors for DXIL libraries, enumerate the exports used from each library.

        std::vector<D3D12_DXIL_LIBRARY_DESC> d3dDxilLibraries;
        d3dDxilLibraries.reserve(dxilLibraries.size());
        for (auto& it : dxilLibraries)
        {
            Library& library = it.second;

            for (const std::pair<std::wstring, std::wstring>& exportNames : library.exports)
            {
                D3D12_EXPORT_DESC d3dExportDesc = {};
                d3dExportDesc.ExportToRename = exportNames.first.c_str();
                d3dExportDesc.Name = exportNames.second.c_str();
                d3dExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
                library.d3dExports.push_back(d3dExportDesc);
            }

            D3D12_DXIL_LIBRARY_DESC d3dLibraryDesc = {};
            d3dLibraryDesc.DXILLibrary.pShaderBytecode = library.pBlob;
            d3dLibraryDesc.DXILLibrary.BytecodeLength = library.blobSize;
            d3dLibraryDesc.NumExports = UINT(library.d3dExports.size());
            d3dLibraryDesc.pExports = library.d3dExports.data();

            d3dDxilLibraries.push_back(d3dLibraryDesc);
        }

        // Start building the D3D state subobject array.

        std::vector<D3D12_STATE_SUBOBJECT> d3dSubobjects;

        // Same subobject is reused multiple times and copied to the vector each time.
        D3D12_STATE_SUBOBJECT d3dSubobject = {};

        // Subobject: Shader config

        D3D12_RAYTRACING_SHADER_CONFIG d3dShaderConfig = {};
        d3dShaderConfig.MaxAttributeSizeInBytes = desc.maxAttributeSize;
        d3dShaderConfig.MaxPayloadSizeInBytes = desc.maxPayloadSize;

        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        d3dSubobject.pDesc = &d3dShaderConfig;
        d3dSubobjects.push_back(d3dSubobject);

        // Subobject: Pipeline config

        D3D12_RAYTRACING_PIPELINE_CONFIG d3dPipelineConfig = {};
        d3dPipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;

        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        d3dSubobject.pDesc = &d3dPipelineConfig;
        d3dSubobjects.push_back(d3dSubobject);

        // Subobjects: DXIL libraries

        for (const D3D12_DXIL_LIBRARY_DESC& d3dLibraryDesc : d3dDxilLibraries)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            d3dSubobject.pDesc = &d3dLibraryDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobjects: hit groups

        for (const D3D12_HIT_GROUP_DESC& d3dHitGroupDesc : d3dHitGroups)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            d3dSubobject.pDesc = &d3dHitGroupDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobject: global root signature

        D3D12_GLOBAL_ROOT_SIGNATURE d3dGlobalRootSignature = {};

        if (!desc.globalBindingLayouts.empty())
        {
            RootSignatureHandle rootSignature = buildRootSignature(desc.globalBindingLayouts, false, false);
            pso->globalRootSignature = static_cast<RootSignature*>(rootSignature.Get());
            d3dGlobalRootSignature.pGlobalRootSignature = pso->globalRootSignature->getNativeObject(ObjectTypes::D3D12_RootSignature);

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = &d3dGlobalRootSignature;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobjects: local root signatures

        // Make sure that adding local root signatures does not resize the array,
        // because we need to store pointers to array elements there.
        d3dSubobjects.reserve(d3dSubobjects.size() + pso->localRootSignatures.size() * 2);

        // Same - pre-allocate the arrays to avoid resizing them
        size_t numAssociations = desc.shaders.size() + desc.hitGroups.size();
        std::vector<std::wstring> d3dAssociationExports;
        std::vector<LPCWSTR> d3dAssociationExportsCStr;
        d3dAssociationExports.reserve(numAssociations);
        d3dAssociationExportsCStr.reserve(numAssociations);

        for (const auto& it : pso->localRootSignatures)
        {
            auto d3dLocalRootSignature = NEW_ON_STACK(D3D12_LOCAL_ROOT_SIGNATURE);
            d3dLocalRootSignature->pLocalRootSignature = it.second->getNativeObject(ObjectTypes::D3D12_RootSignature);

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = d3dLocalRootSignature;
            d3dSubobjects.push_back(d3dSubobject);

            auto d3dAssociation = NEW_ON_STACK(D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
            d3dAssociation->pSubobjectToAssociate = &d3dSubobjects[d3dSubobjects.size() - 1];
            d3dAssociation->NumExports = 0;
            size_t firstExportIndex = d3dAssociationExportsCStr.size();

            for (auto shader : desc.shaders)
            {
                if (shader.bindingLayout == it.first)
                {
                    std::string exportName = shader.exportName.empty() ? shader.shader->GetDesc().entryName : shader.exportName;
                    std::wstring exportNameW = std::wstring(exportName.begin(), exportName.end());
                    d3dAssociationExports.push_back(exportNameW);
                    d3dAssociationExportsCStr.push_back(d3dAssociationExports[d3dAssociationExports.size() - 1].c_str());
                    d3dAssociation->NumExports += 1;
                }
            }

            for (auto hitGroup : desc.hitGroups)
            {
                if (hitGroup.bindingLayout == it.first)
                {
                    std::wstring exportNameW = std::wstring(hitGroup.exportName.begin(), hitGroup.exportName.end());
                    d3dAssociationExports.push_back(exportNameW);
                    d3dAssociationExportsCStr.push_back(d3dAssociationExports[d3dAssociationExports.size() - 1].c_str());
                    d3dAssociation->NumExports += 1;
                }
            }
            
            d3dAssociation->pExports = &d3dAssociationExportsCStr[firstExportIndex];

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            d3dSubobject.pDesc = d3dAssociation;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Top-level PSO descriptor structure

        D3D12_STATE_OBJECT_DESC pipelineDesc = {};
        pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        pipelineDesc.NumSubobjects = static_cast<UINT>(d3dSubobjects.size());
        pipelineDesc.pSubobjects = d3dSubobjects.data();

        HRESULT hr = m_pDevice5->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&pso->pipelineState));
        if (FAILED(hr))
        {
            SIGNAL_ERROR("Failed to create a DXR pipeline state object");
            return nullptr;
        }

        hr = pso->pipelineState->QueryInterface(IID_PPV_ARGS(&pso->pipelineInfo));
        if (FAILED(hr))
        {
            SIGNAL_ERROR("Failed to get a DXR pipeline info interface from a PSO");
            return nullptr;
        }

        for (const rt::PipelineShaderDesc& shaderDesc : desc.shaders)
        {
            std::string exportName = !shaderDesc.exportName.empty() ? shaderDesc.exportName : shaderDesc.shader->GetDesc().entryName;
            std::wstring exportNameW = std::wstring(exportName.begin(), exportName.end());
            const void* pShaderIdentifier = pso->pipelineInfo->GetShaderIdentifier(exportNameW.c_str());

            if (pShaderIdentifier == nullptr)
            {
                SIGNAL_ERROR("Failed to get an identifier for a shader in a fresh DXR PSO");
                return nullptr;
            }

            pso->exports[exportName] = dxr::Pipeline::ExportTableEntry{ shaderDesc.bindingLayout, pShaderIdentifier };
        }

        for(const rt::PipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        { 
            std::wstring exportNameW = std::wstring(hitGroupDesc.exportName.begin(), hitGroupDesc.exportName.end());
            const void* pShaderIdentifier = pso->pipelineInfo->GetShaderIdentifier(exportNameW.c_str());

            if (pShaderIdentifier == nullptr)
            {
                SIGNAL_ERROR("Failed to get an identifier for a hit group in a fresh DXR PSO");
                return nullptr;
            }

            pso->exports[hitGroupDesc.exportName] = dxr::Pipeline::ExportTableEntry{ hitGroupDesc.bindingLayout, pShaderIdentifier };
        }

        return rt::PipelineHandle::Create(pso);
    }

    void CommandList::setRayTracingState(const rt::State& state)
    {
        dxr::ShaderTable* shaderTable = static_cast<dxr::ShaderTable*>(state.shaderTable);
        dxr::Pipeline* pso = shaderTable->pipeline;

        dxr::ShaderTableState* shaderTableState = getShaderTableStateTracking(shaderTable);

        bool rebuildShaderTable = shaderTableState->committedVersion != shaderTable->version ||
            shaderTableState->descriptorHeapSRV != m_device->m_dhSRVetc.GetShaderVisibleHeap() ||
            shaderTableState->descriptorHeapSamplers != m_device->m_dhSamplers.GetShaderVisibleHeap();

        if (rebuildShaderTable)
        {
            uint32_t entrySize = pso->GetShaderTableEntrySize();
            uint32_t sbtSize = shaderTable->GetNumEntries() * entrySize;

            unsigned char* cpuVA;
            D3D12_GPU_VIRTUAL_ADDRESS gpuVA;
            if (!m_upload.SuballocateBuffer(sbtSize, nullptr, nullptr, (void**)&cpuVA, &gpuVA,
                m_recordingInstanceID, m_completedInstanceID, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT))
            {
                CHECK_ERROR(false, "Couldn't suballocate an upload buffer");
                return;
            }

            uint32_t entryIndex = 0;

            auto writeEntry = [this, entrySize, &cpuVA, &gpuVA, &entryIndex](const dxr::ShaderTable::Entry& entry) 
            {
                memcpy(cpuVA, entry.pShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

                if (entry.localBindings)
                {
                    d3d12::BindingSet* bindingSet = static_cast<d3d12::BindingSet*>(entry.localBindings.Get());
                    d3d12::BindingLayout* layout = bindingSet->layout;

                    d3d12::StageBindingLayout* stageLayout = layout->stages[ShaderType::SHADER_ALL_GRAPHICS].get();

                    if (stageLayout)
                    {
                        if (stageLayout->descriptorTableSizeSamplers > 0)
                        {
                            auto pTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + stageLayout->rootParameterSamplers * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                            *pTable = m_device->GetSamplerHeap().GetGpuHandle(bindingSet->descriptorTablesSamplers[ShaderType::SHADER_ALL_GRAPHICS]);
                        }

                        if (stageLayout->descriptorTableSizeSRVetc > 0)
                        {
                            auto pTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + stageLayout->rootParameterSRVetc * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                            *pTable = m_device->GetShaderResourceViewDescriptorHeap().GetGpuHandle(bindingSet->descriptorTablesSRVetc[ShaderType::SHADER_ALL_GRAPHICS]);
                        }

                        if (stageLayout->rootParametersVolatileCB.size() > 0)
                        {
                            m_device->getMessageCallback()->message(MessageSeverity::Error, "Cannot use Volatile CBs in a shader binding table");
                            return;
                        }
                    }
                }

                cpuVA += entrySize;
                gpuVA += entrySize;
                entryIndex += 1;
            };

            D3D12_DISPATCH_RAYS_DESC& drd = shaderTableState->dispatchRaysTemplate;
            memset(&drd, 0, sizeof(D3D12_DISPATCH_RAYS_DESC));

            drd.RayGenerationShaderRecord.StartAddress = gpuVA;
            drd.RayGenerationShaderRecord.SizeInBytes = entrySize;
            writeEntry(shaderTable->rayGenerationShader);
            
            if (!shaderTable->missShaders.empty())
            {
                drd.MissShaderTable.StartAddress = gpuVA;
                drd.MissShaderTable.StrideInBytes = (shaderTable->missShaders.size() == 1) ? 0 : entrySize;
                drd.MissShaderTable.SizeInBytes = uint32_t(shaderTable->missShaders.size()) * entrySize;

                for (auto& entry : shaderTable->missShaders)
                    writeEntry(entry);
            }

            if (!shaderTable->hitGroups.empty())
            {
                drd.HitGroupTable.StartAddress = gpuVA;
                drd.HitGroupTable.StrideInBytes = (shaderTable->hitGroups.size() == 1) ? 0 : entrySize;
                drd.HitGroupTable.SizeInBytes = uint32_t(shaderTable->hitGroups.size()) * entrySize;

                for (auto& entry : shaderTable->hitGroups)
                    writeEntry(entry);
            }

            if (!shaderTable->callableShaders.empty())
            {
                drd.CallableShaderTable.StartAddress = gpuVA;
                drd.CallableShaderTable.StrideInBytes = (shaderTable->callableShaders.size() == 1) ? 0 : entrySize;
                drd.CallableShaderTable.SizeInBytes = uint32_t(shaderTable->callableShaders.size()) * entrySize;

                for (auto& entry : shaderTable->callableShaders)
                    writeEntry(entry);
            }

            shaderTableState->committedVersion = shaderTable->version;
            shaderTableState->descriptorHeapSRV = m_device->m_dhSRVetc.GetShaderVisibleHeap();
            shaderTableState->descriptorHeapSamplers = m_device->m_dhSamplers.GetShaderVisibleHeap();

            // AddRef the shaderTable only on the first use / build because build happens at least once per CL anyway
            m_Instance->referencedResources.push_back(shaderTable);
        }

        bool updatePipeline = !m_CurrentRayTracingStateValid || m_CurrentRayTracingState.shaderTable->GetPipeline() != pso;
        bool updateBindings = updatePipeline || arraysAreDifferent(m_CurrentRayTracingState.bindings, state.bindings);

        if (commitDescriptorHeaps())
        {
            updateBindings = true;
        }

        if (updatePipeline)
        {
            m_ActiveCommandList->commandList4->SetComputeRootSignature(pso->globalRootSignature->handle);
            m_ActiveCommandList->commandList4->SetPipelineState1(pso->pipelineState);

            m_Instance->referencedResources.push_back(pso);
        }

        if (updateBindings)
        {
            // TODO: verify that all layouts have corresponding binding sets

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < uint32_t(state.bindings.size()); bindingSetIndex++)
            {
                IBindingSet* _bindingSet = state.bindings[bindingSetIndex];

                m_currentComputeVolatileCBs.resize(0);

                if (!_bindingSet)
                    continue;

                BindingSet* bindingSet = static_cast<BindingSet*>(_bindingSet);
                const std::pair<RefCountPtr<BindingLayout>, RootParameterIndex>& layoutAndOffset = pso->globalRootSignature->pipelineLayouts[bindingSetIndex];

                CHECK_ERROR(layoutAndOffset.first == bindingSet->layout, "This binding set has been created for a different layout. Out-of-order binding?");
                RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                // Bind the volatile constant buffers
                for (const std::pair<RootParameterIndex, IBuffer*>& parameter : bindingSet->rootParametersVolatileCB[ShaderType::SHADER_ALL_GRAPHICS])
                {
                    RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                    if (parameter.second)
                    {
                        Buffer* buffer = static_cast<Buffer*>(parameter.second);

                        if (buffer->desc.isVolatile)
                        {
                            const BufferState* bufferState = getBufferStateTracking(buffer, true);

                            CHECK_ERROR(bufferState->volatileData, "Attempted use of a volatile buffer before it was written into");

                            m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, bufferState->volatileData);

                            m_currentComputeVolatileCBs.push_back(VolatileConstantBufferBinding{ rootParameterIndex, bufferState, bufferState->volatileData });
                        }
                        else
                        {
                            assert(buffer->gpuVA != 0);

                            m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                        }
                    }
                    else
                    {
                        // This can only happen as a result of an improperly built binding set. 
                        // Such binding set should fail to create.
                        m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, 0);
                    }
                }

                if (bindingSet->descriptorTablesValidSamplers[ShaderType::SHADER_ALL_GRAPHICS])
                {
                    m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                        rootParameterOffset + bindingSet->rootParameterIndicesSamplers[ShaderType::SHADER_ALL_GRAPHICS],
                        m_device->m_dhSamplers.GetGpuHandle(bindingSet->descriptorTablesSamplers[ShaderType::SHADER_ALL_GRAPHICS]));
                }

                if (bindingSet->descriptorTablesValidSRVetc[ShaderType::SHADER_ALL_GRAPHICS])
                {
                    m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                        rootParameterOffset + bindingSet->rootParameterIndicesSRVetc[ShaderType::SHADER_ALL_GRAPHICS],
                        m_device->m_dhSRVetc.GetGpuHandle(bindingSet->descriptorTablesSRVetc[ShaderType::SHADER_ALL_GRAPHICS]));
                }

                if (bindingSet->desc.trackLiveness)
                    m_Instance->referencedResources.push_back(bindingSet);

                bool indirectParamsTransitioned = false;
                for (auto& setup : bindingSet->barrierSetup)
                    setup(this, nullptr, indirectParamsTransitioned);
            }
        }

        m_CurrentComputeStateValid = false;
        m_CurrentGraphicsStateValid = false;
        m_CurrentRayTracingStateValid = true;
        m_CurrentRayTracingState = state;

        commitBarriers();
    }

    void CommandList::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        updateComputeVolatileBuffers();

        if (!m_CurrentRayTracingStateValid)
        {
            SIGNAL_ERROR("setRayTracingState must be called before dispatchRays");
            return;
        }

        dxr::ShaderTableState* shaderTableState = getShaderTableStateTracking(m_CurrentRayTracingState.shaderTable);

        D3D12_DISPATCH_RAYS_DESC desc = shaderTableState->dispatchRaysTemplate;
        desc.Width = args.width;
        desc.Height = args.height;
        desc.Depth = args.depth;

        m_ActiveCommandList->commandList4->DispatchRays(&desc);
    }

    void CommandList::buildBottomLevelAccelStruct(rt::IAccelStruct* _as, const rt::BottomLevelAccelStructDesc& desc)
    {
        dxr::AccelStruct* as = static_cast<dxr::AccelStruct*>(_as);

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3dGeometryDescs;
        d3dGeometryDescs.resize(desc.triangles.size());

        for (uint32_t i = 0; i < desc.triangles.size(); i++)
        {
            const auto& geometryDesc = desc.triangles[i];
            auto& d3dGeometryDesc = d3dGeometryDescs[i];

            d3dGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            d3dGeometryDesc.Flags = (D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryDesc.flags;
            d3dGeometryDesc.Triangles.IndexBuffer = static_cast<Buffer*>(geometryDesc.indexBuffer)->gpuVA + geometryDesc.indexOffset;
            d3dGeometryDesc.Triangles.VertexBuffer.StartAddress = static_cast<Buffer*>(geometryDesc.vertexBuffer)->gpuVA + geometryDesc.vertexOffset;
            d3dGeometryDesc.Triangles.VertexBuffer.StrideInBytes = geometryDesc.vertexStride;
            d3dGeometryDesc.Triangles.IndexFormat = GetFormatMapping(geometryDesc.indexFormat).srvFormat;
            d3dGeometryDesc.Triangles.VertexFormat = GetFormatMapping(geometryDesc.vertexFormat).srvFormat;
            d3dGeometryDesc.Triangles.IndexCount = geometryDesc.indexCount;
            d3dGeometryDesc.Triangles.VertexCount = geometryDesc.vertexCount;

            if (geometryDesc.useTransform)
            {
                void* cpuVA = 0;
                D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
                if (!m_upload.SuballocateBuffer(sizeof(rt::AffineTransform), nullptr, nullptr, &cpuVA, &gpuVA,
                    m_recordingInstanceID, m_completedInstanceID, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
                {
                    CHECK_ERROR(false, "Couldn't suballocate an upload buffer");
                    return;
                }

                memcpy(cpuVA, &geometryDesc.transform, sizeof(rt::AffineTransform));

                d3dGeometryDesc.Triangles.Transform3x4 = gpuVA;
            }

            requireBufferState(geometryDesc.indexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            requireBufferState(geometryDesc.vertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            m_Instance->referencedResources.push_back(geometryDesc.indexBuffer);
            m_Instance->referencedResources.push_back(geometryDesc.vertexBuffer);
        }

        commitBarriers();

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (!m_dxrScratch.SuballocateBuffer(m_ActiveCommandList->commandList, as->scratchBufferSize, &scratchGpuVA, 
            m_recordingInstanceID, m_completedInstanceID, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
        {
            char message[256];
            float MB = 1.f / (1024.f * 1024.f);

            sprintf_s(message, "Couldn't suballocate a scratch buffer for DXR acceleration structure build. "
                "Requested size: %.1f MB, memory limit: %.1f MB, allocated: %.1f MB", 
                float(as->scratchBufferSize) * MB, float(m_dxrScratch.GetMaxTotalMemory()) * MB, float(m_dxrScratch.GetAllocatedMemory()) * MB);

            SIGNAL_ERROR(message);
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        buildDesc.Inputs.pGeometryDescs = d3dGeometryDescs.data();
        buildDesc.Inputs.NumDescs = UINT(d3dGeometryDescs.size());
        buildDesc.Inputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = as->dataBuffer->gpuVA;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = as->dataBuffer->resource;
        m_ActiveCommandList->commandList->ResourceBarrier(1, &barrier);

        m_ActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        m_ActiveCommandList->commandList->ResourceBarrier(1, &barrier);
    }

    void CommandList::buildTopLevelAccelStruct(rt::IAccelStruct* _as, const rt::TopLevelAccelStructDesc& desc)
    {
        dxr::AccelStruct* as = static_cast<dxr::AccelStruct*>(_as);

        as->bottomLevelASes.clear();

        D3D12_RAYTRACING_INSTANCE_DESC* cpuVA = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
        if (!m_upload.SuballocateBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * desc.instances.size(), nullptr, nullptr, (void**)&cpuVA, &gpuVA,
            m_recordingInstanceID, m_completedInstanceID, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
        {
            CHECK_ERROR(false, "Couldn't suballocate an upload buffer");
            return;
        }

        for (uint32_t i = 0; i < desc.instances.size(); i++)
        {
            const rt::InstanceDesc& instance = desc.instances[i];

            dxr::AccelStruct* blas = static_cast<dxr::AccelStruct*>(instance.bottomLevelAS);
            if (blas->trackLiveness)
                as->bottomLevelASes.push_back(blas);

            cpuVA->AccelerationStructure = blas->dataBuffer->gpuVA;
            cpuVA->Flags = (D3D12_RAYTRACING_INSTANCE_FLAGS)instance.flags;
            cpuVA->InstanceID = instance.instanceID;
            cpuVA->InstanceContributionToHitGroupIndex = instance.instanceContributionToHitGroupIndex;
            cpuVA->InstanceMask = instance.instanceMask;
            memcpy(&cpuVA->Transform, instance.transform, sizeof(rt::AffineTransform));

            ++cpuVA;
        }

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (!m_dxrScratch.SuballocateBuffer(m_ActiveCommandList->commandList, as->scratchBufferSize, &scratchGpuVA,
            m_recordingInstanceID, m_completedInstanceID, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
        {
            SIGNAL_ERROR("Couldn't suballocate a scratch buffer");
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        buildDesc.Inputs.InstanceDescs = gpuVA;
        buildDesc.Inputs.NumDescs = UINT(desc.instances.size());
        buildDesc.Inputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = as->dataBuffer->gpuVA;


        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = as->dataBuffer->resource;
        m_ActiveCommandList->commandList->ResourceBarrier(1, &barrier);

        m_ActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        m_ActiveCommandList->commandList->ResourceBarrier(1, &barrier);
    }

} // namespace d3d12
#else // NVRHI_WITH_DXR == 0

namespace d3d12
{

    rt::AccelStructHandle Device::createBottomLevelAccelStruct(const rt::BottomLevelAccelStructDesc& desc)
    {
        assert(!"DXR is not supported in this build");
        (void)desc;
        return nullptr;
    }

    rt::AccelStructHandle Device::createTopLevelAccelStruct(uint32_t numInstances, rt::AccelStructBuildFlags::Enum buildFlags)
    {
        assert(!"DXR is not supported in this build");
        (void)numInstances;
        (void)buildFlags;
        return nullptr;
    }

    rt::PipelineHandle Device::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        assert(!"DXR is not supported in this build");
        (void)desc;
        return nullptr;
    }

    void CommandList::setRayTracingState(const rt::State& state)
    {
        assert(!"DXR is not supported in this build");
        (void)state;
    }

    void CommandList::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        assert(!"DXR is not supported in this build");
        (void)args;
    }

    void CommandList::buildBottomLevelAccelStruct(rt::IAccelStruct* _as, const rt::BottomLevelAccelStructDesc& desc)
    {
        assert(!"DXR is not supported in this build");
        (void)_as;
        (void)desc;
    }

    void CommandList::buildTopLevelAccelStruct(rt::IAccelStruct* _as, const rt::TopLevelAccelStructDesc& desc)
    {
        assert(!"DXR is not supported in this build");
        (void)_as;
        (void)desc;
    }

} // namespace d3d12
#endif // NVRHI_WITH_DXR
} // namespace nvrhi
