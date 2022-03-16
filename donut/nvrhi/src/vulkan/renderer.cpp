#include <nvrhi/vulkan.h>

#if WIN32
#include <windows.h> // just for temporary use of a whiny OutputDebugStringA
#endif // WIN32

namespace nvrhi 
{
namespace vulkan
{

vk::Viewport VKViewportWithDXCoords(const Viewport& v)
{
    // requires VK_KHR_maintenance1 which allows negative-height to indicate an inverted co-ord space to match DX
    return vk::Viewport(v.minX, v.maxY, v.maxX - v.minX, -(v.maxY - v.minY), v.minZ, v.maxZ);
}

// maps Vulkan extension strings into the corresponding boolean flags in Device
const nvrhi::map<const std::string, bool *> Device::getExtensionStringMap()
{
    const nvrhi::map<const std::string, bool *> extensionStringMap = {
        { "VK_KHR_maintenance1", &context.extensions.KHR_maintenance1 },
        { "VK_EXT_debug_report", &context.extensions.EXT_debug_report },
        { "VK_EXT_debug_marker", &context.extensions.EXT_debug_marker },
    };

    return extensionStringMap;
}

Device::Device(IMessageCallback* errorCB, vk::Instance instance,
                                                 vk::PhysicalDevice physicalDevice,
                                                 vk::Device device,
                                                 vk::Queue graphicsQueue, int graphicsQueueIndex, vk::CommandPool graphicsCommandPool,
                                                 vk::Queue transferQueue, int transferQueueIndex, vk::CommandPool transferCommandPool,
                                                 vk::Queue computeQueue,  int computeQueueIndex,  vk::CommandPool computeCommandPool,
                                                 vk::AllocationCallbacks *allocationCallbacks,
                                                 const char **instanceExtensions, size_t numInstanceExtensions,
                                                 const char **layers, size_t numLayers,
                                                 const char **deviceExtensions, size_t numDeviceExtensions)
    : context(instance, physicalDevice, device, allocationCallbacks)
    , syncObjectPool(context, this)
    , allocator(context)
    , queues({
        Queue(context, this, syncObjectPool, QueueID::Graphics, graphicsQueue, graphicsQueueIndex, graphicsCommandPool),
        Queue(context, this, syncObjectPool, QueueID::Transfer, transferQueue, transferQueueIndex, transferCommandPool),
        Queue(context, this, syncObjectPool, QueueID::Compute,  computeQueue,  computeQueueIndex,  computeCommandPool),
    })
    , internalCmd(nullptr)
    , timerQueryObjectPool(context)
    , m_pMessageCallback(errorCB)
{
    const auto extensionStringMap = getExtensionStringMap();

    // parse the extension/layer lists and figure out which extensions are enabled
    for(size_t i = 0; i < numInstanceExtensions; i++)
    {
        auto ext = extensionStringMap.find(instanceExtensions[i]);
        if (ext != extensionStringMap.end())
        {
            *(ext->second) = true;
        }
    }

    for(size_t i = 0; i < numLayers; i++)
    {
        auto ext = extensionStringMap.find(layers[i]);
        if (ext != extensionStringMap.end())
        {
            *(ext->second) = true;
        }
    }

    for(size_t i = 0; i < numDeviceExtensions; i++)
    {
        auto ext = extensionStringMap.find(deviceExtensions[i]);
        if (ext != extensionStringMap.end())
        {
            *(ext->second) = true;
        }
    }

    context.physicalDevice.getProperties(&context.physicalDeviceProperties);
}

Device::~Device()
{
    // cleanupCommandPools();
}

Object Device::getNativeObject(ObjectType objectType)
{
    switch (objectType)
    {
    case ObjectTypes::VK_Device:
        return Object(context.device);
    case ObjectTypes::VK_PhysicalDevice:
        return Object(context.physicalDevice);
    case ObjectTypes::VK_Instance:
        return Object(context.instance);
    case ObjectTypes::VK_CommandBuffer:
        return Object(internalCmd->cmdBuf);
    case ObjectTypes::Nvrhi_VK_Device:
        return Object(this);
    default:
        return nullptr;
    }
}

void Device::open()
{
}

void Device::close()
{
    clearState();
}

void Device::clearState()
{
    m_CurrentDispatchIndirectBuffer = nullptr;
    m_CurrentDrawIndirectBuffer = nullptr;
    
    // TODO: add real context clearing code here 
}


void Device::clearBufferUInt(IBuffer* b, uint32_t clearValue)
{
    Buffer* vkbuf = static_cast<Buffer*>(b);

    TrackedCommandBuffer *cmd = getAnyCmdBuf();
    assert(cmd);

    cmd->unbindFB(); // end renderpass - fillBuffer can only be done outside of a renderpass

    cmd->cmdBuf.fillBuffer(vkbuf->buffer, 0, vkbuf->desc.byteSize, clearValue);
    cmd->referencedResources.push_back(b);

    cmd->markWrite(vkbuf);
}

Semaphore *Device::createSemaphore(vk::PipelineStageFlags stageFlags)
{
    return syncObjectPool.getSemaphore(stageFlags);
}

void Device::releaseSemaphore(Semaphore *semaphore)
{
    syncObjectPool.releaseSemaphore(semaphore);
}

void Device::markSemaphoreInFlight(Semaphore *semaphore)
{
    semaphore->markInFlight();
}

void Device::setTextureReadSemaphore(ITexture* _texture, Semaphore *semaphore)
{
    Texture* texture = static_cast<Texture*>(_texture);
    texture->setReadSemaphore(syncObjectPool, semaphore);
}

void Device::setTextureWriteSemaphore(ITexture* _texture, Semaphore *semaphore)
{
    Texture* texture = static_cast<Texture*>(_texture);
    texture->setWriteSemaphore(syncObjectPool, semaphore);
}

SemaphoreHandle Device::getTextureReadSemaphore(ITexture* texture)
{
    return static_cast<Texture*>(texture)->readSemaphore;
}

SemaphoreHandle Device::getTextureWriteSemaphore(ITexture* texture)
{
    return static_cast<Texture*>(texture)->writeSemaphore;
}

vk::PipelineStageFlags Device::getVulkanSemaphoreStageFlags(Semaphore *semaphore)
{
    return semaphore->getStageFlags();
}

vk::Semaphore Device::getVulkanSemaphore(Semaphore *semaphore)
{
    return semaphore->getVkSemaphore();
}

static vk::PipelineShaderStageCreateInfo shaderStageCreateInfo(vk::ShaderStageFlagBits stage, Shader *shader)
{
    return vk::PipelineShaderStageCreateInfo()
            .setStage(stage)
            .setModule(shader->shaderModule)
            .setPName(shader->entryName.c_str());
}

static vk::PrimitiveTopology convertPrimitiveTopology(PrimitiveType::Enum topology)
{
    switch(topology)
    {
        case PrimitiveType::POINT_LIST:
            return vk::PrimitiveTopology::ePointList;

        case PrimitiveType::LINE_LIST:
            return vk::PrimitiveTopology::eLineList;

        case PrimitiveType::TRIANGLE_LIST:
            return vk::PrimitiveTopology::eTriangleList;

        case PrimitiveType::TRIANGLE_STRIP:
            return vk::PrimitiveTopology::eTriangleStrip;

        case PrimitiveType::PATCH_1_CONTROL_POINT:
            return vk::PrimitiveTopology::ePatchList;

        case PrimitiveType::PATCH_3_CONTROL_POINT:
            return vk::PrimitiveTopology::ePatchList;

        case PrimitiveType::PATCH_4_CONTROL_POINT:
            return vk::PrimitiveTopology::ePatchList;

        default:
            assert(0);
            return vk::PrimitiveTopology::eTriangleList;
    }
}

static vk::PolygonMode convertFillMode(RasterState::FillMode mode)
{
    switch(mode)
    {
        case RasterState::FillMode::FILL_SOLID:
            return vk::PolygonMode::eFill;

        case RasterState::FillMode::FILL_LINE:
            return vk::PolygonMode::eLine;

        default:
            assert(0);
            return vk::PolygonMode::eFill;
    }
}

static vk::CullModeFlagBits convertCullMode(RasterState::CullMode mode)
{
    switch(mode)
    {
        case RasterState::CullMode::CULL_BACK:
            return vk::CullModeFlagBits::eBack;

        case RasterState::CullMode::CULL_FRONT:
            return vk::CullModeFlagBits::eFront;

        case RasterState::CullMode::CULL_NONE:
            return vk::CullModeFlagBits::eNone;

        default:
            assert(0);
            return vk::CullModeFlagBits::eNone;
    }
}

static vk::CompareOp convertCompareOp(DepthStencilState::ComparisonFunc op)
{
    switch(op)
    {
        case DepthStencilState::COMPARISON_NEVER:
            return vk::CompareOp::eNever;

        case DepthStencilState::COMPARISON_LESS:
            return vk::CompareOp::eLess;

        case DepthStencilState::COMPARISON_EQUAL:
            return vk::CompareOp::eEqual;

        case DepthStencilState::COMPARISON_LESS_EQUAL:
            return vk::CompareOp::eLessOrEqual;

        case DepthStencilState::COMPARISON_GREATER:
            return vk::CompareOp::eGreater;

        case DepthStencilState::COMPARISON_NOT_EQUAL:
            return vk::CompareOp::eNotEqual;

        case DepthStencilState::COMPARISON_GREATER_EQUAL:
            return vk::CompareOp::eGreaterOrEqual;

        case DepthStencilState::COMPARISON_ALWAYS:
            return vk::CompareOp::eAlways;

        default:
            assert(0);
            return vk::CompareOp::eAlways;
    }
}

static vk::StencilOp convertStencilOp(DepthStencilState::StencilOp op)
{
    switch(op)
    {
        case DepthStencilState::STENCIL_OP_KEEP:
            return vk::StencilOp::eKeep;

        case DepthStencilState::STENCIL_OP_ZERO:
            return vk::StencilOp::eZero;

        case DepthStencilState::STENCIL_OP_REPLACE:
            return vk::StencilOp::eReplace;

        case DepthStencilState::STENCIL_OP_INCR_SAT:
            return vk::StencilOp::eIncrementAndClamp;

        case DepthStencilState::STENCIL_OP_DECR_SAT:
            return vk::StencilOp::eDecrementAndClamp;

        case DepthStencilState::STENCIL_OP_INVERT:
            return vk::StencilOp::eInvert;

        case DepthStencilState::STENCIL_OP_INCR:
            return vk::StencilOp::eIncrementAndWrap;

        case DepthStencilState::STENCIL_OP_DECR:
            return vk::StencilOp::eDecrementAndWrap;

        default:
            assert(0);
            return vk::StencilOp::eKeep;
    }
}

static vk::StencilOpState convertStencilState(const DepthStencilState& depthStencilState, const DepthStencilState::StencilOpDesc& desc)
{
    return vk::StencilOpState()
            .setFailOp(convertStencilOp(desc.stencilFailOp))
            .setPassOp(convertStencilOp(desc.stencilPassOp))
            .setDepthFailOp(convertStencilOp(desc.stencilDepthFailOp))
            .setCompareOp(convertCompareOp(desc.stencilFunc))
            .setCompareMask(depthStencilState.stencilReadMask)
            .setWriteMask(depthStencilState.stencilWriteMask)
            .setReference(depthStencilState.stencilRefValue);
}

static vk::BlendFactor convertBlendValue(BlendState::BlendValue value)
{
    switch(value)
    {
        case BlendState::BLEND_ZERO:
            return vk::BlendFactor::eZero;

        case BlendState::BLEND_ONE:
            return vk::BlendFactor::eOne;

        case BlendState::BLEND_SRC_COLOR:
            return vk::BlendFactor::eSrcColor;

        case BlendState::BLEND_INV_SRC_COLOR:
            return vk::BlendFactor::eOneMinusSrcColor;

        case BlendState::BLEND_SRC_ALPHA:
            return vk::BlendFactor::eSrcAlpha;

        case BlendState::BLEND_INV_SRC_ALPHA:
            return vk::BlendFactor::eOneMinusSrcAlpha;

        case BlendState::BLEND_DEST_ALPHA:
            return vk::BlendFactor::eDstAlpha;

        case BlendState::BLEND_INV_DEST_ALPHA:
            return vk::BlendFactor::eOneMinusDstAlpha;

        case BlendState::BLEND_DEST_COLOR:
            return vk::BlendFactor::eDstColor;

        case BlendState::BLEND_INV_DEST_COLOR:
            return vk::BlendFactor::eOneMinusDstColor;

        case BlendState::BLEND_SRC_ALPHA_SAT:
            return vk::BlendFactor::eSrcAlphaSaturate;

        case BlendState::BLEND_BLEND_FACTOR:
            return vk::BlendFactor::eConstantColor;

        case BlendState::BLEND_INV_BLEND_FACTOR:
            return vk::BlendFactor::eOneMinusConstantColor;

        case BlendState::BLEND_SRC1_COLOR:
            return vk::BlendFactor::eSrc1Color;

        case BlendState::BLEND_INV_SRC1_COLOR:
            return vk::BlendFactor::eOneMinusSrc1Color;

        case BlendState::BLEND_SRC1_ALPHA:
            return vk::BlendFactor::eSrc1Alpha;

        case BlendState::BLEND_INV_SRC1_ALPHA:
            return vk::BlendFactor::eOneMinusSrc1Alpha;

        default:
            assert(0);
            return vk::BlendFactor::eZero;
    }
}

static vk::BlendOp convertBlendOp(BlendState::BlendOp op)
{
    switch(op)
    {
        case BlendState::BLEND_OP_ADD:
            return vk::BlendOp::eAdd;

        case BlendState::BLEND_OP_SUBTRACT:
            return vk::BlendOp::eSubtract;

        case BlendState::BLEND_OP_REV_SUBTRACT:
            return vk::BlendOp::eReverseSubtract;

        case BlendState::BLEND_OP_MIN:
            return vk::BlendOp::eMin;

        case BlendState::BLEND_OP_MAX:
            return vk::BlendOp::eMax;

        default:
            assert(0);
            return vk::BlendOp::eAdd;
    }
}

static vk::ColorComponentFlags convertColorMask(BlendState::ColorMask mask)
{
    vk::ColorComponentFlags ret;

    if (mask & BlendState::ColorMask::COLOR_MASK_RED)
        ret |= vk::ColorComponentFlagBits::eR;

    if (mask & BlendState::ColorMask::COLOR_MASK_GREEN)
        ret |= vk::ColorComponentFlagBits::eG;

    if (mask & BlendState::ColorMask::COLOR_MASK_BLUE)
        ret |= vk::ColorComponentFlagBits::eB;

    if (mask & BlendState::ColorMask::COLOR_MASK_ALPHA)
        ret |= vk::ColorComponentFlagBits::eA;

    return ret;
}

static vk::PipelineColorBlendAttachmentState convertBlendState(const BlendState& state, uint32_t i)
{
    return vk::PipelineColorBlendAttachmentState()
            .setBlendEnable(state.blendEnable[i])
            .setSrcColorBlendFactor(convertBlendValue(state.srcBlend[i]))
            .setDstColorBlendFactor(convertBlendValue(state.destBlend[i]))
            .setColorBlendOp(convertBlendOp(state.blendOp[i]))
            .setSrcAlphaBlendFactor(convertBlendValue(state.srcBlendAlpha[i]))
            .setDstAlphaBlendFactor(convertBlendValue(state.destBlendAlpha[i]))
            .setAlphaBlendOp(convertBlendOp(state.blendOpAlpha[i]))
            .setColorWriteMask(convertColorMask(state.colorWriteEnable[i]));
}

template <typename T>
using attachment_vector = nvrhi::static_vector<T, FramebufferDesc::MAX_RENDER_TARGETS>;

nvrhi::GraphicsAPI Device::getGraphicsAPI()
{
    return GraphicsAPI::VULKAN;
}

FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc)
{
    Framebuffer *fb = new (nvrhi::HeapAlloc) Framebuffer(this);
    fb->desc = desc;
    fb->framebufferInfo = FramebufferInfo(desc);

    attachment_vector<vk::AttachmentDescription> attachmentDescs(desc.colorAttachments.size());
    attachment_vector<vk::AttachmentReference> colorAttachmentRefs(desc.colorAttachments.size());
    vk::AttachmentReference depthAttachmentRef;

    fb->attachmentViews.resize(desc.colorAttachments.size());

    // set up output color attachments
    assert(desc.colorAttachments.size() || desc.depthAttachment.valid());
    if (desc.colorAttachments.size())
    {
        const auto& a = desc.colorAttachments[0];
        Texture* texture = static_cast<Texture*>(a.texture);
        fb->renderAreaW = texture->desc.width >> a.subresources.baseMipLevel;
        fb->renderAreaH = texture->desc.height >> a.subresources.baseMipLevel;
    } else {
        Texture* texture = static_cast<Texture*>(desc.depthAttachment.texture);
        fb->renderAreaW = texture->desc.width >> desc.depthAttachment.subresources.baseMipLevel;
        fb->renderAreaH = texture->desc.height >> desc.depthAttachment.subresources.baseMipLevel;
    }

    for(uint32_t i = 0; i < desc.colorAttachments.size(); i++)
    {
        const auto& rt = desc.colorAttachments[i];
        Texture* t = static_cast<Texture*>(rt.texture);

        assert(fb->renderAreaW == t->desc.width >> rt.subresources.baseMipLevel);
        assert(fb->renderAreaH == t->desc.height >> rt.subresources.baseMipLevel);

        vk::Format attachmentFormat = (rt.format == Format::UNKNOWN ? t->imageInfo.format : convertFormat(rt.format));

        attachmentDescs[i] = vk::AttachmentDescription()
                                    .setFormat(attachmentFormat)
                                    .setSamples(t->imageInfo.samples)
                                    .setLoadOp(vk::AttachmentLoadOp::eLoad)
                                    .setStoreOp(vk::AttachmentStoreOp::eStore)
                                    .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                    .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

        colorAttachmentRefs[i] = vk::AttachmentReference()
                                    .setAttachment(i)
                                    .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        const auto& view = t->getSubresourceView(rt.subresources.resolve(t->desc, true));
        fb->attachmentViews[i] = view.view;
    }

    // add depth/stencil attachment if present
    if (desc.depthAttachment.valid())
    {
        const auto& att = desc.depthAttachment;

        Texture* texture = static_cast<Texture*>(att.texture);

        assert(fb->renderAreaW == texture->desc.width >> att.subresources.baseMipLevel);
        assert(fb->renderAreaH == texture->desc.height >> att.subresources.baseMipLevel);

        vk::ImageLayout depthLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        if (desc.depthAttachment.isReadOnly)
        {
            depthLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        }

        attachmentDescs.push_back(vk::AttachmentDescription()
                                    .setFormat(texture->imageInfo.format)
                                    .setSamples(texture->imageInfo.samples)
                                    .setLoadOp(vk::AttachmentLoadOp::eLoad)
                                    .setStoreOp(vk::AttachmentStoreOp::eStore)
                                    .setInitialLayout(depthLayout)
                                    .setFinalLayout(depthLayout));

        depthAttachmentRef = vk::AttachmentReference()
                                .setAttachment(uint32_t(attachmentDescs.size()) - 1)
                                .setLayout(depthLayout);

        const auto& view = texture->getSubresourceView(att.subresources.resolve(texture->desc, true));
        fb->attachmentViews.push_back(view.view);
    }

    auto subpass = vk::SubpassDescription()
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(uint32_t(desc.colorAttachments.size()))
        .setPColorAttachments(colorAttachmentRefs.data())
        .setPDepthStencilAttachment(desc.depthAttachment.valid() ? &depthAttachmentRef : nullptr);

    auto renderPassInfo = vk::RenderPassCreateInfo()
                .setAttachmentCount(uint32_t(attachmentDescs.size()))
                .setPAttachments(attachmentDescs.data())
                .setSubpassCount(1)
                .setPSubpasses(&subpass);

    vk::Result res;
    res = context.device.createRenderPass(&renderPassInfo,
                                          context.allocationCallbacks,
                                          &fb->renderPass);
    CHECK_VK_FAIL(res);
    nameVKObject(fb->renderPass);

    // set up the framebuffer object
    auto framebufferInfo = vk::FramebufferCreateInfo()
                            .setRenderPass(fb->renderPass)
                            .setAttachmentCount(uint32_t(fb->attachmentViews.size()))
                            .setPAttachments(fb->attachmentViews.data())
                            .setWidth(fb->renderAreaW)
                            .setHeight(fb->renderAreaH)
                            .setLayers(1);

    res = context.device.createFramebuffer(&framebufferInfo, context.allocationCallbacks,
                                           &fb->framebuffer);
    CHECK_VK_FAIL(res);
    nameVKObject(fb->framebuffer);

    return FramebufferHandle::Create(fb);
}

unsigned long Framebuffer::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyFramebuffer(this); 
    return result; 
}

