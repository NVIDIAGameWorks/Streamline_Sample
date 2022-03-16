#pragma once

#include <nvrhi/vulkan.h>

#include <array>

namespace nvrhi 
{
    namespace ObjectTypes
    {
        constexpr ObjectType Nvrhi_VK_Device = 0x00030101;
    };

namespace vulkan
{

class Semaphore;
typedef Semaphore * SemaphoreHandle;

#define CHECK_VK_RETURN(res) if (res != vk::Result::eSuccess) { return res; }
#define CHECK_VK_FAIL(res) if (res != vk::Result::eSuccess) { return nullptr; }
#if _DEBUG
#define ASSERT_VK_OK(res) assert((res) == vk::Result::eSuccess)
#else // _DEBUG
#define ASSERT_VK_OK(res) do{(void)(res);}while(0)
#endif // _DEBUG

class IDeviceAndCommandList : public IDevice, public ICommandList
{
};

class Device : public RefCounter<IDeviceAndCommandList>
{
    friend class Queue;
    friend class Semaphore;
    friend class Fence;

public:
    Device(IMessageCallback* errorCB, vk::Instance instance,
                            vk::PhysicalDevice physicalDevice,
                            vk::Device device,
                            // any of the queues can be null if this context doesn't intend to use them
                            // if a queue is specified with a null command pool, nvrhi will allocate a command pool internally
                            vk::Queue graphicsQueue, int graphicsQueueIndex, vk::CommandPool graphicsCommandPool,
                            vk::Queue transferQueue, int transferQueueIndex, vk::CommandPool transferCommandPool,
                            vk::Queue computeQueue,  int computeQueueIndex,  vk::CommandPool computeQueueCommandPool,
                            vk::AllocationCallbacks *allocationCallbacks = nullptr,
                            const char **instanceExtensions = nullptr, size_t numInstanceExtensions = 0,
                            const char **layers = nullptr, size_t numLayers = 0,
                            const char **deviceExtensions = nullptr, size_t numDeviceExtensions = 0);

    ~Device();

    // IResource implementation

    virtual Object getNativeObject(ObjectType objectType) override;

    // ICommandList implementation

    virtual void open() override;
    virtual void close() override;
    virtual void clearState() override;

