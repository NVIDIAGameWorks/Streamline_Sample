/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/nvrhi.h>
#include <nvrhi/d3d12/d3d12.h>
#include <nvrhi/common/resourcebindingmap.h>

#include <bitset>
#include <memory>
#include <functional>

#define INVALID_DESCRIPTOR_INDEX (~0u)
#ifdef _DEBUG
#define SIGNAL_ERROR(msg) this->message(MessageSeverity::Error, msg, __FILE__, __LINE__)
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg, __FILE__, __LINE__)
#else
#define SIGNAL_ERROR(msg) this->message(MessageSeverity::Error, msg)
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg)
#endif

#ifndef NVRHI_D3D12_WITH_NVAPI
#define NVRHI_D3D12_WITH_NVAPI 1
#endif

#if NVRHI_D3D12_WITH_NVAPI
#include <nvapi.h>
#endif

#define RESOURCE_STATE_UNKNOWN D3D12_RESOURCE_STATES(~0u)

template<typename T> T Align(T size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(T(alignment) - 1);
}

template<typename T, typename U> bool arraysAreDifferent(const T& a, const U& b)
{
    if (a.size() != b.size())
        return true;

    for (uint32_t i = 0; i < uint32_t(a.size()); i++)
    {
        if (a[i] != b[i])
            return true;
    }

    return false;
}

namespace nvrhi
{
namespace d3d12
{

class Shader : public RefCounter<IShader>
{
public:
    ShaderDesc desc;
    std::vector<char> bytecode;
    std::bitset<128> slotsSRV;
    std::bitset<16> slotsUAV;
    std::bitset<128> slotsSampler;
    std::bitset<16> slotsCB;
#if NVRHI_D3D12_WITH_NVAPI
    std::vector<NVAPI_D3D12_PSO_EXTENSION_DESC*> extensions;
    std::vector<NV_CUSTOM_SEMANTIC> customSemantics;
    std::vector<uint32_t> coordinateSwizzling;
#endif

    Shader()
        : desc(ShaderType::SHADER_VERTEX)
    {
    }

    const ShaderDesc& GetDesc() const override { return desc; }

    virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const override
    {
        if (ppBytecode) *ppBytecode = bytecode.data();
        if (pSize) *pSize = bytecode.size();
    }
};

class ShaderLibrary;

class ShaderLibraryEntry : public RefCounter<IShader>
{
public:
    ShaderDesc desc;
    RefCountPtr<IShaderLibrary> library;

    ShaderLibraryEntry(IShaderLibrary* pLibrary, const char* entryName, ShaderType::Enum shaderType)
        : desc(shaderType)
        , library(pLibrary)
    {
        desc.entryName = entryName;
    }

    const ShaderDesc& GetDesc() const override { return desc; }

    virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const override
    {
        library->GetBytecode(ppBytecode, pSize);
    }
};

class ShaderLibrary : public RefCounter<IShaderLibrary>
{
public:
    std::vector<char> bytecode;

    virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const override
    {
        if (ppBytecode) *ppBytecode = bytecode.data();
        if (pSize) *pSize = bytecode.size();
    }

    virtual ShaderHandle GetShader(const char* entryName, ShaderType::Enum shaderType) override
    {
        return ShaderHandle::Create(new ShaderLibraryEntry(this, entryName, shaderType));
    }
};

class Texture : public RefCounter<ITexture>
{
public:
    Device* parent;
    TextureDesc desc;
    RefCountPtr<ID3D12Resource> resource;
    TextureBindingKey_HashMap<DescriptorIndex> renderTargetViews;
    TextureBindingKey_HashMap<DescriptorIndex> depthStencilViews;
    TextureBindingKey_HashMap<DescriptorIndex> customSRVs;
    TextureBindingKey_HashMap<DescriptorIndex> customUAVs;
    std::vector<DescriptorIndex> clearMipLevelUAVs;
    uint8_t planeCount = 1;
    D3D12_RESOURCE_STATES permanentState = D3D12_RESOURCE_STATE_COMMON;

    Texture(Device* _parent)
        : parent(_parent)
    { }

    virtual ~Texture();
    bool IsPermanent() const { return permanentState != D3D12_RESOURCE_STATE_COMMON; }

    const TextureDesc& GetDesc() const override { return desc; }

    virtual Object getNativeObject(ObjectType objectType) override;
    virtual Object getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV = false) override;
};

class Buffer : public RefCounter<IBuffer>
{
public:
    Device* parent;
    BufferDesc desc;
    RefCountPtr<ID3D12Resource> resource;
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA;
    DescriptorIndex clearUAV;
    D3D12_RESOURCE_STATES permanentState = D3D12_RESOURCE_STATE_COMMON;

