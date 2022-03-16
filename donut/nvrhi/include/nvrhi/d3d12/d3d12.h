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

#include <d3d12.h>
#include <bitset>
#include <memory>
#include <queue>
#include <atomic>

namespace nvrhi
{
    namespace ObjectTypes
    {
        constexpr ObjectType Nvrhi_D3D12_Device         = 0x00020101;
        constexpr ObjectType Nvrhi_D3D12_CommandList    = 0x00020102;
    };

#if NVRHI_WITH_DXR
namespace dxr
{
    class ShaderTableState;
}
#endif

namespace d3d12
{
    class Device;
    class CommandList;
    class StageBindingLayout;
    class Shader;
    class Texture;
    class Buffer;
    class InputLayout;
    class TimerQuery;
    class ManagedResource;
    class Framebuffer;
    class BindingLayout;
    class BindingSet;
    class StagingTexture;
    class CommandListInstance;
    class TextureState;
    class BufferState;
    class InternalCommandList;
    class GraphicsPipeline;

    class IRootSignature : public IResource
    {
    };

    typedef RefCountPtr<IRootSignature> RootSignatureHandle;

    typedef uint32_t DescriptorIndex;
    typedef uint32_t RootParameterIndex;

    struct FormatMapping
    {
        Format abstractFormat;
        DXGI_FORMAT resourceFormat;
        DXGI_FORMAT srvFormat;
        DXGI_FORMAT rtvFormat;
        UINT bitsPerPixel;
        bool isDepthStencil;
    };

    const FormatMapping& GetFormatMapping(Format abstractFormat);

    class StaticDescriptorHeap
    {
    private:
        Device* m_pParent;
        RefCountPtr<ID3D12DescriptorHeap> m_Heap;
        RefCountPtr<ID3D12DescriptorHeap> m_ShaderVisibleHeap;
        D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        D3D12_CPU_DESCRIPTOR_HANDLE m_StartCpuHandle = { 0 };
        D3D12_CPU_DESCRIPTOR_HANDLE m_StartCpuHandleShaderVisible = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE m_StartGpuHandleShaderVisible = { 0 };
        uint32_t m_Stride = 0;
        uint32_t m_NumDescriptors = 0;
        std::vector<bool> m_AllocatedDescriptors;
        DescriptorIndex m_SearchStart = 0;
        uint32_t m_NumAllocatedDescriptors = 0;

        HRESULT Grow();
    public:
        StaticDescriptorHeap(Device* pParent);
        HRESULT AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool shaderVisible);
        DescriptorIndex AllocateDescriptors(uint32_t count);
        DescriptorIndex AllocateDescriptor();
        void ReleaseDescriptors(DescriptorIndex baseIndex, uint32_t count);
        void ReleaseDescriptor(DescriptorIndex index);
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(DescriptorIndex index);
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandleShaderVisible(DescriptorIndex index);
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(DescriptorIndex index);
        ID3D12DescriptorHeap* GetHeap() const;
        ID3D12DescriptorHeap* GetShaderVisibleHeap() const;
        void CopyToShaderVisibleHeap(DescriptorIndex index, uint32_t count = 1);
    };

    class UploadManager
    {
    private:
        friend class DxrScratchManager;
        struct Chunk;

        Device* m_pParent;
        size_t m_DefaultChunkSize = 0;

        std::list<std::shared_ptr<Chunk>> m_ChunkPool;
        std::shared_ptr<Chunk> m_CurrentChunk;

        std::shared_ptr<Chunk> CreateChunk(size_t size);

    public:
        UploadManager(Device* pParent, size_t defaultChunkSize);

        bool SuballocateBuffer(size_t size, ID3D12Resource** pBuffer, size_t* pOffset, void** pCpuVA, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, UINT64 currentFence, UINT64 completedFence, uint32_t alignment = 256);
    };

    class DxrScratchManager
    {
    private:
        Device* m_pParent;
        size_t m_DefaultChunkSize = 0;
        size_t m_MaxTotalMemory = 0;
        size_t m_AllocatedMemory = 0;

        std::list<std::shared_ptr<UploadManager::Chunk>> m_ChunkPool;
        std::shared_ptr<UploadManager::Chunk> m_CurrentChunk;

        std::shared_ptr<UploadManager::Chunk> CreateChunk(size_t size);

    public:
        DxrScratchManager(Device* pParent, size_t defaultChunkSize, size_t maxTotalMemory);

