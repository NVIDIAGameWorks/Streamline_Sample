#pragma once

namespace nvrhi 
{
namespace vulkan 
{

class VulkanAllocator
{
public:
    VulkanAllocator(VulkanContext& context)
        : context(context)
    { }

    vk::Result allocateBufferMemory(Buffer *buffer);
    void freeBufferMemory(Buffer *buffer);

    vk::Result allocateTextureMemory(Texture *texture);
    void freeTextureMemory(Texture *texture);

private:
    vk::Result allocateMemory(MemoryResource *res,
                              vk::MemoryRequirements memRequirements,
                              vk::MemoryPropertyFlags memProperties);
    void freeMemory(MemoryResource *res);

    VulkanContext& context;
};

} // namespace vulkan
} // namespace nvrhi