    RefCountPtr<ID3D12Fence> lastUseFence;
    uint64_t lastUseFenceValue = 0;

    Buffer(Device* _parent)
        : parent(_parent)
        , clearUAV(INVALID_DESCRIPTOR_INDEX)
    { }

    virtual ~Buffer();
    bool IsPermanent() const { return permanentState != D3D12_RESOURCE_STATE_COMMON; }

    const BufferDesc& GetDesc() const override { return desc; }

    virtual Object getNativeObject(ObjectType objectType) override;
};

class StagingTexture : public RefCounter<IStagingTexture>
{
public:
    Device* parent;
    TextureDesc desc;
    D3D12_RESOURCE_DESC resourceDesc;
    RefCountPtr<Buffer> buffer;
    CpuAccessMode cpuAccess;
    nvrhi::vector<UINT64> subresourceOffsets;

    RefCountPtr<ID3D12Fence> lastUseFence;
    uint64_t lastUseFenceValue = 0;

    struct SliceRegion
    {
        // offset and size in bytes of this region inside the buffer
        off_t offset = 0;
        size_t size = 0;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    };

    SliceRegion mappedRegion;
    CpuAccessMode mappedAccess = CpuAccessMode::None;

    // returns a SliceRegion struct corresponding to the subresource that slice points at
    // note that this always returns the entire subresource
    SliceRegion getSliceRegion(ID3D12Device *device, const TextureSlice& slice);

    // returns the total size in bytes required for this staging texture
    size_t getSizeInBytes(ID3D12Device *device);

    void computeSubresourceOffsets(ID3D12Device *device);

    StagingTexture(Device* _parent) : parent(_parent) { }
    const TextureDesc& GetDesc() const override { return desc; }
};

class Sampler : public RefCounter<ISampler>
{
public:
    Device* parent;
    SamplerDesc desc;
    DescriptorIndex view;

    Sampler(Device* _parent)
        : parent(_parent)
        , view(INVALID_DESCRIPTOR_INDEX)
    { }

    const SamplerDesc& GetDesc() const override { return desc; }
};

class InputLayout : public RefCounter<IInputLayout>
{
public:
    Device* parent;
    std::vector<VertexAttributeDesc> attributes;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;

    // maps a binding slot to an element stride
    nvrhi::unordered_map<uint32_t, uint32_t> elementStrides;

    InputLayout(Device* _parent)
        : parent(_parent)
    { }

    uint32_t GetNumAttributes() const override { return uint32_t(attributes.size()); }
    const VertexAttributeDesc* GetAttributeDesc(uint32_t index) const { if (index < uint32_t(attributes.size())) return &attributes[index]; else return nullptr; }
};

class EventQuery : public RefCounter<IEventQuery>
{
public:
    Device* parent;

    RefCountPtr<ID3D12Fence> fence;
    uint64_t fenceCounter = 0;
    bool started = false;
    bool resolved = false;

    EventQuery(Device* _parent)
        : parent(_parent)
    { }
};

class TimerQuery : public RefCounter<ITimerQuery>
{
public:
    Device* parent;
    uint32_t beginQueryIndex = 0;
    uint32_t endQueryIndex = 0;

    RefCountPtr<ID3D12Fence> fence;
    uint64_t fenceCounter = 0;

    bool started = false;
    bool resolved = false;
    float time = 0.f;

    TimerQuery(Device* _parent)
        : parent(_parent)
    { }

    virtual ~TimerQuery();
};

class StageBindingLayout
{
public:
    ShaderType::Enum shaderType;
    RootParameterIndex rootParameterSRVetc = ~0u;
    RootParameterIndex rootParameterSamplers = ~0u;
    int descriptorTableSizeSRVetc = 0;
    int descriptorTableSizeSamplers = 0;
    std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRangesSRVetc;
    std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRangesSamplers;
    std::vector<BindingLayoutItem> bindingLayoutsSRVetc;
    static_vector<std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>, MaxVolatileConstantBuffersPerLayout> rootParametersVolatileCB;

    StageBindingLayout(const StageBindingLayoutDesc& layout, ShaderType::Enum shaderType);
};

class BindingLayout : public RefCounter<IBindingLayout>
{
public:
    BindingLayoutDesc desc;
    std::shared_ptr<StageBindingLayout> stages[7];
    static_vector<D3D12_ROOT_PARAMETER1, 32> rootParameters;

    BindingLayout(const BindingLayoutDesc& desc);