        bool SuballocateBuffer(ID3D12GraphicsCommandList* pCommandList, size_t size, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, UINT64 currentFence, UINT64 completedFence, uint32_t alignment = 256);
        size_t GetMaxTotalMemory() const { return m_MaxTotalMemory; }
        size_t GetAllocatedMemory() const { return m_AllocatedMemory; }
    };

    class CommandList : public RefCounter<ICommandList>
    {
    private:
        friend class Device;

        template<typename T>
        struct VolatileBufferBinding
        {
            uint32_t bindingPoint; // RootParameterIndex or vertex buffer slot, depending on usage
            const BufferState* bufferState;
            T view;
        };

        typedef VolatileBufferBinding<D3D12_GPU_VIRTUAL_ADDRESS> VolatileConstantBufferBinding;
        typedef VolatileBufferBinding<D3D12_INDEX_BUFFER_VIEW> VolatileIndexBufferBinding;
        typedef VolatileBufferBinding<D3D12_VERTEX_BUFFER_VIEW> VolatileVertexBufferBinding;

        RefCountPtr<Device> m_device;
        UploadManager m_upload;
        DxrScratchManager m_dxrScratch;
        std::vector<D3D12_RESOURCE_BARRIER> m_barrier;

        std::shared_ptr<InternalCommandList> m_ActiveCommandList;
        std::list<std::shared_ptr<InternalCommandList>> m_CommandListPool;
        std::shared_ptr<CommandListInstance> m_Instance;

        // Cache for user-provided state

        GraphicsState m_CurrentGraphicsState;
        ComputeState m_CurrentComputeState;
        rt::State m_CurrentRayTracingState;
        bool m_CurrentGraphicsStateValid = false;
        bool m_CurrentComputeStateValid = false;
        bool m_CurrentRayTracingStateValid = false;

        // Cache for internal state

        ID3D12DescriptorHeap* m_CurrentHeapSRVetc = nullptr;
        ID3D12DescriptorHeap* m_CurrentHeapSamplers = nullptr;
        ID3D12Resource* m_CurrentUploadBuffer = nullptr;
        SinglePassStereoState m_CurrentSinglePassStereoState;

        RefCountPtr<ID3D12Fence> m_fence;
        uint64_t m_recordingInstanceID = 0;
        uint64_t m_completedInstanceID = 0;

        // Texture, buffer, and shader table state tracking structures

        std::unordered_map<ITexture*, std::unique_ptr<TextureState>> m_textureStates;
        std::unordered_map<IBuffer*, std::unique_ptr<BufferState>> m_bufferStates;

        // Deferred transitions of textures and buffers to permanent states.
        // They are executed only when the command list is executed, not when the app calls endTrackingTextureState.

        std::vector<std::pair<TextureHandle, D3D12_RESOURCE_STATES>> m_permanentTextureStates;
        std::vector<std::pair<BufferHandle, D3D12_RESOURCE_STATES>> m_permanentBufferStates;

        // Bound volatile buffer state. Saves currently bound volatile buffers and their current GPU VAs.
        // Necessary to patch the bound VAs when a buffer is updated between setGraphicsState and draw, or between draws.

        static constexpr uint32_t MaxVolatileConstantBuffers = MaxVolatileConstantBuffersPerLayout * MaxBindingLayouts;
        static_vector<VolatileConstantBufferBinding, MaxVolatileConstantBuffers> m_currentGraphicsVolatileCBs;
        static_vector<VolatileConstantBufferBinding, MaxVolatileConstantBuffers> m_currentComputeVolatileCBs;
        VolatileIndexBufferBinding m_currentVolatileIndexBuffer;
        static_vector<VolatileVertexBufferBinding, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> m_currentVolatileVertexBuffers;

        // Save strong references to volatile vertex and index buffers. 
        // The reason is that they are not stored in the "referencedResources" vector, but they still need to
        // be alive until the next state change to avoid dangling pointers in the state cache. 
        // The underlying upload manager will be alive no matter what, so the GPU is good.
        // Volatile *constant* buffers do not need to be saved like this because they're bound through binding sets,
        // and the binding sets have strong references to them already.

