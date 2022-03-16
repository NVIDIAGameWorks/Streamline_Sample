#pragma once

namespace nvrhi 
{
namespace vulkan
{

class Device;

class Semaphore : public ReferenceCounter
{
    // semaphore object itself
    vk::Semaphore semaphore = nullptr;

    // identifies which stages this semaphore should block when waiting
    vk::PipelineStageFlags stageFlags = vk::PipelineStageFlags();
    // whether we have submitted a cmdbuf that will signal this semaphore
    bool submitted = false;

public:
    vk::PipelineStageFlags getStageFlags() { return stageFlags; }
    void setStageFlags(vk::PipelineStageFlags flags) { stageFlags = flags; }

    vk::Semaphore getVkSemaphore() { return semaphore; }
    operator vk::Semaphore () { return semaphore; }
    operator vk::Semaphore * () { return &semaphore; }

    // resets the semaphore (allocates objects on first use)
    void reset(VulkanContext& context);
    // destroys the semaphore
    void destroy(VulkanContext& context);

    bool inFlight() { return submitted; }
    // mark the semaphore as having been signaled in a submitted command list
    void markInFlight()
    {
        assert(!submitted);
        submitted = true;
    }
};

class Fence : public ReferenceCounter
{
    vk::Fence fence = nullptr;
    bool signaled = false;

public:
    vk::Fence getVkFence() { return fence; }
    operator vk::Fence () { return fence; }
    operator vk::Fence * () { return &fence; }

    void reset(VulkanContext& context);
    void destroy(VulkanContext& context);
    bool check(VulkanContext& context);
    void wait(VulkanContext& context);
};


template <typename T, bool DoNotAllocate = false>
using VkObjectPool = ObjectPool<VulkanContext, T, DoNotAllocate>;

// manages pooled Vulkan objects
class VulkanSyncObjectPool
{
public:
    VulkanSyncObjectPool(VulkanContext& context, Device* parent)
        : context(context)
        , parent(parent)
        , semaphorePool(context)
        , fencePool(context)
    { }

    Semaphore *getSemaphore(vk::PipelineStageFlags stageFlags);
    void releaseSemaphore(Semaphore* semaphore);

    Fence *getFence();
    void releaseFence(Fence *fence);

private:
    VulkanContext& context;
    Device* parent;

    // GPU semaphore and fence pools
    VkObjectPool<Semaphore> semaphorePool;
    VkObjectPool<Fence> fencePool;
};

} // namespace vulkan
} // namespace nvrhi
