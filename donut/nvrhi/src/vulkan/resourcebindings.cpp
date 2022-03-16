#include <nvrhi/vulkan.h>

//#define EVIL_DEBUG_NAMES

#ifdef EVIL_DEBUG_NAMES
#include "stacktracestring2.cpp"
#endif // EVIL_DEBUG_NAMES

namespace nvrhi 
{
namespace vulkan
{

uint32_t HLSLCompilerParameters::getBindingBase(vk::ShaderStageFlagBits shaderStage, RegisterOffset registerKind)
{
    // maps a shader stage to a per-stage binding location offset
    const static nvrhi::unordered_map<uint32_t, uint32_t> stageOffsetMap {
        { uint32_t(vk::ShaderStageFlagBits::eVertex),                 uint32_t(StageOffset::Vertex)         },
        { uint32_t(vk::ShaderStageFlagBits::eTessellationControl),    uint32_t(StageOffset::TessControl)    },
        { uint32_t(vk::ShaderStageFlagBits::eTessellationEvaluation), uint32_t(StageOffset::TessEval)       },
        { uint32_t(vk::ShaderStageFlagBits::eGeometry),               uint32_t(StageOffset::Geometry)       },
        { uint32_t(vk::ShaderStageFlagBits::eFragment),               uint32_t(StageOffset::Fragment)       },

        // compute is always offset 0 since no other stages can be bound at the same time
        { uint32_t(vk::ShaderStageFlagBits::eCompute),                0 },
    };

    auto iter = stageOffsetMap.find(uint32_t(shaderStage));
    assert(iter != stageOffsetMap.end());

    return iter->second + registerKind;
}

// emit a VK binding structure for a NVRHI binding type into a binding map
static void genBinding(ResourceBindingMap& bindingMap,
                       const BindingLayoutItem& bindingLayout,
                       HLSLCompilerParameters::RegisterOffset registerKind,
                       vk::ShaderStageFlagBits shaderStage,
                       vk::DescriptorType type)
{
    const auto baseOffset = HLSLCompilerParameters::getBindingBase(shaderStage, registerKind);
    const auto bindingLocation = baseOffset + bindingLayout.slot;

    assert(bindingMap.find({ bindingLayout.slot, bindingLayout.type }) == bindingMap.end());

    BindingLayoutVK binding(bindingLayout);
    binding.VK_location = bindingLocation;
    binding.descriptorSetLayoutBinding = vk::DescriptorSetLayoutBinding()
                                            .setBinding(bindingLocation)
                                            .setDescriptorCount(1)
                                            .setDescriptorType(type)
                                            .setStageFlags(shaderStage);

    bindingMap[{ bindingLayout.slot, bindingLayout.type }] = binding;
}

BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc)
{
    PipelineBindingLayout* ret = new PipelineBindingLayout(this, desc);

    ret->bake(context);

    // TODO: verify that none of the layout items have registerSpace != 0

    return BindingLayoutHandle::Create(ret);
}

PipelineBindingLayout::PipelineBindingLayout(Device* _parent, const BindingLayoutDesc& _desc)
    : parent(_parent)
    , refCount(1)
    , desc(_desc)
{
    collectBindings(desc.VS,  vk::ShaderStageFlagBits::eVertex, bindingMap_VS);
    collectBindings(desc.ALL, vk::ShaderStageFlagBits::eVertex, bindingMap_VS);
    collectBindings(desc.HS,  vk::ShaderStageFlagBits::eTessellationControl, bindingMap_HS);
    collectBindings(desc.ALL, vk::ShaderStageFlagBits::eTessellationControl, bindingMap_HS);
    collectBindings(desc.DS,  vk::ShaderStageFlagBits::eTessellationEvaluation, bindingMap_DS);
    collectBindings(desc.ALL, vk::ShaderStageFlagBits::eTessellationEvaluation, bindingMap_DS);
    collectBindings(desc.GS,  vk::ShaderStageFlagBits::eGeometry, bindingMap_GS);
    collectBindings(desc.ALL, vk::ShaderStageFlagBits::eGeometry, bindingMap_GS);
    collectBindings(desc.PS,  vk::ShaderStageFlagBits::eFragment, bindingMap_PS);
    collectBindings(desc.ALL, vk::ShaderStageFlagBits::eFragment, bindingMap_PS);
    collectBindings(desc.CS,  vk::ShaderStageFlagBits::eCompute, bindingMap_CS);
    // ALL is not included in CS.
}

void PipelineBindingLayout::collectBindings(const StageBindingLayoutDesc& bindingLayout, vk::ShaderStageFlagBits shaderStage, ResourceBindingMap& bindingMap)
{
    // iterate over all binding types and add to map
    for (const BindingLayoutItem& binding : bindingLayout)
    {
        HLSLCompilerParameters::RegisterOffset registerOffset;
        vk::DescriptorType descriptorType;

        switch (binding.type)
        {
        case ResourceType::Texture_SRV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::Texture;
            descriptorType = vk::DescriptorType::eSampledImage;
            break;

        case ResourceType::Texture_UAV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::UAV;
            descriptorType = vk::DescriptorType::eStorageImage;
            break;

        case ResourceType::Buffer_SRV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::Texture;
            descriptorType = vk::DescriptorType::eUniformTexelBuffer;
            break;

        case ResourceType::StructuredBuffer_SRV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::Texture;
            descriptorType = vk::DescriptorType::eStorageBuffer;
            break;

        case ResourceType::Buffer_UAV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::UAV;
            descriptorType = vk::DescriptorType::eStorageTexelBuffer;
            break;

        case ResourceType::StructuredBuffer_UAV:
            registerOffset = HLSLCompilerParameters::RegisterOffset::UAV;
            descriptorType = vk::DescriptorType::eStorageBuffer;
            break;

        case ResourceType::ConstantBuffer:
        case ResourceType::VolatileConstantBuffer:
            registerOffset = HLSLCompilerParameters::RegisterOffset::ConstantBuffer;
            descriptorType = vk::DescriptorType::eUniformBuffer;
            break;

        case ResourceType::Sampler:
            registerOffset = HLSLCompilerParameters::RegisterOffset::Sampler;
            descriptorType = vk::DescriptorType::eSampler;
            break;

        default:
            assert(!"Invalid binding.type");
            continue;
        }

        genBinding(bindingMap, binding, registerOffset, shaderStage, descriptorType);
    }
}

vk::Result PipelineBindingLayout::bake(VulkanContext& context)
{
    vk::Result res;

    // build a linear vector of all the bindings for this set
    nvrhi::vector<vk::DescriptorSetLayoutBinding> layoutBindings;

    auto collectBindings =
        [&](const ResourceBindingMap& map)
    {
        for (const auto& binding : map)
        {
            layoutBindings.push_back(binding.second.descriptorSetLayoutBinding);
        }
    };

    collectBindings(bindingMap_VS);
    collectBindings(bindingMap_HS);
    collectBindings(bindingMap_DS);
    collectBindings(bindingMap_GS);
    collectBindings(bindingMap_PS);
    collectBindings(bindingMap_CS);

    // if this assert fires, the set is empty, which is not allowed
    assert(layoutBindings.size());

    // create the descriptor set layout object
    
    auto descriptorSetLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
        .setBindingCount(uint32_t(layoutBindings.size()))
        .setPBindings(layoutBindings.data());

    res = context.device.createDescriptorSetLayout(&descriptorSetLayoutInfo,
        context.allocationCallbacks,
        &descriptorSetLayout);
    CHECK_VK_RETURN(res);
    parent->nameVKObject(descriptorSetLayout);

    // count the number of descriptors required per type
    nvrhi::unordered_map<vk::DescriptorType, uint32_t> poolSizeMap;
    for (auto layoutBinding : layoutBindings)
    {
        if (poolSizeMap.find(layoutBinding.descriptorType) == poolSizeMap.end())
        {
            poolSizeMap[layoutBinding.descriptorType] = 0;
        }

        poolSizeMap[layoutBinding.descriptorType] += layoutBinding.descriptorCount;
    }

    // compute descriptor pool size info
    for (auto poolSizeIter : poolSizeMap)
    {
        descriptorPoolSizeInfo.push_back(vk::DescriptorPoolSize()
            .setType(poolSizeIter.first)
            .setDescriptorCount(poolSizeIter.second));
    }

    return vk::Result::eSuccess;
}

unsigned long PipelineBindingLayout::Release()
{
    assert(refCount > 0);
    unsigned long result = --refCount;
    if (result == 0) parent->destroyPipelineBindingLayout(this);
    return result;
}

void Device::destroyPipelineBindingLayout(IBindingLayout* _layout)
{
    PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout);