        BufferHandle m_currentVolatileIndexBufferHandle;
        static_vector<BufferHandle, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> m_currentVolatileVertexBufferHandles;

#if NVRHI_WITH_DXR
        std::unordered_map<rt::IShaderTable*, std::unique_ptr<dxr::ShaderTableState>> m_shaderTableStates;
        dxr::ShaderTableState* getShaderTableStateTracking(rt::IShaderTable* shaderTable);
#endif

        TextureState* getTextureStateTracking(ITexture* texture, bool allowCreate);
        BufferState* getBufferStateTracking(IBuffer* buffer, bool allowCreate);

        void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0);

        void requireTextureState(ITexture* texture, uint32_t subresource, uint32_t state, bool& anyUavBarrier);
        void requireTextureState(ITexture* texture, TextureSubresourceSet subresources, uint32_t state);
        void requireBufferState(IBuffer* buffer, uint32_t state);
        void commitBarriers();
        void clearStateCache();

        void bindGraphicsPipeline(GraphicsPipeline *pso);
        void bindFramebuffer(GraphicsPipeline *pso, Framebuffer *fb);

        std::shared_ptr<InternalCommandList> createInternalCommandList();
        std::shared_ptr<CommandListInstance> execute(ID3D12CommandQueue* pQueue);

        void keepBufferInitialStates();
        void keepTextureInitialStates();

    protected:

        CommandList(Device* device, const CommandListParameters& params);

    public:
        CommandList & operator=(const CommandList& other); // undefined

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
        virtual void dispatchIndirect(uint32_t offsetBytes) override;

        virtual void setRayTracingState(const rt::State& state) override;
        virtual void dispatchRays(const rt::DispatchRaysArguments& args) override;