    virtual const BindingLayoutDesc& GetDesc() const override { return desc; }
};

class RootSignature : public RefCounter<IRootSignature>
{
public:
    Device* parent;
    uint32_t hash;
    static_vector<std::pair<RefCountPtr<BindingLayout>, RootParameterIndex>, MaxBindingLayouts> pipelineLayouts;
    RefCountPtr<ID3D12RootSignature> handle;
    
    RootSignature(Device* _parent)
        : parent(_parent)
        , hash(0)
    { }
    virtual ~RootSignature();
    virtual Object getNativeObject(ObjectType objectType);
};

class Framebuffer : public RefCounter<IFramebuffer>
{
public:
    Device* parent;
    ULONG refCount;
    FramebufferDesc desc;
    FramebufferInfo framebufferInfo;

    static_vector<TextureHandle, FramebufferDesc::MAX_RENDER_TARGETS + 1> textures;
    static_vector<DescriptorIndex, FramebufferDesc::MAX_RENDER_TARGETS> RTVs;
    DescriptorIndex DSV = INVALID_DESCRIPTOR_INDEX;
    uint32_t rtWidth = 0;
    uint32_t rtHeight = 0;

    Framebuffer(Device* _parent)
        : parent(_parent)
    { }

    virtual ~Framebuffer();

    virtual const FramebufferDesc& GetDesc() const override { return desc; }
    virtual const FramebufferInfo& GetFramebufferInfo() const { return framebufferInfo; }
};

struct DX12_ViewportState
{
    UINT numViewports = 0;
    D3D12_VIEWPORT viewports[16] = {};
    UINT numScissorRects = 0;
    D3D12_RECT scissorRects[16] = {};
};

class GraphicsPipeline : public RefCounter<IGraphicsPipeline>
{
public:
    Device* parent;
    GraphicsPipelineDesc desc;
    FramebufferInfo framebufferInfo;

    RefCountPtr<RootSignature> rootSignature;
    RefCountPtr<ID3D12PipelineState> pipelineState;

    DX12_ViewportState viewportState;

    bool requiresBlendFactors = false;

    GraphicsPipeline(Device* _parent)
        : parent(_parent)
    { }

    virtual const GraphicsPipelineDesc& GetDesc() const override { return desc; }
    virtual const FramebufferInfo& GetFramebufferInfo() const { return framebufferInfo; }
    virtual Object getNativeObject(ObjectType objectType);
};

class ComputePipeline : public RefCounter<IComputePipeline>
{
public:
    Device* parent;
    ComputePipelineDesc desc;

    RefCountPtr<RootSignature> rootSignature;
    RefCountPtr<ID3D12PipelineState> pipelineState;

    ComputePipeline(Device* _parent)
        : parent(_parent)
    { }

    const ComputePipelineDesc& GetDesc() const override { return desc; }
    virtual Object getNativeObject(ObjectType objectType);
};

class InternalCommandList
{
public:
    RefCountPtr<ID3D12CommandAllocator> allocator;
    RefCountPtr<ID3D12GraphicsCommandList> commandList;
#if NVRHI_WITH_DXR
    RefCountPtr<ID3D12GraphicsCommandList4> commandList4;
#endif
    uint64_t lastInstanceID;
};

class BindingSet : public RefCounter<IBindingSet>
{
public:
    Device* parent;
    RefCountPtr<BindingLayout> layout;
    BindingSetDesc desc;

    // ShaderType -> DescriptorIndex
    DescriptorIndex descriptorTablesSRVetc[7];
    DescriptorIndex descriptorTablesSamplers[7];
    RootParameterIndex rootParameterIndicesSRVetc[7];
    RootParameterIndex rootParameterIndicesSamplers[7];
    bool descriptorTablesValidSRVetc[7];
    bool descriptorTablesValidSamplers[7];

    static_vector<std::pair<RootParameterIndex, IBuffer*>, MaxVolatileConstantBuffersPerLayout> rootParametersVolatileCB[7];

    std::vector<std::function<void(CommandList*, IBuffer*, bool&)>> barrierSetup;
    std::vector<RefCountPtr<IResource>> resources;

    BindingSet(Device* _parent)
        : parent(_parent)
        , descriptorTablesSRVetc{ 0 }
        , descriptorTablesSamplers{ 0 }
        , rootParameterIndicesSRVetc{ 0 }
        , rootParameterIndicesSamplers{ 0 }
        , descriptorTablesValidSRVetc{ 0 }
        , descriptorTablesValidSamplers{ 0 }
    { }

    virtual ~BindingSet();

