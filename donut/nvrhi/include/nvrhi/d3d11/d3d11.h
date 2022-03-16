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
#include <nvrhi/common/resourcebindingmap.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <map>
#include <vector>
#include <set>

namespace nvrhi
{
    namespace ObjectTypes
    {
        constexpr ObjectType Nvrhi_D3D11_Device = 0x00010101;
    };

namespace d3d11
{

    class Device;

    class Texture : public RefCounter<ITexture>
    {
    public:
        Device* parent;
        TextureDesc desc;
        RefCountPtr<ID3D11Resource> resource;
        TextureBindingKey_HashMap<RefCountPtr<ID3D11ShaderResourceView>> shaderResourceViews;
        TextureBindingKey_HashMap<RefCountPtr<ID3D11RenderTargetView>> renderTargetViews;
        TextureBindingKey_HashMap<RefCountPtr<ID3D11DepthStencilView>> depthStencilViews;
        TextureBindingKey_HashMap<RefCountPtr<ID3D11UnorderedAccessView>> unorderedAccessViews;

        Texture(Device* _parent) : parent(_parent) { }
        const TextureDesc& GetDesc() const override { return desc; }
        virtual Object getNativeObject(ObjectType objectType) override;
        virtual Object getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV = false) override;
    };

    class StagingTexture : public RefCounter<IStagingTexture>
    {
    public:
        Device* parent;
        RefCountPtr<Texture> texture;
        CpuAccessMode cpuAccess = CpuAccessMode::None;
        UINT mappedSubresource = UINT(-1);

        StagingTexture(Device* _parent) : parent(_parent) { }
        const TextureDesc& GetDesc() const override { return texture->GetDesc(); }
    };

    class Buffer : public RefCounter<IBuffer>
    {
    public:
        Device* parent;
        BufferDesc desc;
        RefCountPtr<ID3D11Buffer> resource;
        RefCountPtr<ID3D11Buffer> stagingBuffer;
        unordered_map<BufferBindingKey, RefCountPtr<ID3D11ShaderResourceView>> shaderResourceViews;
        unordered_map<BufferBindingKey, RefCountPtr<ID3D11UnorderedAccessView>> unorderedAccessViews;

        Buffer(Device* _parent) : parent(_parent) { }
        const BufferDesc& GetDesc() const override { return desc; }
        virtual Object getNativeObject(ObjectType objectType) override;
    };

    class Shader : public RefCounter<IShader>
    {
    public:
        Device* parent;
        ShaderDesc desc;
        RefCountPtr<ID3D11VertexShader> VS;
        RefCountPtr<ID3D11HullShader> HS;
        RefCountPtr<ID3D11DomainShader> DS;
        RefCountPtr<ID3D11GeometryShader> GS;
        RefCountPtr<ID3D11PixelShader> PS;
        RefCountPtr<ID3D11ComputeShader> CS;
        std::vector<char> bytecode;

        Shader(Device* _parent) : parent(_parent), desc(ShaderType::SHADER_VERTEX) { }
        const ShaderDesc& GetDesc() const override { return desc; }

        virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const override
        {
            if (ppBytecode) *ppBytecode = bytecode.data();
            if (pSize) *pSize = bytecode.size();
        }
    };

    class Sampler : public RefCounter<ISampler>
    {
    public:
        Device* parent;
        SamplerDesc desc;
        RefCountPtr<ID3D11SamplerState> sampler;

        Sampler(Device* _parent) : parent(_parent) { }
        const SamplerDesc& GetDesc() const override { return desc; }
    };

    class EventQuery : public RefCounter<IEventQuery>
    {
    public:
        Device* parent;
        RefCountPtr<ID3D11Query> query;
        bool resolved = false;

        EventQuery(Device* _parent) : parent(_parent) { }
    };

    class TimerQuery : public RefCounter<ITimerQuery>
    {
    public:
        Device* parent;
        RefCountPtr<ID3D11Query> start;
        RefCountPtr<ID3D11Query> end;
        RefCountPtr<ID3D11Query> disjoint;

        bool resolved = false;
        float time = 0.f;

        TimerQuery(Device* _parent) : parent(_parent) { }
    };

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

    class InputLayout : public RefCounter<IInputLayout>
    {
    public:
        Device* parent;
        RefCountPtr<ID3D11InputLayout> layout;
        std::vector<VertexAttributeDesc> attributes;
        // maps a binding slot number to a stride
        nvrhi::unordered_map<uint32_t, uint32_t> elementStrides;

        InputLayout(Device* _parent) : parent(_parent) { }
        uint32_t GetNumAttributes() const override { return uint32_t(attributes.size()); }
        const VertexAttributeDesc* GetAttributeDesc(uint32_t index) const { if (index < uint32_t(attributes.size())) return &attributes[index]; else return nullptr; }
    };


    class Framebuffer : public RefCounter<IFramebuffer>
    {
    public:
        Device* parent;
        FramebufferDesc desc;
        FramebufferInfo framebufferInfo;
        nvrhi::static_vector<ID3D11RenderTargetView*, FramebufferDesc::MAX_RENDER_TARGETS> RTVs;
        ID3D11DepthStencilView* pDSV = nullptr;