        virtual void buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::BottomLevelAccelStructDesc& desc) override;
        virtual void buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::TopLevelAccelStructDesc& desc) override;

        virtual void beginTimerQuery(ITimerQuery* query) override;
        virtual void endTimerQuery(ITimerQuery* query) override;

        // perf markers
        virtual void beginMarker(const char *name) override;
        virtual void endMarker() override;

        virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override;
        virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override;

        virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits) override;
        virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits) override;

        virtual void endTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent) override;
        virtual void endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent) override;

        virtual ResourceStates::Enum getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override;
        virtual ResourceStates::Enum getBufferState(IBuffer* buffer) override;

        virtual IDevice* getDevice() override;

        // D3D12 specific methods

        bool allocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress);
        D3D12_GPU_VIRTUAL_ADDRESS getBufferGpuVA(IBuffer* buffer);
        bool commitDescriptorHeaps();

        void updateGraphicsVolatileConstantBuffers();
        void updateGraphicsVolatileIndexBuffer();
        void updateGraphicsVolatileVertexBuffers();
        void updateGraphicsVolatileBuffers();
        void updateComputeVolatileBuffers();
    };

    class Device : public RefCounter<IDevice>
    {
    public:
        Device(IMessageCallback* errorCB, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue);
        virtual ~Device();

    private:
        friend class DescriptorHeap;
        friend class StaticDescriptorHeap;
        friend class UploadManager;
        friend class DxrScratchManager;
        friend class Texture;
        friend class Buffer;
        friend class Sampler;
        friend class CommandList;
        friend class TimerQuery;
        friend class RootSignature;
        friend class BindingSet;
        friend class Framebuffer;

        StaticDescriptorHeap m_dhRTV;
        StaticDescriptorHeap m_dhDSV;
        StaticDescriptorHeap m_dhSRVetc;
        StaticDescriptorHeap m_dhSamplers;

        // The cache does not own the RS objects, so store weak references
        std::map<uint32_t, RootSignature*> m_rootsigCache;

        RefCountPtr<ID3D12CommandSignature> m_drawIndirectSignature;
        RefCountPtr<ID3D12CommandSignature> m_dispatchIndirectSignature;

        static const uint32_t numTimerQueries = 256;
        std::bitset<numTimerQueries> m_allocatedQueries;
        RefCountPtr<ID3D12QueryHeap> m_timerQueryHeap;
        uint32_t m_nextTimerQueryIndex;
        RefCountPtr<Buffer> m_timerQueryResolveBuffer;

        IMessageCallback* m_pMessageCallback;
        RefCountPtr<ID3D12Device> m_pDevice;
#if NVRHI_WITH_DXR
        RefCountPtr<ID3D12Device5> m_pDevice5;
#endif
        RefCountPtr<ID3D12CommandQueue> m_pCommandQueue;
        HANDLE m_fenceEvent;

        std::queue<std::shared_ptr<CommandListInstance>> m_CommandListsInFlight;

        bool m_NvapiIsInitialized;
        bool m_SinglePassStereoSupported;
        bool m_RayTracingSupported = false;
        
        std::unordered_map<DXGI_FORMAT, uint8_t> m_DxgiFormatPlaneCounts;
        uint8_t GetFormatPlaneCount(DXGI_FORMAT format);

        Device& operator=(const Device& other); //undefined
        void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0);
        RefCountPtr<RootSignature> getRootSignature(const static_vector<BindingLayoutHandle, MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout);
        RefCountPtr<ID3D12PipelineState> createPipelineState(const GraphicsPipelineDesc& desc, RootSignature* pRS, const FramebufferInfo& fbinfo);
        RefCountPtr<ID3D12PipelineState> createPipelineState(const ComputePipelineDesc& desc, RootSignature* pRS);
        void createCBV(size_t descriptor, IBuffer* cbuffer);
        void createTextureSRV(size_t descriptor, ITexture* texture, Format format, TextureSubresourceSet subresources);
        void createTextureUAV(size_t descriptor, ITexture* texture, Format format, TextureSubresourceSet subresources);
        void createBufferSRV(size_t descriptor, IBuffer* buffer, Format format, BufferRange range);
        void createBufferUAV(size_t descriptor, IBuffer* buffer, Format format, BufferRange range);
        void createSamplerView(size_t descriptor, ISampler* sampler);
        void createTextureRTV(size_t descriptor, ITexture* texture, Format format, TextureSubresourceSet subresources);
        void createTextureDSV(size_t descriptor, ITexture* texture, TextureSubresourceSet subresources, bool isReadOnly = false);
        void releaseFramebufferViews(Framebuffer* framebuffer);
        void releaseTextureViews(ITexture* texture);
        void releaseBufferViews(IBuffer* buffer);
        void releaseBindingSetViews(BindingSet* bindingSet);
        void removeRootSignatureFromCache(RootSignature* rootsig);

        void createResourceBindingsForStage(BindingSet* bindingSet, ShaderType::Enum stage, const StageBindingLayout* layout, const StageBindingSetDesc& bindings);

        D3D12_RESOURCE_DESC createTextureResourceDesc(const TextureDesc& d);
        void postCreateTextureObject(Texture* texture, const D3D12_RESOURCE_DESC& desc);
        void postCreateBufferObject(Buffer* buffer);

        uint32_t allocateTimerQuerySlot();
        void releaseTimerQuerySlot(uint32_t slot);

    public:

        // IResource implementation

        unsigned long AddRef() override { return RefCounter::AddRef(); }
        unsigned long Release() override { return RefCounter::Release(); }
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
        virtual IMessageCallback* getMessageCallback() override { return m_pMessageCallback; }

        // D3D12 specific methods

        StaticDescriptorHeap& GetRenderTargetViewDescriptorHeap() { return m_dhRTV; }
        StaticDescriptorHeap& GetDepthStencilViewDescriptorHeap() { return m_dhDSV; }
        StaticDescriptorHeap& GetShaderResourceViewDescriptorHeap() { return m_dhSRVetc; }
        StaticDescriptorHeap& GetSamplerHeap() { return m_dhSamplers; }

        RootSignatureHandle buildRootSignature(const static_vector<BindingLayoutHandle, MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout, bool isLocal, const D3D12_ROOT_PARAMETER1* pCustomParameters = nullptr, uint32_t numCustomParameters = 0);
        GraphicsPipelineHandle createHandleForNativeGraphicsPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const GraphicsPipelineDesc& desc, const FramebufferInfo& framebufferInfo);
    };

    // helper function for texture subresource calculations
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn705766(v=vs.85).aspx
    inline static uint32_t calcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t PlaneSlice, uint32_t MipLevels, uint32_t ArraySize)
    {
        return MipSlice + (ArraySlice * MipLevels) + (PlaneSlice * MipLevels * ArraySize);
    }

    void TranslateBlendState(const BlendState& inState, D3D12_BLEND_DESC& outState);
    void TranslateDepthStencilState(const DepthStencilState& inState, D3D12_DEPTH_STENCIL_DESC& outState);
    void TranslateRasterizerState(const RasterState& inState, D3D12_RASTERIZER_DESC& outState);

} // namespace d3d12
} // namespace nvrhi
