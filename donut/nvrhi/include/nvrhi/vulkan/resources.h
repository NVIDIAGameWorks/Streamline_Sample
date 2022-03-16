#pragma once

#include <nvrhi/common/resourcebindingmap.h>
#include <cstddef>
#include <limits>

namespace nvrhi 
{
namespace vulkan 
{

vk::Viewport VKViewportWithDXCoords(const Viewport& v);

class VulkanSyncObjectPool;

struct MemoryBarrierInfo
{
    vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits();
    vk::AccessFlags accessMask = vk::AccessFlags();
};

struct ImageBarrierInfo : public MemoryBarrierInfo
{
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

class MemoryResource
{
public:
    virtual ~MemoryResource()
    {
        assert(readFence == nullptr);
        assert(writeFence == nullptr);
        assert(readSemaphore == nullptr);
        assert(writeSemaphore == nullptr);
    }

    bool managed = true;
    vk::MemoryPropertyFlags propertyFlags;
    vk::DeviceMemory memory;

    // both semaphores need to be waited on for a write op
    // only writeSemaphore needs to be waited on for a read op
    // either or both can be null at any time
    Semaphore *readSemaphore = nullptr;
    Semaphore *writeSemaphore = nullptr;

    // equivalent CPU-side fences
    Fence *readFence = nullptr;
    Fence *writeFence = nullptr;

    Semaphore *getReadSemaphore() { return readSemaphore; }
    Semaphore *getWriteSemaphore() { return writeSemaphore; }

    void setReadSemaphore(VulkanSyncObjectPool& objectPool, Semaphore *semaphore)
    {
        replaceSemaphore(objectPool, readSemaphore, semaphore);
    }

    void setWriteSemaphore(VulkanSyncObjectPool& objectPool, Semaphore *semaphore)
    {
        replaceSemaphore(objectPool, writeSemaphore, semaphore);
    }

    void setReadFence(VulkanSyncObjectPool& objectPool, Fence *fence)
    {
        replaceFence(objectPool, readFence, fence);
    }

    void setWriteFence(VulkanSyncObjectPool& objectPool, Fence *fence)
    {
        replaceFence(objectPool, writeFence, fence);
    }

private:
    void replaceSemaphore(VulkanSyncObjectPool& objectPool,
                          Semaphore *& semaphoreSlot,
                          Semaphore *newSemaphore)
    {
        if (semaphoreSlot)
        {
            objectPool.releaseSemaphore(semaphoreSlot);
        }

        semaphoreSlot = newSemaphore;

        if (newSemaphore)
        {
            newSemaphore->addref();
        }
    }

    void replaceFence(VulkanSyncObjectPool& objectPool,
                      Fence *& fenceSlot,
                      Fence *newFence)
    {
        if (!(propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible))
        {
            // non-host-visible memory doesn't need fence tracking
            return;
        }

        if (fenceSlot)
        {
            objectPool.releaseFence(fenceSlot);
        }

        fenceSlot = newFence;

        if (newFence)
        {
            newFence->addref();
        }
    }
};

struct TextureSubresource : public TextureSubresourceSet
{
    TextureSubresource()
    {
    }

    TextureSubresource(const TextureSubresourceSet& b)
        : TextureSubresourceSet(b)
    {
    }

    TextureSubresource(MipLevel _baseMipLevel, MipLevel _numMipLevels, ArraySlice _baseArraySlice, ArraySlice _numArraySlices)
        : TextureSubresourceSet(_baseMipLevel, _numMipLevels, _baseArraySlice, _numArraySlices)
    {
    }

    bool operator==(const TextureSubresource& other) const
    {
        return baseMipLevel == other.baseMipLevel &&
                numMipLevels == other.numMipLevels &&
                baseArraySlice == other.baseArraySlice &&
                numArraySlices == other.numArraySlices;
    }
};

struct TextureSubresourceView
{
    Texture& texture;
    TextureSubresource subresource;

    vk::ImageView view = nullptr;
    vk::ImageSubresourceRange subresourceRange;

    TextureSubresourceView(Texture& texture)
        : texture(texture)
    { }

    TextureSubresourceView(const TextureSubresourceView&) = delete;

    bool operator==(const TextureSubresourceView& other) const
    {
        return &texture == &other.texture &&
                subresource == other.subresource &&
                view == other.view &&
                subresourceRange == other.subresourceRange;
    }
};

class Texture : public MemoryResource, public ITexture
{
public:

