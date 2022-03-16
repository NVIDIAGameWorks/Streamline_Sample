#include <nvrhi/vulkan.h>

namespace nvrhi 
{
namespace vulkan
{

static vk::MemoryPropertyFlags pickBufferMemoryProperties(const BufferDesc& d)
{
    vk::MemoryPropertyFlags flags = vk::MemoryPropertyFlags();

    if (d.cpuAccess != CpuAccessMode::None)
    {
        flags |= vk::MemoryPropertyFlagBits::eHostVisible;
        // we have to request cached here - otherwise access to this memory on the CPU will be 10x slower
        flags |= vk::MemoryPropertyFlagBits::eHostCached;
    } else {
        flags |= vk::MemoryPropertyFlagBits::eDeviceLocal;
    }

    return flags;
}

vk::Result VulkanAllocator::allocateBufferMemory(Buffer *buffer)
{
    vk::Result res;

    // figure out memory requirements
    vk::MemoryRequirements memRequirements;
    context.device.getBufferMemoryRequirements(buffer->buffer, &memRequirements);

    // allocate memory
    res = allocateMemory(buffer, memRequirements, pickBufferMemoryProperties(buffer->desc));
    CHECK_VK_RETURN(res);

    context.device.bindBufferMemory(buffer->buffer, buffer->memory, 0);

    return vk::Result::eSuccess;
}

void VulkanAllocator::freeBufferMemory(Buffer *buffer)
{
    freeMemory(buffer);
}

static vk::MemoryPropertyFlags pickTextureMemoryProperties(const TextureDesc& d)
{
    (void)d; 
    vk::MemoryPropertyFlags flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    return flags;
}

vk::Result VulkanAllocator::allocateTextureMemory(Texture *texture)
{
    vk::Result res;

    // grab the image memory requirements
    vk::MemoryRequirements memRequirements;
    context.device.getImageMemoryRequirements(texture->image, &memRequirements);

    // allocate memory
    vk::MemoryPropertyFlags memProperties = pickTextureMemoryProperties(texture->desc);
    res = allocateMemory(texture, memRequirements, memProperties);
    CHECK_VK_RETURN(res);

    context.device.bindImageMemory(texture->image, texture->memory, 0);

    return vk::Result::eSuccess;
}

void VulkanAllocator::freeTextureMemory(Texture *texture)
{
    freeMemory(texture);
}

vk::Result VulkanAllocator::allocateMemory(MemoryResource *res,
                                           vk::MemoryRequirements memRequirements,
                                           vk::MemoryPropertyFlags memPropertyFlags)
{
    res->managed = true;
    res->propertyFlags = memPropertyFlags;

    // find a memory space that satisfies the requirements
    vk::PhysicalDeviceMemoryProperties memProperties;
    context.physicalDevice.getMemoryProperties(&memProperties);

    uint32_t memTypeIndex;
    for(memTypeIndex = 0; memTypeIndex < memProperties.memoryTypeCount; memTypeIndex++)
    {
        if ((memRequirements.memoryTypeBits & (1 << memTypeIndex)) &&
            ((memProperties.memoryTypes[memTypeIndex].propertyFlags & memPropertyFlags) == memPropertyFlags))
        {
            break;
        }
    }

    if (memTypeIndex == memProperties.memoryTypeCount)
    {
        // xxxnsubtil: this is incorrect; need better error reporting
        return vk::Result::eErrorOutOfDeviceMemory;
    }

    // allocate memory
    auto allocInfo = vk::MemoryAllocateInfo()
                        .setAllocationSize(memRequirements.size)
                        .setMemoryTypeIndex(memTypeIndex);

    return context.device.allocateMemory(&allocInfo, context.allocationCallbacks, &res->memory);
}

void VulkanAllocator::freeMemory(MemoryResource *res)
{
    assert(res->managed);

    context.device.freeMemory(res->memory, context.allocationCallbacks);
    res->memory = vk::DeviceMemory(nullptr);
}

} // namespace vulkan
} // namespace nvrhi