    if (layout->descriptorSetLayout)
    {
        context.device.destroyDescriptorSetLayout(layout->descriptorSetLayout, context.allocationCallbacks);
        layout->descriptorSetLayout = vk::DescriptorSetLayout();
    }
}

// utility function to extract an element that is known to exist in a map
template <typename TVal>
static const TVal& find(uint32_t key, const nvrhi::unordered_map<uint32_t, TVal>& map)
{
    auto& iter = map.find(key);
    assert(iter != map.end());
    return iter->second;
}

BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout)
{
    vk::Result res;

    PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout);

    ResourceBindingSet *ret = new (nvrhi::HeapAlloc) ResourceBindingSet(this);
    ret->desc = desc;
    ret->layout = layout;

    const auto& descriptorSetLayout = layout->descriptorSetLayout;
    const auto& poolSizes = layout->descriptorPoolSizeInfo;

    // create descriptor pool to allocate a descriptor from
    auto poolInfo = vk::DescriptorPoolCreateInfo()
        .setPoolSizeCount(uint32_t(poolSizes.size()))
        .setPPoolSizes(poolSizes.data())
        .setMaxSets(1);

    res = context.device.createDescriptorPool(&poolInfo,
        context.allocationCallbacks,
        &ret->descriptorPool);
    CHECK_VK_FAIL(res);
    nameVKObject(ret->descriptorPool);

    // create the descriptor set
    auto descriptorSetAllocInfo = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(ret->descriptorPool)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&descriptorSetLayout);

    res = context.device.allocateDescriptorSets(&descriptorSetAllocInfo,
        &ret->descriptorSet);
    CHECK_VK_FAIL(res);
    nameVKObject(ret->descriptorSet);

    // collect all of the descriptor write data
    static_vector<vk::DescriptorImageInfo,
        MaxBindingsPerStage> descriptorImageInfo;
    static_vector<vk::DescriptorBufferInfo,
        MaxBindingsPerStage> descriptorBufferInfo;

    static_vector<vk::WriteDescriptorSet, MaxBindingsPerStage> descriptorWriteInfo;

    auto generateWriteDescriptorData =
        // generates a vk::WriteDescriptorSet struct in descriptorWriteInfo
        [&](uint32_t bindingLocation,
            vk::DescriptorType descriptorType,
            vk::DescriptorImageInfo *imageInfo,
            vk::DescriptorBufferInfo *bufferInfo,
            vk::BufferView *bufferView)
    {
        descriptorWriteInfo.push_back(
            vk::WriteDescriptorSet()
            .setDstSet(ret->descriptorSet)
            .setDstBinding(bindingLocation)
            .setDstArrayElement(0)
            .setDescriptorCount(1)
            .setDescriptorType(descriptorType)
            .setPImageInfo(imageInfo)
            .setPBufferInfo(bufferInfo)
            .setPTexelBufferView(bufferView)
        );
    };

    auto iterateOverStageBindings =
        // consumes all the bindings for a given StageBindingSetDesc structure
        [&](const ResourceBindingMap& bindingMap,
            const StageBindingSetDesc& bindingDesc)
    {
        for (const auto& binding : bindingDesc)
        {
            const auto& layoutIter = bindingMap.find({ binding.slot, binding.type });
            assert(layoutIter != bindingMap.end());
            const auto& layout = layoutIter->second;

            if (binding.resourceHandle == nullptr)
            {
                continue;
            }

            // We cast twice since the types in binding.resourceHandle can have multiple inheritance,
            // and static_cast is only guaranteed to give us the right pointer if we go down the
            // right inheritance chain...
            auto pResource = static_cast<IResource *>(binding.resourceHandle);

            switch (binding.type)
            {
            case ResourceType::Texture_SRV:
            {
                const auto& texture = static_cast<Texture *>(pResource);

                const auto subresource = binding.subresources.resolve(texture->desc, false);
                auto viewType = FormatIsStencil(binding.format) ? Texture::TextureSubresourceViewType::StencilOnly : Texture::TextureSubresourceViewType::DepthOnly;
                auto& view = texture->getSubresourceView(subresource, viewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

                generateWriteDescriptorData(layout.VK_location,
                    layout.descriptorSetLayoutBinding.descriptorType,
                    &imageInfo, nullptr, nullptr);
            }

            break;

            case ResourceType::Texture_UAV:
            {
                const auto texture = static_cast<Texture *>(pResource);

                const auto subresource = binding.subresources.resolve(texture->desc, true);
                auto viewType = Texture::TextureSubresourceViewType::AllAspects;
                auto& view = texture->getSubresourceView(subresource, viewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eGeneral);

                generateWriteDescriptorData(layout.VK_location,
                    layout.descriptorSetLayoutBinding.descriptorType,
                    &imageInfo, nullptr, nullptr);
            }

            break;

            case ResourceType::Buffer_SRV:
            case ResourceType::Buffer_UAV:
            {
                const auto& buffer = static_cast<Buffer *>(pResource);

                auto vkformat = nvrhi::vulkan::convertFormat(binding.format);

                const auto& bufferViewFound = buffer->viewCache.find(vkformat);
                auto& bufferViewRef = (bufferViewFound != buffer->viewCache.end()) ? bufferViewFound->second : buffer->viewCache[vkformat];
                if (bufferViewFound == buffer->viewCache.end())
                {
                    assert(binding.format != Format::UNKNOWN);
                    const auto range = binding.range.resolve(buffer->desc);

                    auto bufferViewInfo = vk::BufferViewCreateInfo()
                        .setBuffer(buffer->buffer)
                        .setOffset(range.byteOffset)
                        .setRange(range.byteSize)
                        .setFormat(vkformat);
                    auto res = context.device.createBufferView(&bufferViewInfo, context.allocationCallbacks, &bufferViewRef);
                    ASSERT_VK_OK(res);
                    nameVKObject(bufferViewRef);
                }

                generateWriteDescriptorData(layout.VK_location,
                    layout.descriptorSetLayoutBinding.descriptorType,
                    nullptr, nullptr, &bufferViewRef);
            }
            break;

            case ResourceType::StructuredBuffer_SRV:
            case ResourceType::StructuredBuffer_UAV:
            case ResourceType::ConstantBuffer:
            case ResourceType::VolatileConstantBuffer:
            {
                const auto buffer = static_cast<Buffer *>(pResource);

                const auto range = binding.range.resolve(buffer->desc);

                auto& bufferInfo = descriptorBufferInfo.emplace_back();
                bufferInfo = vk::DescriptorBufferInfo()
                    .setBuffer(buffer->buffer)
                    .setOffset(range.byteOffset)
                    .setRange(range.byteSize);

                assert(buffer->buffer);
                generateWriteDescriptorData(layout.VK_location,
                    layout.descriptorSetLayoutBinding.descriptorType,
                    nullptr, &bufferInfo, nullptr);
            }

            break;

            case ResourceType::Sampler:
            {
                const auto& sampler = static_cast<Sampler *>(pResource);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setSampler(sampler->sampler);

                generateWriteDescriptorData(layout.VK_location,
                    layout.descriptorSetLayoutBinding.descriptorType,
                    &imageInfo, nullptr, nullptr);
            }

            break;

			default:
			{
				assert(!"unhandled resource type in binding mapping");
			}
            }
        }
    };

    if (!layout->bindingMap_VS.empty()) {
        iterateOverStageBindings(layout->bindingMap_VS, desc.VS);
        iterateOverStageBindings(layout->bindingMap_VS, desc.ALL);
    }

    if (!layout->bindingMap_HS.empty()) {
        iterateOverStageBindings(layout->bindingMap_HS, desc.HS);
        iterateOverStageBindings(layout->bindingMap_HS, desc.ALL);
    }

    if (!layout->bindingMap_DS.empty()) {
        iterateOverStageBindings(layout->bindingMap_DS, desc.DS);
        iterateOverStageBindings(layout->bindingMap_DS, desc.ALL);
    }

    if (!layout->bindingMap_GS.empty()) {
        iterateOverStageBindings(layout->bindingMap_GS, desc.GS);
        iterateOverStageBindings(layout->bindingMap_GS, desc.ALL);
    }

    if (!layout->bindingMap_PS.empty()) {
        iterateOverStageBindings(layout->bindingMap_PS, desc.PS);
        iterateOverStageBindings(layout->bindingMap_PS, desc.ALL);
    }

    if (!layout->bindingMap_CS.empty()) {
        iterateOverStageBindings(layout->bindingMap_CS, desc.CS);
        // ALL is not included in CS
    }

    context.device.updateDescriptorSets(uint32_t(descriptorWriteInfo.size()), descriptorWriteInfo.data(), 0, nullptr);

    return BindingSetHandle::Create(ret);
}