    enum class TextureSubresourceViewType // see getSubresourceView()
    {
        AllAspects, DepthOnly, StencilOnly
    };

    VulkanContext& context;
    Device* parent;
    unsigned long refCount;

    TextureDesc desc;

    vk::ImageCreateInfo imageInfo;
    vk::Image image;

    // barrier info vector --- one per subresource (indexed by getSubresourceIndex below)
    nvrhi::vector<ImageBarrierInfo> subresourceBarrierStates;

    Texture(VulkanContext& context, Device* parent)
        : context(context)
        , parent(parent)
        , refCount(1)
    { }

    void barrier(TrackedCommandBuffer *cmd,
                 TextureSubresourceView& view,
                 vk::PipelineStageFlags dstStageFlags,
                 vk::AccessFlags dstAccessMask,
                 vk::ImageLayout dstLayout);

    void barrier(TrackedCommandBuffer *cmd,
                 const TextureSubresource& subresource,
                 vk::PipelineStageFlags dstStageFlags,
                 vk::AccessFlags dstAccessMask,
                 vk::ImageLayout dstLayout)
    {
        return barrier(cmd,
                       getSubresourceView(subresource.resolve(desc, false), TextureSubresourceViewType::AllAspects),
                       dstStageFlags,
                       dstAccessMask,
                       dstLayout);
    }

    static void hostDataGetPitchAndRows(nvrhi::Format format, size_t width, size_t height, size_t depth, size_t* rowPitchBytesOut, size_t* numRowsInSliceOut, size_t* slicePitchBytesOut);

    // returns a subresource view for an arbitrary range of mip levels and array layers.  'viewtype' only matters when asking for a depthstencil view; in situations where only depth or stencil can be bound (such as an SRV with ImageLayout::eShaderReadOnlyOptimal), but not both, then this specifies which of the two aspect bits is to be set.
    TextureSubresourceView& getSubresourceView(const TextureSubresourceSet& subresources, TextureSubresourceViewType viewtype = TextureSubresourceViewType::AllAspects);
    
    uint32_t getNumSubresources()
    {
        return desc.mipLevels * desc.arraySize;
    }

    uint32_t getSubresourceIndex(uint32_t mipLevel, uint32_t arrayLayer)
    {
        return mipLevel * desc.arraySize + arrayLayer;
    }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const TextureDesc& GetDesc() const override { return desc; }

    struct Hash
    {
        std::size_t operator()(std::pair<TextureSubresource, TextureSubresourceViewType> const& s) const noexcept
        {
            size_t ret = 0;

#define ADD_MEMBER(__item) \
                ret |= __item; \
                ret <<= sizeof(__item) * 8

            ADD_MEMBER(s.first.baseMipLevel);
            ADD_MEMBER(s.first.numMipLevels);
            ADD_MEMBER(s.first.baseArraySlice);
            ADD_MEMBER(s.first.numArraySlices);
            ADD_MEMBER(uint8_t(s.second));

#undef ADD_MEMBER

            return ret;
        }
    };
    // contains subresource views for this texture
    // note that we only create the views that the app uses, and that multiple views may map to the same subresources
    nvrhi::unordered_map<std::pair<TextureSubresource, TextureSubresourceViewType>, TextureSubresourceView, Texture::Hash> subresourceViews;

    virtual Object getNativeObject(ObjectType objectType) override;
    virtual Object getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV = false) override;
};

class Buffer : public MemoryResource, public IBuffer
{
public:
    Device* parent;
    unsigned long refCount;

    BufferDesc desc;

    vk::BufferCreateInfo bufferInfo;
    vk::Buffer buffer;
    MemoryBarrierInfo barrierState;

    void barrier(TrackedCommandBuffer *cmd,
                 vk::PipelineStageFlags dstStageFlags,
                 vk::AccessFlags dstAccessMask);

    Buffer(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    std::unordered_map<vk::Format, vk::BufferView> viewCache;

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const BufferDesc& GetDesc() const override { return desc; }
    Object getNativeObject(ObjectType type) override;
};

struct StagingTextureRegion
{
    // offset, size in bytes
    off_t offset;
    size_t size;
};

class StagingTexture : public IStagingTexture
{
public:
    Device* parent;
    unsigned long refCount;

    TextureDesc desc;
    // backing store for staging texture is a buffer
    RefCountPtr<Buffer> buffer;
    // per-mip, per-slice regions
    // offset = mipLevel * numDepthSlices + depthSlice
    nvrhi::vector<StagingTextureRegion> sliceRegions;