    virtual const BindingSetDesc& GetDesc() const override { return desc; }
    virtual IBindingLayout* GetLayout() const override { return layout; }
};

DX12_ViewportState convertViewportState(const GraphicsPipeline *pso, const ViewportState& vpState);

class CommandListInstance
{
public:
    uint64_t instanceID;
    RefCountPtr<ID3D12Fence> fence;
    RefCountPtr<ID3D12CommandAllocator> commandAllocator;
    RefCountPtr<ID3D12CommandList> commandList;
    std::vector<RefCountPtr<IResource>> referencedResources;
    std::vector<RefCountPtr<IUnknown>> referencedNativeResources;
    std::vector<RefCountPtr<StagingTexture>> referencedStagingTextures;
    std::vector<RefCountPtr<Buffer>> referencedStagingBuffers;
    std::vector<RefCountPtr<TimerQuery>> referencedTimerQueries;
};

class TextureState
{
public:
    std::vector<D3D12_RESOURCE_STATES> subresourceStates;
    bool enableUavBarriers = true;
    bool firstUavBarrierPlaced = false;
    bool permanentTransition = false;

    TextureState(uint32_t numSubresources)
    {
        subresourceStates.resize(numSubresources, RESOURCE_STATE_UNKNOWN);
    }
};

class BufferState
{
public:
    D3D12_RESOURCE_STATES state = RESOURCE_STATE_UNKNOWN;
    bool enableUavBarriers = true;
    bool firstUavBarrierPlaced = false;
    D3D12_GPU_VIRTUAL_ADDRESS volatileData = 0;
    bool permanentTransition = false;
};

D3D12_RESOURCE_STATES TranslateResourceStates(ResourceStates::Enum stateBits);
ResourceStates::Enum TranslateResourceStatesFromD3D(D3D12_RESOURCE_STATES stateBits);

} // namespace d3d12

#if NVRHI_WITH_DXR
namespace dxr
{
    class AccelStruct : public RefCounter<rt::IAccelStruct>
    {
    public:
        RefCountPtr<d3d12::Buffer> dataBuffer;
        std::vector<rt::AccelStructHandle> bottomLevelASes;
        size_t scratchBufferSize = 0;
        bool isTopLevel = false;
        bool trackLiveness = true;

        virtual Object getNativeObject(ObjectType objectType) override;
    };

    class Pipeline : public RefCounter<rt::IPipeline>
    {
    public:
        d3d12::Device* parent;
        rt::PipelineDesc desc;

        std::unordered_map<IBindingLayout*, d3d12::RootSignatureHandle> localRootSignatures;
        RefCountPtr<d3d12::RootSignature> globalRootSignature;
        RefCountPtr<ID3D12StateObject> pipelineState;
        RefCountPtr<ID3D12StateObjectProperties> pipelineInfo;

        struct ExportTableEntry
        {
            IBindingLayout* bindingLayout;
            const void* pShaderIdentifier;
        };

        std::unordered_map<std::string, ExportTableEntry> exports;
        uint32_t maxLocalRootParameters;

        Pipeline(d3d12::Device* _parent)
            : parent(_parent)
        { }

        const ExportTableEntry* GetExport(const char* name);
        uint32_t GetShaderTableEntrySize() const;

        const rt::PipelineDesc& GetDesc() const override { return desc; }
        virtual rt::ShaderTableHandle CreateShaderTable() override;
    };

    class ShaderTable : public RefCounter<rt::IShaderTable>
    {
    public:
        struct Entry
        {
            const void* pShaderIdentifier;
            BindingSetHandle localBindings;
        };

        RefCountPtr<Pipeline> pipeline;

        Entry rayGenerationShader = {};
        std::vector<Entry> missShaders;
        std::vector<Entry> callableShaders;
        std::vector<Entry> hitGroups;

        uint32_t version = 0;

        ShaderTable(Pipeline* _pipeline);
        uint32_t GetNumEntries() const;

        virtual void SetRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        virtual int AddMissShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        virtual int AddHitGroup(const char* exportName, IBindingSet* bindings = nullptr) override;
        virtual int AddCallableShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        virtual void ClearMissShaders() override;
        virtual void ClearHitShaders() override;
        virtual void ClearCallableShaders() override;
        virtual rt::IPipeline* GetPipeline() override;

    private:
        bool VerifyExport(const Pipeline::ExportTableEntry* pExport, IBindingSet* bindings);
    };


    class ShaderTableState
    {
    public:
        uint32_t committedVersion = 0;
        ID3D12DescriptorHeap* descriptorHeapSRV = nullptr;
        ID3D12DescriptorHeap* descriptorHeapSamplers = nullptr;
        D3D12_DISPATCH_RAYS_DESC dispatchRaysTemplate = {};
    };

} // namespace dxr
#endif
} // namespace nvrhi