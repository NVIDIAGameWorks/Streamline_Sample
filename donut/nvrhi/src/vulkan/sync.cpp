#include <nvrhi/vulkan.h>


namespace nvrhi 
{
namespace vulkan
{

void Semaphore::reset(VulkanContext& context)
{
    if (semaphore)
    {
        context.device.destroySemaphore(semaphore, context.allocationCallbacks);
        semaphore = nullptr;
    }

    auto info = vk::SemaphoreCreateInfo();
    vk::Result res;

    res = context.device.createSemaphore(&info, context.allocationCallbacks, &semaphore);
    ASSERT_VK_OK(res);

    stageFlags = vk::PipelineStageFlags();
    submitted = false;
}

void Semaphore::destroy(VulkanContext& context)
{
    if (semaphore)
    {
        context.device.destroySemaphore(semaphore, context.allocationCallbacks);
        semaphore = nullptr;
    }
}


void Fence::reset(VulkanContext& context)
{
    vk::Result res;

    if (fence == vk::Fence())
    {
        // create a new fence object
        auto info = vk::FenceCreateInfo();
        res = context.device.createFence(&info, context.allocationCallbacks, &fence);
        ASSERT_VK_OK(res);
    } else {
        // reset the fence object back to the unsignaled state
        res = context.device.resetFences(1, &fence);
        ASSERT_VK_OK(res);
    }

    signaled = false;
}

void Fence::destroy(VulkanContext& context)
{
    if (fence != vk::Fence())
    {
        context.device.destroyFence(fence, context.allocationCallbacks);
        fence = nullptr;
    }
}

bool Fence::check(VulkanContext& context)
{
    assert(fence);

    if (signaled)
    {
        return true;
    }

    vk::Result res;
    res = context.device.getFenceStatus(fence);

    if (res == vk::Result::eSuccess)
    {
        signaled = true;
        return true;
    } else {
        return false;
    }
}

void Fence::wait(VulkanContext& context)
{
    assert(fence);

    if (signaled)
    {
        return;
    }

    vk::Result res;
    res = context.device.waitForFences({ fence }, true, 10000000000ul);
    assert(res == vk::Result::eSuccess);
}

Semaphore *VulkanSyncObjectPool::getSemaphore(vk::PipelineStageFlags stageFlags)
{
    Semaphore *ret = semaphorePool.get();
    parent->nameVKObject(ret->getVkSemaphore());

    int32_t refcount = ret->addref();
    (void)refcount;
    assert(refcount == 1);

    ret->setStageFlags(stageFlags);
    return ret;
}

void VulkanSyncObjectPool::releaseSemaphore(Semaphore* semaphore)
{
    assert(semaphore);

    int32_t refcount = semaphore->release();

    if (refcount == 0)
    {
        semaphorePool.retire(semaphore);
    }
}

Fence *VulkanSyncObjectPool::getFence()
{
    Fence *ret = fencePool.get();
    parent->nameVKObject(ret->getVkFence());

    int32_t refcount = ret->addref();
    (void)refcount;
    assert(refcount == 1);

    return ret;
}

void VulkanSyncObjectPool::releaseFence(Fence *fence)
{
    int32_t refcount = fence->release();

    if (refcount == 0)
    {
        fencePool.retire(fence);
    }
}

} // namespace vulkan
} // namespace nvrhi
