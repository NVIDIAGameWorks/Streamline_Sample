#pragma once

namespace nvrhi 
{
namespace vulkan
{

class Device;
class MemoryResource;
class VulkanSyncObjectPool;
class PipelineState;
class EventQuery;

typedef enum
{
    Graphics,
    Transfer,
    Compute,

    Count
} QueueID;

// command buffer with resource tracking
struct TrackedCommandBuffer
{
    // target queue ID
    QueueID targetQueueID;

    // set of resources that will be read from/written to by this command buffer
    nvrhi::unordered_set<MemoryResource *> writeList;
    nvrhi::unordered_set<MemoryResource *> readList;

    // the command bufer itself
    vk::CommandBuffer cmdBuf;

    // currently bound PSO, if any
    vk::Pipeline currentPSO_Graphics = nullptr;
    vk::Pipeline currentPSO_Compute = nullptr;

    // currently bound descriptor sets
    std::array<vk::DescriptorSet, MaxBindingLayouts> currentDescriptorSets_Graphics;
    std::array<vk::DescriptorSet, MaxBindingLayouts> currentDescriptorSets_Compute;

    // current framebuffer, if any
    Framebuffer *currentFB = nullptr;

    std::vector<RefCountPtr<IResource>> referencedResources; // to keep them alive

    void markWrite(MemoryResource *resource)
    {
        writeList.insert(resource);
    }

    void markRead(MemoryResource *resource)
    {
        readList.insert(resource);
    }

    bool isResourceMarked(MemoryResource *resource)
    {
        return readList.find(resource) != readList.end() ||
               writeList.find(resource) != writeList.end();
    }

    void bindPSO(vk::PipelineBindPoint bindPoint, vk::Pipeline pso);
    void bindDescriptorSets(vk::PipelineBindPoint bindPoint,
                            vk::PipelineLayout layout,
                            const static_vector<vk::DescriptorSet, MaxBindingLayouts>& descriptorSets);
    void bindFB(Framebuffer *fb);
    void unbindFB();
};

// a command buffer submission
struct CommandBufferSubmission
{
    enum {
        // xxxnsubtil: 64 semaphores is enough for everyone?
        SemaphoreArraySize = 64
    };

    TrackedCommandBuffer *cmdBuf = nullptr;
    vk::SubmitInfo info;

    // list of wait semaphores in this submit
    std::array<Semaphore *, SemaphoreArraySize> waitSemaphores = { nullptr };
    uint32_t numWaitSemaphores = 0;

    // completion semaphore and fence
    Semaphore *completionSemaphore = nullptr;
    Fence *completionFence = nullptr;
};

// represents a hardware queue
class Queue
{
public:
    Queue(VulkanContext& context,
          Device* parent,
          VulkanSyncObjectPool& syncObjectPool,
          QueueID queueID,
          vk::Queue queue,
          uint32_t index,
          vk::CommandPool commandPool = vk::CommandPool())
        : context(context)
        , parent(parent)
        , syncObjectPool(syncObjectPool)
        , queueID(queueID)
        , queue(queue)
        , index(index)
        , commandPool(commandPool)
    {
        if (commandPool == vk::CommandPool())
        {
            this->poolManaged = true;
        }
    }

    // creates a one-shot command buffer
    TrackedCommandBuffer *createOneShotCmdBuf();
    // submits a command buffer to this queue
    // note: all command buffers are presumed to be one-shot and will be destroyed after execution
    void submit(TrackedCommandBuffer *cmd);

    // retire any command buffers that have finished execution from the pending execution list
    void retireCommandBuffers();

    // wait for queue idle
    void idle();

    // adds a pointer to a fence pointer to be updated with the submit fence after next submit
    void addSubmitFenceListener(Fence **fence);

private:
    VulkanContext & context;
    Device* parent;
    VulkanSyncObjectPool& syncObjectPool;

    QueueID queueID;
    vk::Queue queue;
    uint32_t index = uint32_t(-1);
    vk::CommandPool commandPool;
    bool poolManaged = false;

    // list of fence pointers to be updated at the next submit
    nvrhi::forward_list<Fence **> submitFenceListeners;

    // tracks the list of command buffers in flight on this queue
    nvrhi::list<CommandBufferSubmission> commandBuffersInFlight;
};

} // namespace vulkan
} // namespace nvrhi
