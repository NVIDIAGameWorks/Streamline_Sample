/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/validation/validation.h>
#include <sstream>

namespace nvrhi 
{
namespace validation
{

    DeviceWrapper::DeviceWrapper(IDevice* device)
        : m_Device(device)
        , m_MessageCallback(device->getMessageCallback())
    {

    }

    void DeviceWrapper::message(MessageSeverity severity, const char* messageText, const char* file /*= nullptr*/, int line /*= 0*/)
    {
        m_MessageCallback->message(severity, messageText, file, line);
    }

    Object DeviceWrapper::getNativeObject(ObjectType objectType)
    {
        return m_Device->getNativeObject(objectType);
    }

    const char* TextureDimensionToString(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::Texture1D:           return "Texture1D";
        case TextureDimension::Texture1DArray:      return "Texture1DArray";
        case TextureDimension::Texture2D:           return "Texture2D";
        case TextureDimension::Texture2DArray:      return "Texture2DArray";
        case TextureDimension::TextureCube:         return "TextureCube";
        case TextureDimension::TextureCubeArray:    return "TextureCubeArray";
        case TextureDimension::Texture2DMS:         return "Texture2DMS";
        case TextureDimension::Texture2DMSArray:    return "Texture2DMSArray";
        case TextureDimension::Texture3D:           return "Texture3D";
        default:                                    return "Unknown";
        }
    }

    TextureHandle DeviceWrapper::createTexture(const TextureDesc& d)
    {
        char messageBuffer[256];
        bool anyErrors = false;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
        case TextureDimension::Texture3D:
            break;

        default:
            message(MessageSeverity::Error, "Unknown texture dimension");
            return nullptr;
        }

        const char* dimensionStr = TextureDimensionToString(d.dimension);

