#include <nvrhi/vulkan.h>

namespace nvrhi
{
namespace vulkan
{

void TrackedCommandBuffer::bindPSO(vk::PipelineBindPoint bindPoint, vk::Pipeline pso)
{
    switch(bindPoint)
    {
        case vk::PipelineBindPoint::eGraphics:
            if (pso != currentPSO_Graphics)
            {
                cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pso);
                currentPSO_Graphics = pso;
            }

            break;

        case vk::PipelineBindPoint::eCompute:
            if (pso != currentPSO_Compute)
            {
                cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pso);
                currentPSO_Compute = pso;
            }
    }
}

void TrackedCommandBuffer::bindDescriptorSets(vk::PipelineBindPoint bindPoint,
                                              vk::PipelineLayout layout,
                                              const static_vector<vk::DescriptorSet, MaxBindingLayouts>& descriptorSets)
{
    auto& current =
        bindPoint == vk::PipelineBindPoint::eGraphics ?
            currentDescriptorSets_Graphics : currentDescriptorSets_Compute;

    for(size_t i = 0; i < descriptorSets.size(); i++)
    {
        if (current[i] != descriptorSets[i])
        {
            cmdBuf.bindDescriptorSets(bindPoint, layout,
                                        uint32_t(i), 1, &descriptorSets[i],
                                        0, nullptr);

            current[i] = descriptorSets[i];
        }
    }
}

void TrackedCommandBuffer::bindFB(Framebuffer *fb)
{
    if (currentFB != fb)
    {
        if (currentFB)
        {
            cmdBuf.endRenderPass();
        }

        cmdBuf.beginRenderPass(vk::RenderPassBeginInfo()
                                .setRenderPass(fb->renderPass)
                                .setFramebuffer(fb->framebuffer)
                                .setRenderArea(vk::Rect2D()
                                                .setOffset(vk::Offset2D(0, 0))
                                                .setExtent(vk::Extent2D(fb->renderAreaW, fb->renderAreaH)))
                                .setClearValueCount(0),
                                vk::SubpassContents::eInline);

        currentFB = fb;
    }
}

void TrackedCommandBuffer::unbindFB()
{
    if (currentFB)
    {
        cmdBuf.endRenderPass();
        currentFB = nullptr;
    }
}

TrackedCommandBuffer *Queue::createOneShotCmdBuf()
{
    vk::Result res;

    TrackedCommandBuffer *ret = new (nvrhi::HeapAlloc) TrackedCommandBuffer();
    ret->targetQueueID = queueID;

    // check if we need to allocate a command pool
    if (commandPool == vk::CommandPool())
    {
        auto cmdPoolInfo = vk::CommandPoolCreateInfo()
                            .setQueueFamilyIndex(index)
                            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                                      vk::CommandPoolCreateFlagBits::eTransient);

        res = context.device.createCommandPool(&cmdPoolInfo, context.allocationCallbacks, &commandPool);
        CHECK_VK_FAIL(res);
        parent->nameVKObject(commandPool);
    }

    // allocate command buffer
    auto allocInfo = vk::CommandBufferAllocateInfo()
                        .setLevel(vk::CommandBufferLevel::ePrimary)
                        .setCommandPool(commandPool)
                        .setCommandBufferCount(1);

    res = context.device.allocateCommandBuffers(&allocInfo, &ret->cmdBuf);
    CHECK_VK_FAIL(res);

    ret->writeList.clear();
    ret->readList.clear();

    // start recording into it
    auto beginInfo = vk::CommandBufferBeginInfo()
                        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    ret->cmdBuf.begin(&beginInfo);

    return ret;
}

static void moveSemaphoreToSubmitList(CommandBufferSubmission& submit, vk::Semaphore *semArray, vk::PipelineStageFlags *flagArray, Semaphore *semaphore)
{
    assert(semaphore->getVkSemaphore() != vk::Semaphore());
    assert(submit.numWaitSemaphores < submit.waitSemaphores.size());

    if (semaphore->inFlight())
    {
        // semaphore release already in flight, nothing to do
        return;
    }

    submit.waitSemaphores[submit.numWaitSemaphores] = semaphore;
    semArray[submit.numWaitSemaphores] = semaphore->getVkSemaphore();
    flagArray[submit.numWaitSemaphores] = semaphore->getStageFlags();

    semaphore->addref();

    submit.numWaitSemaphores++;
}