    virtual void clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor) override;
    virtual void clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) override;
    virtual void clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, uint32_t clearColor) override;

    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) override;
    virtual void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) override;
    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice) override;
    virtual void writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch) override;
    virtual void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) override;

    virtual void writeBuffer(IBuffer* b, const void* data, size_t dataSize, size_t destOffsetBytes = 0) override;
    virtual void clearBufferUInt(IBuffer* b, uint32_t clearValue) override;
    virtual void copyBuffer(IBuffer* dest, uint32_t destOffsetBytes, IBuffer* src, uint32_t srcOffsetBytes, size_t dataSizeBytes) override;

    virtual void setGraphicsState(const GraphicsState& state) override;
    virtual void draw(const DrawArguments& args) override;
    virtual void drawIndexed(const DrawArguments& args) override;
    virtual void drawIndirect(uint32_t offsetBytes) override;

    virtual void setComputeState(const ComputeState& state) override;
    virtual void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
    virtual void dispatchIndirect(uint32_t offsetBytes)  override;

    virtual void beginTimerQuery(ITimerQuery* query) override;
    virtual void endTimerQuery(ITimerQuery* query) override;

    // perf markers
    virtual void beginMarker(const char *name) override;
    virtual void endMarker() override;

    virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override { (void)texture; (void)enableBarriers; }
    virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override { (void)buffer; (void)enableBarriers; }

    virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits) override { (void)texture; (void)subresources; (void)stateBits; }
    virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits) override { (void)buffer; (void)stateBits; }

    virtual void endTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent = false) override { (void)texture; (void)subresources; (void)stateBits; (void)permanent; }
    virtual void endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent = false) override { (void)buffer; (void)stateBits; (void)permanent; }

    virtual ResourceStates::Enum getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override { (void)texture; (void)arraySlice; (void)mipLevel; return ResourceStates::COMMON; }
    virtual ResourceStates::Enum getBufferState(IBuffer* buffer) override { (void)buffer; return ResourceStates::COMMON; }

    virtual IDevice* getDevice() override { return this; }


    // IDevice implementation

    virtual TextureHandle createTexture(const TextureDesc& d) override;

    virtual TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) override;

    virtual StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess) override;
    virtual void *mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch) override;
    virtual void unmapStagingTexture(IStagingTexture* tex) override;

    virtual BufferHandle createBuffer(const BufferDesc& d) override;
    virtual void *mapBuffer(IBuffer* b, CpuAccessMode mapFlags) override;
    virtual void unmapBuffer(IBuffer* b) override;

    virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) override;

    virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, const size_t binarySize) override;
    virtual ShaderHandle createShaderPermutation(const ShaderDesc& d, const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true) override;
    virtual ShaderLibraryHandle createShaderLibrary(const void* binary, const size_t binarySize) override { (void)binary; (void)binarySize; return nullptr; }
    virtual ShaderLibraryHandle createShaderLibraryPermutation(const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true) override { (void)blob; (void)blobSize; (void)constants; (void)numConstants; (void)errorIfNotFound; return nullptr; }

    virtual SamplerHandle createSampler(const SamplerDesc& d) override;

    virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) override;

    // event queries
    virtual EventQueryHandle createEventQuery(void) override;
    virtual void setEventQuery(IEventQuery* query) override;
    virtual bool pollEventQuery(IEventQuery* query) override;
    virtual void waitEventQuery(IEventQuery* query) override;
    virtual void resetEventQuery(IEventQuery* query) override;

    // timer queries
    virtual TimerQueryHandle createTimerQuery(void) override;
    virtual bool pollTimerQuery(ITimerQuery* query) override;
    virtual float getTimerQueryTime(ITimerQuery* query) override;
    virtual void resetTimerQuery(ITimerQuery* query) override;

    virtual GraphicsAPI getGraphicsAPI() override;

    virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) override;

    virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb) override;

    virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override;

    virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) override;

    virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) override;

    virtual uint32_t getNumberOfAFRGroups() override;
    virtual uint32_t getAFRGroupOfCurrentFrame(uint32_t numAFRGroups) override;

    virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) override;
    virtual void executeCommandList(ICommandList* commandList) override;
    virtual void waitForIdle() override;
    virtual void runGarbageCollection() override { }
    virtual bool queryFeatureSupport(Feature feature) override { (void)feature; return false; } // Vulkan backend needs some work
    virtual IMessageCallback* getMessageCallback() override { return m_pMessageCallback; }

    // misc functions

    // uses whatever command buffer is already sitting in the context, otherwise sticks this in the graphics queue
    // void bufferBarrier(Buffer *buffer,
    //                    vk::PipelineStageFlags dstStageFlags,
    //                    vk::AccessFlags dstAccessMask,
    //                    uint32_t dstQueueFamilyIndex);

    void imageBarrier(ITexture* image,
                      const TextureSlice& slice,
                      vk::PipelineStageFlags dstStageFlags,
                      vk::AccessFlags dstAccessMask,
                      vk::ImageLayout dstLayout);


    // creates a semaphore
    SemaphoreHandle createSemaphore(vk::PipelineStageFlags stageFlags);
    // marks a semaphore as being in flight
    void markSemaphoreInFlight(SemaphoreHandle semaphore);
    // releases a semaphore --- if refcount == 0, semaphore is destroyed
    void releaseSemaphore(SemaphoreHandle semaphore);

    // updates the read/write semaphores for a texture
    void setTextureReadSemaphore(ITexture* texture, SemaphoreHandle semaphore);
    void setTextureWriteSemaphore(ITexture* texture, SemaphoreHandle semaphore);
    // returns the read/write semaphores for a texture
    // note: this places a refcount on the semaphore, app will need to call releaseSemaphore afterwards
    SemaphoreHandle getTextureReadSemaphore(ITexture* texture);
    SemaphoreHandle getTextureWriteSemaphore(ITexture* texture);

    // get the Vulkan stage flags for a semaphore
    vk::PipelineStageFlags getVulkanSemaphoreStageFlags(SemaphoreHandle semaphore);
    // retrieve the Vulkan semaphore object
    // (does not increment refcount)
    vk::Semaphore getVulkanSemaphore(SemaphoreHandle semaphore);

    void flushCommandList();