        Framebuffer(Device* _parent) : parent(_parent) { }
        virtual const FramebufferDesc& GetDesc() const override { return desc; }
        virtual const FramebufferInfo& GetFramebufferInfo() const { return framebufferInfo; }
    };

    struct DX11_ViewportState
    {
        uint32_t numViewports = 0;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX] = {};
        uint32_t numScissorRects = 0;
        D3D11_RECT scissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX] = {};
    };

    class GraphicsPipeline : public RefCounter<IGraphicsPipeline>
    {
    public:
        Device* parent;
        GraphicsPipelineDesc desc;
        FramebufferInfo framebufferInfo;

        D3D11_PRIMITIVE_TOPOLOGY primitiveTopology;
        InputLayout *inputLayout = nullptr;

        DX11_ViewportState viewportState;

        ID3D11RasterizerState *pRS = nullptr;

        ID3D11BlendState *pBlendState = nullptr;
        FLOAT blendFactor[4] = { 0.f };
        ID3D11DepthStencilState *pDepthStencilState = nullptr;
        UINT stencilRef = 0;
        bool pixelShaderHasUAVs = false;

        RefCountPtr<ID3D11VertexShader> pVS;
        RefCountPtr<ID3D11HullShader> pHS;
        RefCountPtr<ID3D11DomainShader> pDS;
        RefCountPtr<ID3D11GeometryShader> pGS;
        RefCountPtr<ID3D11PixelShader> pPS;

        GraphicsPipeline(Device* _parent) : parent(_parent) { }
        virtual const GraphicsPipelineDesc& GetDesc() const override { return desc; }
        virtual const FramebufferInfo& GetFramebufferInfo() const { return framebufferInfo; }
    };

    class ComputePipeline : public RefCounter<IComputePipeline>
    {
    public:
        Device* parent;
        ComputePipelineDesc desc;

        RefCountPtr<ID3D11ComputeShader> shader;
        
        ComputePipeline(Device* _parent) : parent(_parent) { }
        const ComputePipelineDesc& GetDesc() const override { return desc; }
    };

    class PipelineBindingLayout : public RefCounter<IBindingLayout>
    {
    public:
        BindingLayoutDesc desc;

        const BindingLayoutDesc& GetDesc() const override { return desc; }
    };

    class PipelineBindingSet : public RefCounter<IBindingSet>
    {
    public:
        Device* parent;
        BindingSetDesc desc;
        BindingLayoutHandle layout;

        struct StageResourceBindings
        {
            ID3D11ShaderResourceView* SRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            uint32_t minSRVSlot = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
            uint32_t maxSRVSlot = 0;

            ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
            uint32_t minSamplerSlot = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
            uint32_t maxSamplerSlot = 0;

            ID3D11Buffer* constantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            uint32_t minConstantBufferSlot = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
            uint32_t maxConstantBufferSlot = 0;

            ID3D11UnorderedAccessView* UAVs[D3D11_1_UAV_SLOT_COUNT] = {};
            uint32_t minUAVSlot = D3D11_1_UAV_SLOT_COUNT;
            uint32_t maxUAVSlot = 0;

            bool IsSupersetOf(const StageResourceBindings& other) const;
        };

        StageResourceBindings VS;
        StageResourceBindings HS;
        StageResourceBindings DS;
        StageResourceBindings GS;
        StageResourceBindings PS;
        StageResourceBindings CS;

        std::vector<RefCountPtr<IResource>> resources;

        PipelineBindingSet(Device* _parent) : parent(_parent) { }
        virtual const BindingSetDesc& GetDesc() const override { return desc; }
        virtual IBindingLayout* GetLayout() const override { return layout; }
        bool IsSupersetOf(const PipelineBindingSet& other) const;
    };

    class IDeviceAndCommandList : public IDevice, public ICommandList
    {
    };

    class Device : public RefCounter<IDeviceAndCommandList>
    {
    public:
        //The user-visible API
        Device(IMessageCallback* messageCallback, ID3D11DeviceContext* context);
        virtual ~Device();

        //You should not call this while the client is initialized or else resources it is using may be deleted
        void clearCachedData();

        inline ID3D11DeviceContext* GetDeviceContext() const { return context.Get(); }
        inline ID3D11Device* GetDevice() const { return device.Get(); }

        ID3D11ShaderResourceView* getSRVForTexture(ITexture* handle, Format format, TextureSubresourceSet subresources);
        ID3D11RenderTargetView* getRTVForTexture(ITexture* handle, Format format, TextureSubresourceSet subresources);
        ID3D11RenderTargetView* getRTVForAttachment(const FramebufferAttachment& attachment);
        ID3D11DepthStencilView* getDSVForTexture(ITexture* handle, TextureSubresourceSet subresources, bool isReadOnly = false);
        ID3D11DepthStencilView* getDSVForAttachment(const FramebufferAttachment& attachment);
        ID3D11UnorderedAccessView* getUAVForTexture(ITexture* handle, Format format, TextureSubresourceSet subresources);

        ID3D11ShaderResourceView* getSRVForBuffer(IBuffer* resource, Format format, BufferRange range);
        ID3D11UnorderedAccessView* getUAVForBuffer(IBuffer* resource, Format format, BufferRange range);

        ID3D11BlendState* getBlendState(const BlendState& blendState);
        ID3D11DepthStencilState* getDepthStencilState(const DepthStencilState& depthStencilState);
        ID3D11RasterizerState* getRasterizerState(const RasterState& rasterState);
    private:
        Device& operator=(const Device& other); //undefined

    protected:
        RefCountPtr<ID3D11DeviceContext> context;
        RefCountPtr<ID3D11Device> device;
        IMessageCallback* messageCallback;
        bool nvapiIsInitalized;
        RefCountPtr<ID3DUserDefinedAnnotation> userDefinedAnnotation;

        void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0);
        
        std::unordered_map<uint32_t, RefCountPtr<ID3D11BlendState>> blendStates;
        std::unordered_map<uint32_t, RefCountPtr<ID3D11DepthStencilState>> depthStencilStates;
        std::unordered_map<uint32_t, RefCountPtr<ID3D11RasterizerState>> rasterizerStates;

        // State cache.
        // Use strong references (handles) instead of just a copy of GraphicsState etc.
        // If user code creates some object, draws using it, and releases it, a weak pointer would become invalid.
        // Using strong references in all state objects would solve this problem, but it means there will be an extra AddRef/Release cost everywhere.
        
        GraphicsPipelineHandle m_CurrentGraphicsPipeline;
        FramebufferHandle m_CurrentFramebuffer;
        ViewportState m_CurrentDynamicViewports;
        static_vector<BindingSetHandle, MaxBindingLayouts> m_CurrentBindings;
        static_vector<VertexBufferBinding, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> m_CurrentVertexBufferBindings;
        IndexBufferBinding m_CurrentIndexBufferBinding;
        static_vector<BufferHandle, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> m_CurrentVertexBuffers;
        BufferHandle m_CurrentIndexBuffer;
        ComputePipelineHandle m_CurrentComputePipeline;
        SinglePassStereoState m_CurrentSinglePassStereoState;
        BufferHandle m_CurrentIndirectBuffer;
        bool m_CurrentGraphicsStateValid;
        bool m_CurrentComputeStateValid;

        bool m_SinglePassStereoSupported;

        int m_numUAVOverlapCommands = 0;
        void enterUAVOverlapSection();
        void leaveUAVOverlapSection();

        D3D11_BLEND convertBlendValue(BlendState::BlendValue value);
        D3D11_BLEND_OP convertBlendOp(BlendState::BlendOp value);
        D3D11_STENCIL_OP convertStencilOp(DepthStencilState::StencilOp value);
        D3D11_COMPARISON_FUNC convertComparisonFunc(DepthStencilState::ComparisonFunc value);

        //if there are multiple views to clear you can keep calling index until you get all null
        void getClearViewForTexture(ITexture* resource, TextureSubresourceSet subresources, ID3D11UnorderedAccessView*& outUAV, ID3D11RenderTargetView*& outRTV, ID3D11DepthStencilView*& outDSV);

        D3D_PRIMITIVE_TOPOLOGY getPrimType(PrimitiveType::Enum pt);

        void disableSLIResouceSync(ID3D11Resource* resource);

        void copyTexture(ID3D11Resource *dst, const TextureDesc& dstDesc, const TextureSlice& dstSlice,
            ID3D11Resource *src, const TextureDesc& srcDesc, const TextureSlice& srcSlice);

        TextureHandle createTexture(const TextureDesc& d, CpuAccessMode cpuAccess);

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

        virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override;
        virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override;

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
        virtual void executeCommandList(ICommandList* commandList) override { (void)commandList; }
        virtual void waitForIdle() override;
        virtual void runGarbageCollection() override { }
        virtual bool queryFeatureSupport(Feature feature) override;
        virtual IMessageCallback* getMessageCallback() override { return messageCallback; }

        // misc functions

        void bindGraphicsPipeline(const GraphicsPipeline *pso);

        void prepareToBindGraphicsResourceSets(const BindingSetVector& resourceSets, const static_vector<BindingSetHandle, MaxBindingLayouts>* currentResourceSets, bool updateFramebuffer, BindingSetVector& outSetsToBind);
        void bindGraphicsResourceSets(const BindingSetVector& setsToBind);
        void bindComputeResourceSets(const BindingSetVector& resourceSets, const static_vector<BindingSetHandle, MaxBindingLayouts>* currentResourceSets);

        // helper function to set up the shader object
        template <class DX11_Shader_type>
        RefCountPtr<DX11_Shader_type> setupShader(IShader* shader);

        // setup a binding set for a given stage
        void setupStageBindings(PipelineBindingSet* bindingSet, const StageBindingSetDesc& bindings, PipelineBindingSet::StageResourceBindings& target);
    };

} // namespace d3d11
} // namespace nvrhi