    size_t computeSliceSize(uint32_t mipLevel);
    const StagingTextureRegion& getSliceRegion(uint32_t mipLevel, uint32_t arraySlice, uint32_t z);
    void populateSliceRegions();

    size_t getBufferSize()
    {
        assert(sliceRegions.size());
        size_t size = sliceRegions.back().offset + sliceRegions.back().size;
        assert(size > 0);
        return size;
    }

    StagingTexture(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const TextureDesc& GetDesc() const override { return desc; }
};

class Sampler : public ISampler
{
public:
    Device* parent;
    unsigned long refCount;

    SamplerDesc desc;

    vk::SamplerCreateInfo samplerInfo;
    vk::Sampler sampler;

    Sampler(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const SamplerDesc& GetDesc() const override { return desc; }
};

class Shader : public IShader
{
public:
    Device* parent;
    unsigned long refCount;
    
    ShaderDesc desc;

    std::string entryName;

    vk::ShaderModuleCreateInfo shaderInfo;
    vk::ShaderModule shaderModule;

    Shader(Device* parent)
        : desc(ShaderType::SHADER_VERTEX)
        , parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const ShaderDesc& GetDesc() const override { return desc; }

    virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const override
    {
        // we don't save these for vulkan
        if (ppBytecode) *ppBytecode = nullptr;
        if (pSize) *pSize = 0;
    }
};

class InputLayout : public IInputLayout
{
public:
    Device* parent;
    unsigned long refCount;

    // the original input data
    nvrhi::vector<VertexAttributeDesc> inputDesc;

    nvrhi::vector<vk::VertexInputBindingDescription> bindingDesc;
    nvrhi::vector<vk::VertexInputAttributeDescription> attributeDesc;

    InputLayout(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    uint32_t GetNumAttributes() const override { return uint32_t(inputDesc.size()); }
    const VertexAttributeDesc* GetAttributeDesc(uint32_t index) const override { if (index < uint32_t(inputDesc.size())) return &inputDesc[index]; else return nullptr; }
};

class EventQuery : public IEventQuery
{
public:
    Device* parent;
    unsigned long refCount;

    Fence *fence;
    bool started = false;
    bool resolved = false;

    EventQuery(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;

};

class TimerQuery : public ITimerQuery
{
public:
    Device* parent;
    unsigned long refCount;

    uint32_t beginQueryIndex = uint32_t(-1);
    uint32_t endQueryIndex = uint32_t(-1);

    bool started = false;
    bool resolved = false;
    float time = 0.f;

    void reset(VulkanContext& context)
    {
        (void)context;
        started = false;
        resolved = false;
        time = 0.f;
    }

    void destroy(VulkanContext& context)
    { 
        (void)context;
    }

    TimerQuery(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    TimerQuery()
        : parent(nullptr)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
};

// a database of barriers required for a given draw state
struct BarrierTracker
{
    nvrhi::unordered_map<Buffer *, MemoryBarrierInfo> bufferBarrierInfo;
    nvrhi::unordered_map<TextureSubresourceView *, ImageBarrierInfo> imageBarrierInfo;

    void update(Buffer *buffer,
                vk::PipelineStageFlags dstStageFlags,
                vk::AccessFlags dstAccessMask);

    void update(TextureSubresourceView *textureView,
                 vk::PipelineStageFlags dstStageFlags,
                 vk::AccessFlags dstAccessMask,
                 vk::ImageLayout dstLayout);

    void execute(TrackedCommandBuffer *cmd);
};

class Framebuffer : public IFramebuffer
{
public:
    Device* parent;
    unsigned long refCount;

    FramebufferDesc desc;
    FramebufferInfo framebufferInfo;

    nvrhi::static_vector<vk::ImageView, FramebufferDesc::MAX_RENDER_TARGETS + 1> attachmentViews;

    uint32_t renderAreaW = 0;
    uint32_t renderAreaH = 0;

    vk::RenderPass renderPass = vk::RenderPass();
    vk::Framebuffer framebuffer = vk::Framebuffer();

    Framebuffer(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    virtual const FramebufferDesc& GetDesc() const override { return desc; };
    virtual const FramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; };
};

// describes compile-time settings for HLSL -> SPIR-V register allocation
struct HLSLCompilerParameters
{
    // controls the number of binding slots reserved for each resource type in each stage at compile time
    static constexpr uint32_t BindingsPerResourceType = 128;