protected:
    VulkanContext context;
    VulkanSyncObjectPool syncObjectPool;
    VulkanAllocator allocator;

    // numTimerQueries must be a multiple of two
    // this represents the number of timestamp queries in the pool, each
    // timer query requires two timestamps
    static constexpr uint32_t numTimerQueries = 512;
    vk::QueryPool timerQueryPool = nullptr;
    uint32_t nextTimerQueryIndex = 0;
    VkObjectPool<TimerQuery, true> timerQueryObjectPool;

    // array of submission queues
    std::array<Queue, QueueID::Count> queues;
    // current internal command buffer
    TrackedCommandBuffer * internalCmd = nullptr;

    BufferHandle m_CurrentDrawIndirectBuffer;
    BufferHandle m_CurrentDispatchIndirectBuffer;

    IMessageCallback* m_pMessageCallback;

    const nvrhi::map<const std::string, bool *> getExtensionStringMap();

    void bindGraphicsPipeline(TrackedCommandBuffer *cmd, GraphicsPipeline *pso, Framebuffer *fb);
    void bindGraphicsState(TrackedCommandBuffer *cmd, const GraphicsState& state);

    void bindComputePipeline(TrackedCommandBuffer *cmd, BarrierTracker& tracker, ComputePipeline *pso, const BindingSetVector& bindings);

    void trackResourcesAndBarriers(TrackedCommandBuffer *cmd, const GraphicsState& state);
    void trackResourcesAndBarriers(TrackedCommandBuffer *cmd,
                                   BarrierTracker& barrierTracker,
                                   const ResourceBindingMap& bindingMap,
                                   const StageBindingSetDesc& bindings,
                                   vk::PipelineStageFlags stageFlags);

    TrackedCommandBuffer *getCmdBuf(QueueID queue);
    TrackedCommandBuffer *getAnyCmdBuf();
    TrackedCommandBuffer *pollAnyCmdBuf();
    
    void *mapBuffer(IBuffer* b, CpuAccessMode flags, off_t offset, size_t size);

public:
    void destroyTexture(ITexture* t);
    void destroyStagingTexture(IStagingTexture* tex);
    void destroyBuffer(IBuffer* b);
    void destroyShader(IShader* s);
    void destroySampler(ISampler* s);
    void destroyInputLayout(IInputLayout* i);
    void destroyEventQuery(IEventQuery* query);
    void destroyTimerQuery(ITimerQuery* query);
    void destroyFramebuffer(IFramebuffer* fb);
    void destroyGraphicsPipeline(IGraphicsPipeline* pso);
    void destroyComputePipeline(IComputePipeline* pso);
    void destroyPipelineBindingLayout(IBindingLayout* layout);
    void destroyPipelineBindingSet(IBindingSet* binding);

#if _DEBUG
    void nameVKObject(const uint64_t handle, const vk::DebugReportObjectTypeEXT objtype, const char* const name);
#define VKNO(T) void nameVKObject(const vk::T obj, const char* const name = nullptr) { nameVKObject(uint64_t(Vk##T(obj)), vk::DebugReportObjectTypeEXT::e##T, name); }
    // generate specializations for nameObject()
    VKNO(Instance) VKNO(PhysicalDevice) VKNO(Device) VKNO(Queue) VKNO(Semaphore) VKNO(CommandBuffer)
        VKNO(Fence) VKNO(DeviceMemory) VKNO(Buffer) VKNO(Image) VKNO(Event) VKNO(QueryPool) VKNO(BufferView)
        VKNO(ImageView) VKNO(ShaderModule) VKNO(PipelineCache) VKNO(PipelineLayout) VKNO(RenderPass)
        VKNO(Pipeline) VKNO(DescriptorSetLayout) VKNO(Sampler) VKNO(DescriptorPool) VKNO(DescriptorSet)
        VKNO(Framebuffer) VKNO(CommandPool)
#undef VKNO
#else // _DEBUG
    void nop() {};
#define nameVKObject(...) nop()
#endif // _DEBUG

};

} // namespace vulkan
} // namespace nvrhi
