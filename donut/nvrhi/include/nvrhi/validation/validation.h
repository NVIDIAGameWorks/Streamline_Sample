/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <nvrhi/nvrhi.h>
#include <atomic>
#include <bitset>
#include <iostream>

namespace nvrhi
{
namespace validation
{
	class DeviceWrapper;
		
    enum class CommandListState
    {
        INITIAL,
        OPEN,
        CLOSED
    };

    class DeviceWrapper;

    class CommandListWrapper : public RefCounter<ICommandList>
    {
    public:
        friend class DeviceWrapper;

        CommandListWrapper(DeviceWrapper* device, ICommandList* commandList, bool isImmediate);

    private:
        CommandListWrapper& operator=(const CommandListWrapper& other); // undefined

    protected:
        CommandListHandle m_CommandList;
        RefCountPtr<DeviceWrapper> m_Device;
        IMessageCallback* m_MessageCallback;
        bool m_IsImmediate;

        CommandListState m_State = CommandListState::INITIAL;
        bool m_GraphicsStateSet = false;
        bool m_ComputeStateSet = false;
        GraphicsState m_CurrentGraphicsState;
        ComputeState m_CurrentComputeState;

        void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0);

        bool requireOpenState();
        bool requireExecuteState();
        ICommandList* getUnderlyingCommandList() { return m_CommandList; }

    public:

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

        virtual void writeBuffer(IBuffer* b, const void* data, size_t dataSize, size_t destOffsetBytes) override;
        virtual void clearBufferUInt(IBuffer* b, uint32_t clearValue) override;
        virtual void copyBuffer(IBuffer* dest, uint32_t destOffsetBytes, IBuffer* src, uint32_t srcOffsetBytes, size_t dataSizeBytes) override;

        virtual void setGraphicsState(const GraphicsState& state) override;
        virtual void draw(const DrawArguments& args) override;
        virtual void drawIndexed(const DrawArguments& args) override;
        virtual void drawIndirect(uint32_t offsetBytes) override;

        virtual void setComputeState(const ComputeState& state) override;
        virtual void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
        virtual void dispatchIndirect(uint32_t offsetBytes)  override;

        virtual void setRayTracingState(const rt::State& state) override;
        virtual void dispatchRays(const rt::DispatchRaysArguments& args) override;
        
        virtual void buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::BottomLevelAccelStructDesc& desc) override;
        virtual void buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::TopLevelAccelStructDesc& desc) override;

        virtual void beginTimerQuery(ITimerQuery* query) override;
        virtual void endTimerQuery(ITimerQuery* query) override;

        virtual void beginMarker(const char *name) override;
        virtual void endMarker() override;

        virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override;
        virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override;

        virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits) override;
        virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits) override;

        virtual void endTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent = false) override;
        virtual void endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent = false) override;

        virtual ResourceStates::Enum getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override;
        virtual ResourceStates::Enum getBufferState(IBuffer* buffer) override;

        virtual IDevice* getDevice() override;
    };

    struct Range
    {
        uint32_t min = ~0u;
        uint32_t max = 0;

        void add(uint32_t item);
        bool empty() const;
        bool overlapsWith(const Range& other) const;
    };

    struct ShaderBindingSet
    {
        std::bitset<128> SRV;
        std::bitset<128> Sampler;
        std::bitset<16> UAV;
        std::bitset<16> CB;
        uint32_t numVolatileCBs = 0;
        Range rangeSRV;
        Range rangeSampler;
        Range rangeUAV;
        Range rangeCB;

        bool any() const;
        bool overlapsWith(const ShaderBindingSet& other) const;
        friend std::ostream& operator<<(std::ostream& os, const ShaderBindingSet& set);
    };

    class DeviceWrapper : public RefCounter<IDevice>
    {
    public:
        friend class CommandListWrapper;

        DeviceWrapper(IDevice* device);

    private:
        DeviceWrapper& operator=(const DeviceWrapper& other); // undefined

    protected:
        DeviceHandle m_Device;
        IMessageCallback* m_MessageCallback;
        std::atomic<unsigned int> m_NumOpenImmediateCommandLists = 0;

        void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0);
        
    public:

        // IResource implementation

        virtual Object getNativeObject(ObjectType objectType) override;

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
        virtual ShaderLibraryHandle createShaderLibrary(const void* binary, const size_t binarySize) override;
        virtual ShaderLibraryHandle createShaderLibraryPermutation(const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true) override;

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

        virtual rt::PipelineHandle createRayTracingPipeline(const rt::PipelineDesc& desc) override;

        virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) override;

        virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) override;

        virtual rt::AccelStructHandle createBottomLevelAccelStruct(const rt::BottomLevelAccelStructDesc& desc) override;
        virtual rt::AccelStructHandle createTopLevelAccelStruct(uint32_t numInstances, rt::AccelStructBuildFlags::Enum buildFlags) override;

        virtual uint32_t getNumberOfAFRGroups() override;
        virtual uint32_t getAFRGroupOfCurrentFrame(uint32_t numAFRGroups) override;

        virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) override;
        virtual void executeCommandList(ICommandList* commandList) override;
        virtual void waitForIdle() override;
        virtual void runGarbageCollection() override;
        virtual bool queryFeatureSupport(Feature feature) override;
        virtual IMessageCallback* getMessageCallback() override;
    };

    const char* TextureDimensionToString(TextureDimension dimension);

} // namespace validation
} // namespace nvrhi