unsigned long ResourceBindingSet::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyPipelineBindingSet(this); 
    return result; 
}

void Device::destroyPipelineBindingSet(IBindingSet* _binding)
{
    ResourceBindingSet* binding = static_cast<ResourceBindingSet*>(_binding);

    if (binding->descriptorPool)
    {
        context.device.destroyDescriptorPool(binding->descriptorPool, context.allocationCallbacks);
        binding->descriptorPool = vk::DescriptorPool();
        binding->descriptorSet = vk::DescriptorSet();
    }
}

#if _DEBUG
void Device::nameVKObject(const uint64_t handle, const vk::DebugReportObjectTypeEXT objtype, const char* const name)
{
    if (context.extensions.EXT_debug_marker)
    {
        const char* debugname = name;

#ifdef EVIL_DEBUG_NAMES
        std::string st = GrabResolvedStackTrace(2);
        std::string evil_debug_name = std::string(debugname ? debugname : "(?)") + " // " + st;
        debugname = evil_debug_name.c_str();
#endif // EVIL_DEBUG_NAMES

        assert(handle != VK_NULL_HANDLE);

        if (debugname)
        {
            auto info = vk::DebugMarkerObjectNameInfoEXT()
                .setObjectType(objtype)
                .setObject(handle)
                .setPObjectName(debugname);
            context.device.debugMarkerSetObjectNameEXT(&info);
        }
    }
}
#endif // _DEBUG

} // namespace vulkan
} // namespace nvrhi