void Device::destroyFramebuffer(IFramebuffer* _fb)
{
    Framebuffer* fb = static_cast<Framebuffer*>(_fb);

    if (fb->framebuffer)
    {
        context.device.destroyFramebuffer(fb->framebuffer);
        fb->framebuffer = nullptr;
    }

    if (fb->renderPass)
    {
        context.device.destroyRenderPass(fb->renderPass);
        fb->renderPass = nullptr;
    }

    heapDelete(fb);
}

GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* _fb)
{
    if (desc.renderState.singlePassStereo.enabled)
    {
        assert(!"Single-pass stereo is not supported by the Vulkan backend");
        return nullptr;
    }

    vk::Result res;

    Framebuffer* fb = static_cast<Framebuffer*>(_fb);

    if (!context.pipelineCache)
    {
        auto pipelineInfo = vk::PipelineCacheCreateInfo();
        res = context.device.createPipelineCache(&pipelineInfo,
                                                 context.allocationCallbacks,
                                                 &context.pipelineCache);
        CHECK_VK_FAIL(res);
        nameVKObject(context.pipelineCache);
    }

	InputLayout inputLayout(this);
	if (desc.inputLayout)
	{
		inputLayout = *static_cast<InputLayout*>(desc.inputLayout.Get());
	}

    GraphicsPipeline *pso = new (nvrhi::HeapAlloc) GraphicsPipeline(this);
    pso->desc = desc;
    pso->framebufferInfo = fb->framebufferInfo;
	pso->inputLayout = inputLayout;

    for (const BindingLayoutHandle& _layout : desc.bindingLayouts)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout.Get());
        pso->pipelineBindingLayouts.push_back(layout);
    }

    // set up shader stages
    nvrhi::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

    if (desc.VS)
    {
        shaderStages.push_back(shaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, static_cast<Shader*>(desc.VS.Get())));
    }

    if (desc.HS)
    {
        shaderStages.push_back(shaderStageCreateInfo(vk::ShaderStageFlagBits::eTessellationControl, static_cast<Shader*>(desc.HS.Get())));
    }

    if (desc.DS)
    {
        shaderStages.push_back(shaderStageCreateInfo(vk::ShaderStageFlagBits::eTessellationEvaluation, static_cast<Shader*>(desc.DS.Get())));
    }

    if (desc.GS)
    {
        shaderStages.push_back(shaderStageCreateInfo(vk::ShaderStageFlagBits::eGeometry, static_cast<Shader*>(desc.GS.Get())));
    }

    if (desc.PS)
    {
        shaderStages.push_back(shaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, static_cast<Shader*>(desc.PS.Get())));
    }

    // set up vertex input state
    auto vertexInput = vk::PipelineVertexInputStateCreateInfo()
                        .setVertexBindingDescriptionCount(uint32_t(inputLayout.bindingDesc.size()))
                        .setPVertexBindingDescriptions(inputLayout.bindingDesc.data())
                        .setVertexAttributeDescriptionCount(uint32_t(inputLayout.attributeDesc.size()))
                        .setPVertexAttributeDescriptions(inputLayout.attributeDesc.data());

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo()
                            .setTopology(convertPrimitiveTopology(desc.primType));

    // fixed function state
    const auto& renderState = desc.renderState;
    const auto& rasterState = desc.renderState.rasterState;
    const auto& depthStencilState = desc.renderState.depthStencilState;
    const auto& blendState = desc.renderState.blendState;

    nvrhi::static_vector<vk::Viewport, ViewportState::MAX_VIEWPORTS> viewports;
    nvrhi::static_vector<vk::Rect2D, ViewportState::MAX_VIEWPORTS> scissors;

    for(const auto& vp : renderState.viewportState.viewports)
    {
        viewports.push_back(VKViewportWithDXCoords(vp));
    }

    for(const auto& sc : renderState.viewportState.scissorRects)
    {
        scissors.push_back(vk::Rect2D(vk::Offset2D(sc.minX, sc.minY),
                                      vk::Extent2D(std::abs(sc.maxX - sc.minX), std::abs(sc.maxY - sc.minY))));
    }

    auto viewportState = vk::PipelineViewportStateCreateInfo()
                            .setViewportCount(uint32_t(viewports.size()))
                            .setPViewports(viewports.data())
                            .setScissorCount(uint32_t(scissors.size()))
                            .setPScissors(scissors.data());

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo()
                        // .setDepthClampEnable(??)
                        // .setRasterizerDiscardEnable(??)
                        .setPolygonMode(convertFillMode(rasterState.fillMode))
                        .setCullMode(convertCullMode(rasterState.cullMode))
                        .setFrontFace(rasterState.frontCounterClockwise ?
                                        vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise)
                        .setDepthBiasEnable(rasterState.depthBias ? true : false)
                        .setDepthBiasConstantFactor(float(rasterState.depthBias))
                        .setDepthBiasClamp(rasterState.depthBiasClamp)
                        .setDepthBiasSlopeFactor(rasterState.slopeScaledDepthBias)
                        .setLineWidth(1.0f);

    // xxxnsubtil: is this correct?
    auto multisample = vk::PipelineMultisampleStateCreateInfo()
                        // xxxnsubtil: is this correct?
                        // .setRasterizationSamples(rasterState.forcedSampleCount)
                        .setAlphaToCoverageEnable(blendState.alphaToCoverage)
                        ;

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo()
                            .setDepthTestEnable(depthStencilState.depthEnable)
                            .setDepthWriteEnable(depthStencilState.depthWriteMask == DepthStencilState::DEPTH_WRITE_MASK_ALL)
                            .setDepthCompareOp(convertCompareOp(depthStencilState.depthFunc))
                            .setStencilTestEnable(depthStencilState.stencilEnable)
                            .setFront(convertStencilState(depthStencilState, depthStencilState.frontFace))
                            .setBack(convertStencilState(depthStencilState, depthStencilState.backFace));

    BindingVector<vk::DescriptorSetLayout> descriptorSetLayouts;
    for (const BindingLayoutHandle& _layout : desc.bindingLayouts)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout.Get());
        descriptorSetLayouts.push_back(layout->descriptorSetLayout);
    }

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
                                .setSetLayoutCount(uint32_t(descriptorSetLayouts.size()))
                                .setPSetLayouts(descriptorSetLayouts.data())
                                .setPushConstantRangeCount(0);

    res = context.device.createPipelineLayout(&pipelineLayoutInfo,
                                              context.allocationCallbacks,
                                              &pso->pipelineLayout);
    CHECK_VK_FAIL(res);
    nameVKObject(pso->pipelineLayout);

    attachment_vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments(fb->desc.colorAttachments.size());

    for(uint32_t i = 0; i < uint32_t(fb->desc.colorAttachments.size()); i++)
    {
        colorBlendAttachments[i] = convertBlendState(blendState, i);
    }

    auto colorBlend = vk::PipelineColorBlendStateCreateInfo()
                        .setAttachmentCount(uint32_t(colorBlendAttachments.size()))
                        .setPAttachments(colorBlendAttachments.data())
                        .setBlendConstants({ blendState.blendFactor.r,
                                             blendState.blendFactor.g,
                                             blendState.blendFactor.b,
                                             blendState.blendFactor.a });

    nvrhi::static_vector<vk::DynamicState, 2> dynamicStates;

    if (viewports.size() == 0)
    {
        dynamicStates.push_back(vk::DynamicState::eViewport);
        viewportState.setViewportCount(1);
        viewportState.setPViewports(nullptr);
        pso->viewportStateDynamic = true;
    }

    if (scissors.size() == 0)
    {
        dynamicStates.push_back(vk::DynamicState::eScissor);
        viewportState.setScissorCount(1);
        viewportState.setPScissors(nullptr);
        pso->scissorStateDynamic = true;
    }

    auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo()
                                .setDynamicStateCount(uint32_t(dynamicStates.size()))
                                .setPDynamicStates(dynamicStates.data());

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo()
                            .setStageCount(uint32_t(shaderStages.size()))
                            .setPStages(shaderStages.data())
                            .setPVertexInputState(&vertexInput)
                            .setPInputAssemblyState(&inputAssembly)
                            .setPViewportState(&viewportState)
                            .setPRasterizationState(&rasterizer)
                            .setPMultisampleState(&multisample)
                            .setPDepthStencilState(&depthStencil)
                            .setPColorBlendState(&colorBlend)
                            .setPDynamicState(dynamicStateInfo.dynamicStateCount ? &dynamicStateInfo : nullptr)
                            .setLayout(pso->pipelineLayout)
                            .setRenderPass(fb->renderPass)
                            .setSubpass(0)
                            .setBasePipelineHandle(nullptr)
                            .setBasePipelineIndex(-1)
                            .setPTessellationState(nullptr);

    auto tessellationState = vk::PipelineTessellationStateCreateInfo();

    if (desc.primType == PrimitiveType::Enum::PATCH_1_CONTROL_POINT)
    {
        tessellationState.setPatchControlPoints(1);
        pipelineInfo.setPTessellationState(&tessellationState);
    }
    else if (desc.primType == PrimitiveType::Enum::PATCH_3_CONTROL_POINT)
    {
        tessellationState.setPatchControlPoints(3);
        pipelineInfo.setPTessellationState(&tessellationState);
    }
    else if (desc.primType == PrimitiveType::Enum::PATCH_4_CONTROL_POINT)
    {
        tessellationState.setPatchControlPoints(4);
        pipelineInfo.setPTessellationState(&tessellationState);
    }

    res = context.device.createGraphicsPipelines(context.pipelineCache,
                                                 1, &pipelineInfo,
                                                 context.allocationCallbacks,
                                                 &pso->pipeline);
    ASSERT_VK_OK(res); // for debugging
    CHECK_VK_FAIL(res);
    nameVKObject(pso->pipeline);

    return GraphicsPipelineHandle::Create(pso);
}