    // describes the binding register offsets within a single stage
    typedef enum
    {
        Texture         = 0,
        Sampler         = Texture + HLSLCompilerParameters::BindingsPerResourceType,
        ConstantBuffer  = Sampler + HLSLCompilerParameters::BindingsPerResourceType,
        UAV             = ConstantBuffer + HLSLCompilerParameters::BindingsPerResourceType,

        Next            = UAV + HLSLCompilerParameters::BindingsPerResourceType,
    } RegisterOffset;

    // describes the base binding location per stage
    typedef enum
    {
        Vertex          = 0,
        TessControl     = Vertex + RegisterOffset::Next,
        TessEval        = TessControl + RegisterOffset::Next,
        Geometry        = TessEval + RegisterOffset::Next,
        Fragment        = Geometry + RegisterOffset::Next,
    } StageOffset;

    // returns the base VK location for a given stage + resource kind
    static uint32_t getBindingBase(vk::ShaderStageFlagBits shaderStage, RegisterOffset registerKind);
};

// wraps a binding layout with VK-specific information
struct BindingLayoutVK : public BindingLayoutItem
{
    BindingLayoutVK() = default;
    BindingLayoutVK(const BindingLayoutVK&) = default;
    BindingLayoutVK(const BindingLayoutItem& other)
        : BindingLayoutItem(other)
    { }

    uint32_t VK_location = uint32_t(-1);
    vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
};

// maps NVRHI binding locations to binding information
typedef ResourceBindingKey_HashMap<BindingLayoutVK> ResourceBindingMap;

class PipelineBindingLayout : public IBindingLayout
{
public:
    Device* parent;
    unsigned long refCount;

    BindingLayoutDesc desc;

    ResourceBindingMap bindingMap_VS;
    ResourceBindingMap bindingMap_HS;
    ResourceBindingMap bindingMap_DS;
    ResourceBindingMap bindingMap_GS;
    ResourceBindingMap bindingMap_PS;
    ResourceBindingMap bindingMap_CS;

    vk::DescriptorSetLayout descriptorSetLayout;

    // descriptor pool size information per binding set
    nvrhi::vector<vk::DescriptorPoolSize> descriptorPoolSizeInfo;

    PipelineBindingLayout(Device* parent, const BindingLayoutDesc& desc);
    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const BindingLayoutDesc& GetDesc() const override { return desc; }

    void collectBindings(const StageBindingLayoutDesc& bindingLayout, vk::ShaderStageFlagBits shaderStage, ResourceBindingMap& bindingMap);

    // generate the descriptor set layout
    vk::Result bake(VulkanContext& context);
};

// contains a vk::DescriptorSet
class ResourceBindingSet : public IBindingSet
{
public:
    Device* parent;
    unsigned long refCount;

    BindingSetDesc desc;
    BindingLayoutHandle layout;

    // xxxnsubtil: move pool to the context instead
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSet descriptorSet;

    ResourceBindingSet(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    virtual const BindingSetDesc& GetDesc() const override { return desc; }
    virtual IBindingLayout* GetLayout() const override { return layout; }
};

template <typename T>
using BindingVector = static_vector<T, MaxBindingLayouts>;

class GraphicsPipeline : public IGraphicsPipeline
{
public:
    Device* parent;
    unsigned long refCount;

    GraphicsPipelineDesc desc;
    FramebufferInfo framebufferInfo;
    InputLayout inputLayout;

    BindingVector<RefCountPtr<PipelineBindingLayout>> pipelineBindingLayouts;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;

    bool viewportStateDynamic;
    bool scissorStateDynamic;

    GraphicsPipeline(Device* parent)
        : parent(parent)
        , refCount(1)
        , inputLayout(parent)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    virtual const GraphicsPipelineDesc& GetDesc() const override { return desc; }
    virtual const FramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; }
};

class ComputePipeline : public IComputePipeline
{
public:
    Device* parent;
    unsigned long refCount;

    ComputePipelineDesc desc;

    BindingVector<RefCountPtr<PipelineBindingLayout>> pipelineBindingLayouts;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;

    ComputePipeline(Device* parent)
        : parent(parent)
        , refCount(1)
    { }

    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override;
    const ComputePipelineDesc& GetDesc() const override { return desc; }
};

} // namespace vulkan
} // namespace nvrhi