void Queue::submit(TrackedCommandBuffer *cmd)
{
    cmd->cmdBuf.end();

    CommandBufferSubmission submit;
    submit.cmdBuf = cmd;

    // array of Vulkan objects for vk::SubmitInfo
    std::array<vk::Semaphore, CommandBufferSubmission::SemaphoreArraySize> waitSemArray;
    std::array<vk::PipelineStageFlags, CommandBufferSubmission::SemaphoreArraySize> waitStageArray;

    // build the list of wait semaphores
    for(auto *res : cmd->writeList)
    {
        if (res->getReadSemaphore())
        {
            moveSemaphoreToSubmitList(submit, waitSemArray.data(), waitStageArray.data(), res->getReadSemaphore());
            res->setReadSemaphore(syncObjectPool, nullptr);
        }

        if (res->writeSemaphore)
        {
            moveSemaphoreToSubmitList(submit, waitSemArray.data(), waitStageArray.data(), res->getWriteSemaphore());
            res->setWriteSemaphore(syncObjectPool, nullptr);
        }
    }

    for(auto *res : cmd->readList)
    {
        if (res->writeSemaphore)
        {
            moveSemaphoreToSubmitList(submit, waitSemArray.data(), waitStageArray.data(), res->writeSemaphore);
            res->setWriteSemaphore(syncObjectPool, nullptr);
        }
    }

    // figure out if we need to signal a semaphore at the bottom of the command buffer
    assert(!submit.completionSemaphore);
    submit.completionSemaphore = nullptr;
    if (cmd->readList.size() != 0 || cmd->writeList.size() != 0)
    {
        vk::PipelineStageFlags flags;
        switch(queueID)
        {
            case QueueID::Graphics:
                flags = vk::PipelineStageFlagBits::eAllGraphics;
                break;

            case QueueID::Compute:
                flags = vk::PipelineStageFlagBits::eComputeShader;
                break;

            case QueueID::Transfer:
                flags = vk::PipelineStageFlagBits::eTransfer;
                break;

            default:
                assert(!"can't happen");
                break;
        }

        submit.completionSemaphore = syncObjectPool.getSemaphore(flags);
        assert(!submit.completionSemaphore->inFlight());
    }

    submit.completionFence = syncObjectPool.getFence();

	vk::Semaphore completionSemaphore;
	if (submit.completionSemaphore)
		completionSemaphore = submit.completionSemaphore->getVkSemaphore();

    submit.info = vk::SubmitInfo()
                    .setCommandBufferCount(1)
                    .setPCommandBuffers(&cmd->cmdBuf)
                    .setWaitSemaphoreCount(submit.numWaitSemaphores)
                    .setPWaitSemaphores(waitSemArray.data())
                    .setPWaitDstStageMask(waitStageArray.data())
                    .setSignalSemaphoreCount(submit.completionSemaphore ? 1 : 0)
                    .setPSignalSemaphores(&completionSemaphore);

    assert(submit.completionFence->getVkFence());
    queue.submit({ submit.info }, submit.completionFence->getVkFence());

    // XXX(jstpierre): The fences should be set regardless of the semaphore existing.
    if (submit.completionSemaphore)
    {
        submit.completionSemaphore->markInFlight();

        // update read/write semaphores for this submit
        for(auto *res : cmd->writeList)
        {
            res->setWriteSemaphore(syncObjectPool, submit.completionSemaphore);
            res->setWriteFence(syncObjectPool, submit.completionFence);
        }

        for(auto *res : cmd->readList)
        {
            res->setReadSemaphore(syncObjectPool, submit.completionSemaphore);
            res->setReadFence(syncObjectPool, submit.completionFence);
        }
    }

    commandBuffersInFlight.push_front(submit);

    // update any fence pointers that are waiting for the submit fence
    for (auto **query : submitFenceListeners)
    {
        *query = submit.completionFence;
        submit.completionFence->addref();
    }

    submitFenceListeners.clear();
}

void Queue::retireCommandBuffers()
{
    // take local ownership of the command buffer list, as resource destruction can create reentrancy
    nvrhi::list<CommandBufferSubmission> submissions = std::move(commandBuffersInFlight);
    commandBuffersInFlight = nvrhi::list<CommandBufferSubmission>();

    auto iter = submissions.begin();
    while (iter != submissions.end())
    {
        assert(!submissions.empty());
        auto cur = iter;
        auto& submit = *iter;
        iter++;

        assert(submit.completionFence->getVkFence());
        if (submit.completionFence->check(context))
        {
            syncObjectPool.releaseFence(submit.completionFence);

            // XXX(jstpierre): The fences should be set regardless of the semaphore existing.
            if (submit.completionSemaphore)
            {
                // XXX(jstpierre): This is an ugly way of resetting the fences before we release
                // the objects below. If the fence isn't this one, then it's either part of a
                // future fence which will destroy it when it signals, or it's NULL.

                for (auto *res : submit.cmdBuf->writeList)
                {
                    if (res->writeFence == submit.completionFence)
                        res->setWriteFence(syncObjectPool, nullptr);
                }

                for (auto *res : submit.cmdBuf->readList)
                {
                    if (res->readFence == submit.completionFence)
                        res->setReadFence(syncObjectPool, nullptr);
                }

                syncObjectPool.releaseSemaphore(submit.completionSemaphore);
            }

            for(uint32_t i = 0; i < submit.numWaitSemaphores; i++)
            {
                syncObjectPool.releaseSemaphore(submit.waitSemaphores[i]);
            }

            context.device.freeCommandBuffers(commandPool, { submit.cmdBuf->cmdBuf });

            submit.cmdBuf->referencedResources.clear();
            heapDelete(submit.cmdBuf);
        }
        else
        {
            commandBuffersInFlight.push_back(submit);
        }
    }
}

void Queue::idle()
{
    queue.waitIdle();
}

void Queue::addSubmitFenceListener(Fence **fence)
{
    submitFenceListeners.push_front(fence);
}

} // namespace vulkan
} // namespace nvrhi