unsigned long GraphicsPipeline::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyGraphicsPipeline(this); 
    return result; 
}

void Device::destroyGraphicsPipeline(IGraphicsPipeline* _pso)
{
    GraphicsPipeline* pso = static_cast<GraphicsPipeline*>(_pso);

    if (pso->pipeline)
    {
        context.device.destroyPipeline(pso->pipeline, context.allocationCallbacks);
        pso->pipeline = nullptr;
    }

    if (pso->pipelineLayout)
    {
        context.device.destroyPipelineLayout(pso->pipelineLayout, context.allocationCallbacks);
        pso->pipelineLayout = nullptr;
    }

    heapDelete(pso);
}

uint32_t Device::getNumberOfAFRGroups()
{
    return 1;
}

uint32_t Device::getAFRGroupOfCurrentFrame(uint32_t numAFRGroups)
{
    (void)numAFRGroups;
    return 0;
}

nvrhi::CommandListHandle Device::createCommandList(const CommandListParameters& params)
{
    if (!params.enableImmediateExecution)
    {
        assert(0);
        return nullptr;
    }

    // hack to enforce internalCmd existing before use by DLSS init
    getAnyCmdBuf();

    // create a new RefCountPtr and add a reference to this
    return CommandListHandle(this);
}

void Device::executeCommandList(ICommandList* commandList)
{
    (void)commandList;
    flushCommandList();
}

void Device::waitForIdle()
{
    flushCommandList();
    context.device.waitIdle();
}

} // namespace vulkan
} // namespace nvrhi