        if (d.width == 0 || d.height == 0 || d.depth == 0 || d.arraySize == 0 || d.mipLevels == 0)
        {
            snprintf(messageBuffer, std::size(messageBuffer), "%s: width (%d), height (%d), depth (%d), arraySize (%d) and mipLevels (%d) must not be zero",
                dimensionStr, d.width, d.height, d.depth, d.arraySize, d.mipLevels);
            return nullptr;
        }

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
            if (d.height != 1)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: height (%d) must be equal to 1", dimensionStr, d.height);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            if (d.depth != 1)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: depth (%d) must be equal to 1", dimensionStr, d.depth);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture3D:
            if (d.arraySize != 1)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: arraySize (%d) must be equal to 1", dimensionStr, d.arraySize);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        case TextureDimension::TextureCube:
            if (d.arraySize != 6)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: arraySize (%d) must be equal to 6", dimensionStr, d.arraySize);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        case TextureDimension::TextureCubeArray:
            if ((d.arraySize % 6) != 0)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: arraySize (%d) must be a multiple of 6", dimensionStr, d.arraySize);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture3D:
            if (d.sampleCount != 1)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: sampleCount (%d) must be equal to 1", dimensionStr, d.sampleCount);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            if (d.sampleCount != 2 && d.sampleCount != 4 && d.sampleCount != 8)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "%s: sampleCount (%d) must be equal to 2, 4 or 8", dimensionStr, d.sampleCount);
                message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
            break;
        default:;
        }

        if(anyErrors)
            return nullptr;

        return m_Device->createTexture(d);
    }

    TextureHandle DeviceWrapper::createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc)
    {
        return m_Device->createHandleForNativeTexture(objectType, texture, desc);
    }

    StagingTextureHandle DeviceWrapper::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        return m_Device->createStagingTexture(d, cpuAccess);
    }

    void * DeviceWrapper::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        return m_Device->mapStagingTexture(tex, slice, cpuAccess, outRowPitch);
    }

    void DeviceWrapper::unmapStagingTexture(IStagingTexture* tex)
    {
        m_Device->unmapStagingTexture(tex);
    }

    BufferHandle DeviceWrapper::createBuffer(const BufferDesc& d)
    {
        if (d.isVolatile && !d.isConstantBuffer)
        {
            message(MessageSeverity::Error, "createBuffer: Volatile buffers must be constant buffers");
            return nullptr;
        }

        return m_Device->createBuffer(d);
    }

    void * DeviceWrapper::mapBuffer(IBuffer* b, CpuAccessMode mapFlags)
    {
        return m_Device->mapBuffer(b, mapFlags);
    }

    void DeviceWrapper::unmapBuffer(IBuffer* b)
    {
        m_Device->unmapBuffer(b);
    }

    BufferHandle DeviceWrapper::createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)
    {
        return m_Device->createHandleForNativeBuffer(objectType, buffer, desc);
    }

    ShaderHandle DeviceWrapper::createShader(const ShaderDesc& d, const void* binary, const size_t binarySize)
    {
        return m_Device->createShader(d, binary, binarySize);
    }

    ShaderHandle DeviceWrapper::createShaderPermutation(const ShaderDesc& d, const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound /*= true*/)
    {
        return m_Device->createShaderPermutation(d, blob, blobSize, constants, numConstants, errorIfNotFound);
    }

    nvrhi::ShaderLibraryHandle DeviceWrapper::createShaderLibrary(const void* binary, const size_t binarySize)
    {
        return m_Device->createShaderLibrary(binary, binarySize);
    }

    nvrhi::ShaderLibraryHandle DeviceWrapper::createShaderLibraryPermutation(const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound /*= true*/)
    {
        return m_Device->createShaderLibraryPermutation(blob, blobSize, constants, numConstants, errorIfNotFound);
    }

    SamplerHandle DeviceWrapper::createSampler(const SamplerDesc& d)
    {
        return m_Device->createSampler(d);
    }

    InputLayoutHandle DeviceWrapper::createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader)
    {
        return m_Device->createInputLayout(d, attributeCount, vertexShader);
    }

    EventQueryHandle DeviceWrapper::createEventQuery(void)
    {
        return m_Device->createEventQuery();
    }

    void DeviceWrapper::setEventQuery(IEventQuery* query)
    {
        m_Device->setEventQuery(query);
    }

    bool DeviceWrapper::pollEventQuery(IEventQuery* query)
    {
        return m_Device->pollEventQuery(query);
    }

    void DeviceWrapper::waitEventQuery(IEventQuery* query)
    {
        m_Device->waitEventQuery(query);
    }

    void DeviceWrapper::resetEventQuery(IEventQuery* query)
    {
        m_Device->resetEventQuery(query);
    }

    TimerQueryHandle DeviceWrapper::createTimerQuery(void)
    {
        return m_Device->createTimerQuery();
    }

    bool DeviceWrapper::pollTimerQuery(ITimerQuery* query)
    {
        return m_Device->pollTimerQuery(query);
    }

    float DeviceWrapper::getTimerQueryTime(ITimerQuery* query)
    {
        return m_Device->getTimerQueryTime(query);
    }

    void DeviceWrapper::resetTimerQuery(ITimerQuery* query)
    {
        return m_Device->resetTimerQuery(query);
    }

    GraphicsAPI DeviceWrapper::getGraphicsAPI()
    {
        return m_Device->getGraphicsAPI();
    }

    FramebufferHandle DeviceWrapper::createFramebuffer(const FramebufferDesc& desc)
    {
        return m_Device->createFramebuffer(desc);
    }

    template<typename DescType>
    void FillShaderBindingSetFromDesc(IMessageCallback* messageCallback, const DescType& desc, ShaderBindingSet& bindingSet, ShaderBindingSet& duplicates)
    {
        for (const auto& item : desc)
        {
            if (item.registerSpace != 0)
            {
                // TODO: add validation for register spaces. Currently everything outside of space 0 is ignored.
                continue;
            }

            switch (item.type)
            {
            case ResourceType::Texture_SRV:
            case ResourceType::Buffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
                if (bindingSet.SRV[item.slot])
                {
                    duplicates.SRV[item.slot] = true;
                }
                else
                {
                    bindingSet.SRV[item.slot] = true;
                    bindingSet.rangeSRV.add(item.slot);
                }
                break;

            case ResourceType::Texture_UAV:
            case ResourceType::Buffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
                if (bindingSet.UAV[item.slot])
                {
                    duplicates.UAV[item.slot] = true;
                }
                else
                {
                    bindingSet.UAV[item.slot] = true;
                    bindingSet.rangeUAV.add(item.slot);
                }
                break;

            case ResourceType::ConstantBuffer:
            case ResourceType::VolatileConstantBuffer:
                if (bindingSet.CB[item.slot])
                {
                    duplicates.CB[item.slot] = true;
                }
                else
                {
                    bindingSet.CB[item.slot] = true;

                    if (item.type == ResourceType::VolatileConstantBuffer)
                        ++bindingSet.numVolatileCBs;

                    bindingSet.rangeCB.add(item.slot);
                }
                break;

            case ResourceType::Sampler:
                if (bindingSet.Sampler[item.slot])
                {
                    duplicates.Sampler[item.slot] = true;
                }
                else
                {
                    bindingSet.Sampler[item.slot] = true;
                    bindingSet.rangeSampler.add(item.slot);
                }
                break;

            default: {
                char buf[256];
                snprintf(buf, std::size(buf), "Invalid layout item type %d", (int)item.type);
                messageCallback->message(MessageSeverity::Error, buf);
                break;
            }
            }
        }
    }

    template<size_t N>
    void BitsetToStream(const std::bitset<N>& bits, std::ostream& os, const char* prefix, bool &first)
    {
        if (bits.any())
        {
            for (uint32_t slot = 0; slot < bits.size(); slot++)
            {
                if (bits[slot])
                {
                    if (!first)
                        os << ", ";
                    os << prefix << slot;
                    first = false;
                }
            }
        }
    }

    const char* ShaderStageToString(ShaderType::Enum stage)
    {
        switch (stage)
        {
        case nvrhi::ShaderType::SHADER_VERTEX: return "VERTEX";
        case nvrhi::ShaderType::SHADER_HULL: return "HULL";
        case nvrhi::ShaderType::SHADER_DOMAIN: return "DOMAIN";
        case nvrhi::ShaderType::SHADER_GEOMETRY: return "GEOMETRY";
        case nvrhi::ShaderType::SHADER_PIXEL: return "PIXEL";
        case nvrhi::ShaderType::SHADER_COMPUTE: return "COMPUTE";
        default: return "<INVALID>";
        }
    }

    template<typename ItemType, typename DescType>
    const ItemType* SelectShaderStage(const DescType& desc, ShaderType::Enum stage)
    {
        switch (stage)
        {
        case ShaderType::SHADER_VERTEX: return &desc.VS;
        case ShaderType::SHADER_HULL: return &desc.HS;
        case ShaderType::SHADER_DOMAIN: return &desc.DS;
        case ShaderType::SHADER_GEOMETRY: return &desc.GS;
        case ShaderType::SHADER_PIXEL: return &desc.PS;
        case ShaderType::SHADER_COMPUTE: return &desc.CS;
        default:
            assert(!"Invalid shader stage");
            return nullptr;
        }
    }

    template<typename ItemType, typename DescType>
    const ItemType* SelectGraphicsShaderStage(const DescType& desc, ShaderType::Enum stage)
    {
        switch (stage)
        {
        case ShaderType::SHADER_VERTEX: return &desc.VS;
        case ShaderType::SHADER_HULL: return &desc.HS;
        case ShaderType::SHADER_DOMAIN: return &desc.DS;
        case ShaderType::SHADER_GEOMETRY: return &desc.GS;
        case ShaderType::SHADER_PIXEL: return &desc.PS;
        default: 
            assert(!"Invalid shader stage");
            return nullptr;
        }
    }

    static const ShaderType::Enum g_ShaderStages[] = {
        ShaderType::SHADER_VERTEX,
        ShaderType::SHADER_HULL,
        ShaderType::SHADER_DOMAIN,
        ShaderType::SHADER_GEOMETRY,
        ShaderType::SHADER_PIXEL,
        ShaderType::SHADER_COMPUTE
    };

    bool ValidatePipelineBindingLayouts(IMessageCallback* messageCallback, const static_vector<BindingLayoutHandle, MaxBindingLayouts>& bindingLayouts, const std::array<IShader*, 7>& shaders)
    {
        const int numBindingLayouts = int(bindingLayouts.size());
        bool anyErrors = false;
        bool anyDuplicateBindings = false;
        bool anyOverlappingBindings = false;
        char messageBuffer[256];
        std::stringstream ssDuplicateBindings;
        std::stringstream ssOverlappingBindings;

        for (ShaderType::Enum stage : g_ShaderStages)
        {
            IShader* shader = shaders[stage];

            static_vector<ShaderBindingSet, MaxBindingLayouts> bindingsPerLayout;
            static_vector<ShaderBindingSet, MaxBindingLayouts> duplicatesPerLayout;
            bindingsPerLayout.resize(numBindingLayouts);
            duplicatesPerLayout.resize(numBindingLayouts);

            // Accumulate binding information about the stage from all layouts

            for (int layoutIndex = 0; layoutIndex < numBindingLayouts; layoutIndex++)
            {
                if (bindingLayouts[layoutIndex] == nullptr)
                {
                    snprintf(messageBuffer, std::size(messageBuffer), "Binding layout in slot %d is NULL", layoutIndex);
                    messageCallback->message(MessageSeverity::Error, messageBuffer);
                    anyErrors = true;
                }
                else
                {
                    const BindingLayoutDesc& layoutDesc = bindingLayouts[layoutIndex]->GetDesc();
                    const StageBindingLayoutDesc& stageLayoutDesc = *SelectShaderStage<StageBindingLayoutDesc, BindingLayoutDesc>(layoutDesc, stage);

                    FillShaderBindingSetFromDesc(messageCallback, stageLayoutDesc, bindingsPerLayout[layoutIndex], duplicatesPerLayout[layoutIndex]);

                    // Layouts with duplicates should not have passed validation in createBindingLayout
                    assert(!duplicatesPerLayout[layoutIndex].any());
                }
            }

            // Check for bindings to an unused shader stage

            if (shader == nullptr)
            {
                for (int layoutIndex = 0; layoutIndex < numBindingLayouts; layoutIndex++)
                {
                    if (bindingsPerLayout[layoutIndex].any())
                    {
                        snprintf(messageBuffer, std::size(messageBuffer),
                            "Binding layout in slot %d has bindings for %s shader, which is not used in the pipeline",
                            layoutIndex, ShaderStageToString(stage));

                        messageCallback->message(MessageSeverity::Error, messageBuffer);
                        anyErrors = true;
                    }
                }
            }

            // Check for multiple layouts declaring the same bindings

            if (numBindingLayouts > 1)
            {
                ShaderBindingSet bindings = bindingsPerLayout[0];
                ShaderBindingSet duplicates;

                for (int layoutIndex = 1; layoutIndex < numBindingLayouts; layoutIndex++)
                {
                    duplicates.SRV |= bindings.SRV & bindingsPerLayout[layoutIndex].SRV;
                    duplicates.Sampler |= bindings.Sampler & bindingsPerLayout[layoutIndex].Sampler;
                    duplicates.UAV |= bindings.UAV & bindingsPerLayout[layoutIndex].UAV;
                    duplicates.CB |= bindings.CB & bindingsPerLayout[layoutIndex].CB;

                    bindings.SRV |= bindingsPerLayout[layoutIndex].SRV;
                    bindings.Sampler |= bindingsPerLayout[layoutIndex].Sampler;
                    bindings.UAV |= bindingsPerLayout[layoutIndex].UAV;
                    bindings.CB |= bindingsPerLayout[layoutIndex].CB;
                }

                if (duplicates.any())
                {
                    if (!anyDuplicateBindings)
                        ssDuplicateBindings << "Same bindings defined by more than one layout in this pipeline:";

                    ssDuplicateBindings << std::endl << ShaderStageToString(stage) << ": " << duplicates;

                    anyDuplicateBindings = true;
                }
                else
                {
                    // Check for overlapping layouts.
                    // Do this only when there are no duplicates, as with duplicates the layouts will always overlap.

                    bool overlapSRV = false;
                    bool overlapSampler = false;
                    bool overlapUAV = false;
                    bool overlapCB = false;

                    for (int i = 0; i < numBindingLayouts - 1; i++)
                    {
                        const ShaderBindingSet& set1 = bindingsPerLayout[i];

                        for (int j = i + 1; j < numBindingLayouts; j++)
                        {
                            const ShaderBindingSet& set2 = bindingsPerLayout[j];

                            overlapSRV = overlapSRV || set1.rangeSRV.overlapsWith(set2.rangeSRV);
                            overlapSampler = overlapSampler || set1.rangeSampler.overlapsWith(set2.rangeSampler);
                            overlapUAV = overlapUAV || set1.rangeUAV.overlapsWith(set2.rangeUAV);
                            overlapCB = overlapCB || set1.rangeCB.overlapsWith(set2.rangeCB);
                        }
                    }

                    if (overlapSRV || overlapSampler || overlapUAV || overlapCB)
                    {
                        if (!anyOverlappingBindings)
                            ssOverlappingBindings << "Binding layouts have overlapping register ranges:";

                        ssOverlappingBindings << std::endl << ShaderStageToString(stage) << ": ";

                        bool first = true;
                        auto append = [&first, &ssOverlappingBindings](bool value, const char* text)
                        {
                            if (value)
                            {
                                if (!first) ssOverlappingBindings << ", ";
                                ssOverlappingBindings << text;
                                first = false;
                            }
                        };

                        append(overlapSRV, "SRV");
                        append(overlapSampler, "Sampler");
                        append(overlapUAV, "UAV");
                        append(overlapCB, "CB");

                        anyOverlappingBindings = true;
                    }
                }
            }
        }

        if (anyDuplicateBindings)
        {
            messageCallback->message(MessageSeverity::Error, ssDuplicateBindings.str().c_str());
            anyErrors = true;
        }

        if (anyOverlappingBindings)
        {
            messageCallback->message(MessageSeverity::Error, ssOverlappingBindings.str().c_str());
            anyErrors = true;
        }

        return !anyErrors;
    }

    GraphicsPipelineHandle DeviceWrapper::createGraphicsPipeline(const GraphicsPipelineDesc& pipelineDesc, IFramebuffer* fb)
    {
        std::array<IShader*, 7> shaders;

        for (ShaderType::Enum stage : g_ShaderStages)
        {
            if (stage != ShaderType::SHADER_COMPUTE)
                shaders[stage] = *SelectGraphicsShaderStage<ShaderHandle, GraphicsPipelineDesc>(pipelineDesc, stage);
            else
                shaders[stage] = nullptr;
        }

        if (!ValidatePipelineBindingLayouts(m_MessageCallback, pipelineDesc.bindingLayouts, shaders))
            return nullptr;

        return m_Device->createGraphicsPipeline(pipelineDesc, fb);
    }

    ComputePipelineHandle DeviceWrapper::createComputePipeline(const ComputePipelineDesc& pipelineDesc)
    {
        std::array<IShader*, 7> shaders;

        for (ShaderType::Enum stage : g_ShaderStages)
        {
            if (stage == ShaderType::SHADER_COMPUTE)
                shaders[stage] = pipelineDesc.CS;
            else
                shaders[stage] = nullptr;
        }

        if (!ValidatePipelineBindingLayouts(m_MessageCallback, pipelineDesc.bindingLayouts, shaders))
            return nullptr;

        return m_Device->createComputePipeline(pipelineDesc);
    }

    nvrhi::rt::PipelineHandle DeviceWrapper::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        return m_Device->createRayTracingPipeline(desc);
    }

    BindingLayoutHandle DeviceWrapper::createBindingLayout(const BindingLayoutDesc& desc)
    {
        std::stringstream ssDuplicates;
        bool anyDuplicates = false;
        uint32_t numVolatileCBs = 0;

        auto validateStage = [this, &ssDuplicates, &anyDuplicates, &numVolatileCBs](const StageBindingLayoutDesc& stageDesc, const char* stageName)
        {
            ShaderBindingSet bindings;
            ShaderBindingSet duplicates;

            FillShaderBindingSetFromDesc(m_MessageCallback, stageDesc, bindings, duplicates);

            if (duplicates.any())
            {
                if(!anyDuplicates)
                    ssDuplicates << "Binding layout contains duplicate bindings:";

                ssDuplicates << std::endl << stageName << ": " << duplicates;
                
                anyDuplicates = true;
            }

            numVolatileCBs += bindings.numVolatileCBs;
        };

        validateStage(desc.VS, "VS");
        validateStage(desc.HS, "HS");
        validateStage(desc.DS, "DS");
        validateStage(desc.GS, "GS");
        validateStage(desc.PS, "PS");
        validateStage(desc.CS, "CS");

        bool anyErrors = false;

        if (anyDuplicates)
        {
            message(MessageSeverity::Error, ssDuplicates.str().c_str());
            anyErrors = true;
        }

        size_t numGraphicsBindings = desc.VS.size() + desc.HS.size() + desc.DS.size() + desc.GS.size() + desc.PS.size();
        size_t numComputeBindings = desc.CS.size();

        if (numGraphicsBindings > 0 && numComputeBindings > 0)
        {
            message(MessageSeverity::Error, "Binding layout contains both graphics and compute bindings");
            anyErrors = true;
        }

        if (numVolatileCBs > MaxVolatileConstantBuffersPerLayout)
        {
            message(MessageSeverity::Error, "Binding layout contains too many volatile CBs");
            anyErrors = true;
        }

        if (anyErrors)
            return nullptr;

        return m_Device->createBindingLayout(desc);
    }

    BindingSetHandle DeviceWrapper::createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)
    {
        if (layout == nullptr)
        {
            message(MessageSeverity::Error, "Cannot create a binding set without a valid layout");
            return nullptr;
        }

        std::stringstream ssDeclaredNotBound;
        std::stringstream ssBoundNotDeclared;
        std::stringstream ssDuplicates;
        bool anyDeclaredNotBound = false;
        bool anyBoundNotDeclared = false;
        bool anyDuplicates = false;

        auto validateStage = [this, &ssDeclaredNotBound, &ssBoundNotDeclared, &ssDuplicates, &anyDeclaredNotBound, &anyBoundNotDeclared, &anyDuplicates]
            (const StageBindingLayoutDesc& layoutDesc, const StageBindingSetDesc& setDesc, const char* stageName)
        {
            ShaderBindingSet layoutBindings;
            ShaderBindingSet layoutDuplicates;

            FillShaderBindingSetFromDesc(m_MessageCallback, layoutDesc, layoutBindings, layoutDuplicates);

            ShaderBindingSet setBindings;
            ShaderBindingSet setDuplicates;

            FillShaderBindingSetFromDesc(m_MessageCallback, setDesc, setBindings, setDuplicates);

            ShaderBindingSet declaredNotBound;
            ShaderBindingSet boundNotDeclared;

            declaredNotBound.SRV = layoutBindings.SRV & ~setBindings.SRV;
            declaredNotBound.Sampler = layoutBindings.Sampler & ~setBindings.Sampler;
            declaredNotBound.UAV = layoutBindings.UAV & ~setBindings.UAV;
            declaredNotBound.CB = layoutBindings.CB & ~setBindings.CB;

            boundNotDeclared.SRV = ~layoutBindings.SRV & setBindings.SRV;
            boundNotDeclared.Sampler = ~layoutBindings.Sampler & setBindings.Sampler;
            boundNotDeclared.UAV = ~layoutBindings.UAV & setBindings.UAV;
            boundNotDeclared.CB = ~layoutBindings.CB & setBindings.CB;

            if (declaredNotBound.any())
            {
                if (!anyDeclaredNotBound)
                    ssDeclaredNotBound << "Bindings declared in the layout are not present in the binding set:";

                ssDeclaredNotBound << std::endl << stageName << ": " << declaredNotBound;

                anyDeclaredNotBound = true;
            }

            if (boundNotDeclared.any())
            {
                if (!anyBoundNotDeclared)
                    ssBoundNotDeclared << "Bindings in the binding set are not declared in the layout:";

                ssBoundNotDeclared << std::endl << stageName << ": " << boundNotDeclared;

                anyBoundNotDeclared = true;
            }

            if (setDuplicates.any())
            {
                if (!anyDuplicates)
                    ssDuplicates << "Binding set contains duplicate bindings:";

                ssDuplicates << std::endl << stageName << ": " << setDuplicates;

                anyDuplicates = true;
            }
        };

        validateStage(layout->GetDesc().VS, desc.VS, "VS");
        validateStage(layout->GetDesc().HS, desc.HS, "HS");
        validateStage(layout->GetDesc().DS, desc.DS, "DS");
        validateStage(layout->GetDesc().GS, desc.GS, "GS");
        validateStage(layout->GetDesc().PS, desc.PS, "PS");
        validateStage(layout->GetDesc().CS, desc.CS, "CS");

        bool anyErrors = false;

        if (anyDeclaredNotBound)
        {
            message(MessageSeverity::Error, ssDeclaredNotBound.str().c_str());
            anyErrors = true;
        }

        if (anyBoundNotDeclared)
        {
            message(MessageSeverity::Error, ssBoundNotDeclared.str().c_str());
            anyErrors = true;
        }

        if (anyDuplicates)
        {
            message(MessageSeverity::Error, ssDuplicates.str().c_str());
            anyErrors = true;
        }

        if (anyErrors)
            return nullptr;

        return m_Device->createBindingSet(desc, layout);
    }

    nvrhi::rt::AccelStructHandle DeviceWrapper::createBottomLevelAccelStruct(const rt::BottomLevelAccelStructDesc& desc)
    {
        return m_Device->createBottomLevelAccelStruct(desc);
    }

    nvrhi::rt::AccelStructHandle DeviceWrapper::createTopLevelAccelStruct(uint32_t numInstances, rt::AccelStructBuildFlags::Enum buildFlags)
    {
        return m_Device->createTopLevelAccelStruct(numInstances, buildFlags);
    }

    uint32_t DeviceWrapper::getNumberOfAFRGroups()
    {
        return m_Device->getNumberOfAFRGroups();
    }

    uint32_t DeviceWrapper::getAFRGroupOfCurrentFrame(uint32_t numAFRGroups)
    {
        return m_Device->getAFRGroupOfCurrentFrame(numAFRGroups);
    }

    CommandListHandle DeviceWrapper::createCommandList(const CommandListParameters& params)
    {
        CommandListHandle commandList = m_Device->createCommandList(params);

        if (commandList == nullptr)
            return nullptr;

        CommandListWrapper* wrapper = new CommandListWrapper(this, commandList, params.enableImmediateExecution);
        return CommandListHandle::Create(wrapper);
    }

    void DeviceWrapper::executeCommandList(ICommandList* commandList)
    {
        CommandListWrapper* wrapper = dynamic_cast<CommandListWrapper*>(commandList);

        if (wrapper)
        {
            if (!wrapper->requireExecuteState())
                return;

            m_Device->executeCommandList(wrapper->getUnderlyingCommandList());
        }
        else
            m_Device->executeCommandList(commandList);
    }

    void DeviceWrapper::waitForIdle()
    {
        m_Device->waitForIdle();
    }

    void DeviceWrapper::runGarbageCollection()
    {
        m_Device->runGarbageCollection();
    }

    bool DeviceWrapper::queryFeatureSupport(Feature feature)
    {
        return m_Device->queryFeatureSupport(feature);
    }

    IMessageCallback* DeviceWrapper::getMessageCallback()
    {
        return m_MessageCallback;
    }

    void Range::add(uint32_t item)
    {
        min = std::min(min, item);
        max = std::max(max, item);
    }

    bool Range::empty() const
    {
        return min > max;
    }

    bool Range::overlapsWith(const Range& other) const
    {
        return !empty() && !other.empty() && max >= other.min && min <= other.max;
    }

    bool ShaderBindingSet::any() const
    {
        return SRV.any() || Sampler.any() || UAV.any() || CB.any();
    }

    bool ShaderBindingSet::overlapsWith(const ShaderBindingSet& other) const
    {
        return rangeSRV.overlapsWith(other.rangeSRV)
            || rangeSampler.overlapsWith(other.rangeSampler)
            || rangeUAV.overlapsWith(other.rangeUAV)
            || rangeCB.overlapsWith(other.rangeCB);
    }

    std::ostream& operator<<(std::ostream& os, const ShaderBindingSet& set)
    {
        bool first = true;
        BitsetToStream(set.SRV, os, "t", first);
        BitsetToStream(set.Sampler, os, "s", first);
        BitsetToStream(set.UAV, os, "u", first);
        BitsetToStream(set.CB, os, "b", first);
        return os;
    }
} // namespace validation
} // namespace nvrhi
