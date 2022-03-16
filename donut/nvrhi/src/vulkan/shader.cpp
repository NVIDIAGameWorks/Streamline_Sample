#include <nvrhi/vulkan.h>
#include <nvrhi/common/shader-blob.h>

namespace nvrhi 
{
namespace vulkan
{

ShaderHandle Device::createShader(const ShaderDesc& desc, const void *binary, const size_t binarySize)
{
    vk::Result res;

    Shader *shader = new (nvrhi::HeapAlloc) Shader(this);

    shader->entryName = desc.entryName; // need to know this later when creating PSO...

    shader->desc = desc;
    shader->shaderInfo = vk::ShaderModuleCreateInfo()
                            .setCodeSize(binarySize)
                            .setPCode((const uint32_t *)binary);

    res = context.device.createShaderModule(&shader->shaderInfo, context.allocationCallbacks, &shader->shaderModule);
    CHECK_VK_FAIL(res);
    nameVKObject(shader->shaderModule, (std::string(!desc.debugName.empty()?desc.debugName:"(?)") + ":" + shader->entryName).c_str());

    return ShaderHandle::Create(shader);
}

ShaderHandle Device::createShaderPermutation(const ShaderDesc& d, const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound)
{
    const void* binary = nullptr;
    size_t binarySize = 0;

    if (FindPermutationInBlob(blob, blobSize, constants, numConstants, &binary, &binarySize))
    {
        return createShader(d, binary, binarySize);
    }
    else
    {
        if (errorIfNotFound)
        {
            m_pMessageCallback->message(MessageSeverity::Error, FormatShaderNotFoundMessage(blob, blobSize, constants, numConstants).c_str());
        }

        return nullptr;
    }
}

unsigned long Shader::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyShader(this); 
    return result;
}

void Device::destroyShader(IShader* _shader)
{
    Shader* shader = static_cast<Shader*>(_shader);

    assert(shader);

    if (shader->shaderModule)
    {
        context.device.destroyShaderModule(shader->shaderModule, context.allocationCallbacks);
        shader->shaderModule = vk::ShaderModule();
    }

    heapDelete(shader);
}

InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* attributeDesc, uint32_t attributeCount, IShader* vertexShader)
{
    (void)vertexShader;

    InputLayout *layout = new (nvrhi::HeapAlloc) InputLayout(this);

    int total_attribute_array_size = 0;

    // collect all buffer bindings
    nvrhi::map<uint32_t, vk::VertexInputBindingDescription> bindingMap;
    for (uint32_t i = 0; i < attributeCount; i++)
    {
        const VertexAttributeDesc& desc = attributeDesc[i];

        assert(desc.arraySize > 0);

        total_attribute_array_size += desc.arraySize;

        if (bindingMap.find(desc.bufferIndex) == bindingMap.end())
        {
            bindingMap[desc.bufferIndex] = vk::VertexInputBindingDescription()
                .setBinding(desc.bufferIndex)
                .setStride(desc.elementStride)
                .setInputRate(desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex);
        }
        else {
            assert(bindingMap[desc.bufferIndex].stride == desc.elementStride);
            assert(bindingMap[desc.bufferIndex].inputRate == (desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex));
        }
    }

    for (const auto& b : bindingMap)
    {
        layout->bindingDesc.push_back(b.second);
    }

    // build attribute descriptions
    layout->inputDesc.resize(attributeCount);
    layout->attributeDesc.resize(total_attribute_array_size);

    uint32_t attributeLocation = 0;
    for (uint32_t i = 0; i < attributeCount; i++)
    {
        const VertexAttributeDesc& in = attributeDesc[i];
        layout->inputDesc[i] = in;

        uint32_t element_size_bytes = (formatElementSizeBits(in.format) + 7) / 8;

        uint32_t bufferOffset = 0;

        for (uint32_t slot = 0; slot < in.arraySize; ++slot)
        {
            auto& outAttrib = layout->attributeDesc[attributeLocation];

            outAttrib.location = attributeLocation;
            outAttrib.binding = in.bufferIndex;
            outAttrib.format = vulkan::convertFormat(in.format);
            outAttrib.offset = bufferOffset + in.offset;
            bufferOffset += element_size_bytes;

            ++attributeLocation;
        }
    }

    return InputLayoutHandle::Create(layout);
}

unsigned long InputLayout::Release() 
{ 
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyInputLayout(this); 
    return result; 
}

void Device::destroyInputLayout(IInputLayout* _i)
{
    InputLayout* i = static_cast<InputLayout*>(_i);

    heapDelete(i);
}

} // namespace vulkan
} // namespace nvrhi
