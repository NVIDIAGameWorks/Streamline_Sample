
#include <donut/render/ToneMappingPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <sstream>
#include <assert.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/core/log.h>

using namespace donut::math;
#include <donut/shaders/tonemapping_cb.h>

using namespace donut::engine;
using namespace donut::render;

ToneMappingPass::ToneMappingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView,
    uint32_t histogramBins,
    bool isTextureArray,
    nvrhi::ITexture* exposureTextureOverride,
    nvrhi::ResourceStates::Enum exposureTextureStateOverride,
    nvrhi::ITexture* colorLUT)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
    , m_HistogramBins(histogramBins)
    , m_FramebufferFactory(framebufferFactory)
{
    assert(histogramBins <= 256);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    {
        std::vector<ShaderMacro> Macros;

        std::stringstream ss;
        ss << histogramBins;
        Macros.push_back(ShaderMacro("HISTOGRAM_BINS", ss.str()));
        Macros.push_back(ShaderMacro("SOURCE_ARRAY", isTextureArray ? "1" : "0"));

        m_HistogramComputeShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/histogram_cs.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_COMPUTE);
        m_ExposureComputeShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/exposure_cs.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_COMPUTE);
        m_PixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/tonemapping_ps.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_PIXEL);
    }

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(ToneMappingConstants);
    constantBufferDesc.debugName = "ToneMappingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_ToneMappingCB = device->createBuffer(constantBufferDesc);

    nvrhi::BufferDesc storageBufferDesc;
    storageBufferDesc.byteSize = sizeof(uint) * m_HistogramBins;
    storageBufferDesc.canHaveUAVs = true;
    storageBufferDesc.debugName = "HistogramBuffer";
    m_HistogramBuffer = device->createBuffer(storageBufferDesc);
    m_HistogramBufferState = nvrhi::ResourceStates::COMMON;

    if (exposureTextureOverride)
    {
        m_ExposureTexture = exposureTextureOverride;
        m_ExposureTextureState = exposureTextureStateOverride;
    }
    else
    {
        nvrhi::TextureDesc exposureTextureDesc;
        exposureTextureDesc.width = 1;
        exposureTextureDesc.height = 1;
        exposureTextureDesc.isRenderTarget = true;
        exposureTextureDesc.sampleCount = 1;
        exposureTextureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        exposureTextureDesc.keepInitialState = true;
        exposureTextureDesc.format = nvrhi::Format::R16_FLOAT;
        exposureTextureDesc.isUAV = true;
        exposureTextureDesc.debugName = "ExposureTexture";
        m_ExposureTexture = device->createTexture(exposureTextureDesc);
        m_ExposureTextureState = nvrhi::ResourceStates::COMMON;
    }

    m_ColorLUT = commonPasses->m_BlackTexture;

    if (colorLUT)
    {
        const nvrhi::TextureDesc& desc = colorLUT->GetDesc();
        m_ColorLUTSize = float(desc.height);

        if (desc.width != desc.height * desc.height)
        {
            log::error("Color LUT texture size must be: width = (n*n), height = (n)");
            m_ColorLUTSize = 0.f;
        }
        else
        {
            m_ColorLUT = colorLUT;
        }
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.CS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Buffer_UAV }
        };
        m_HistogramBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::ComputePipelineDesc computePipelineDesc;
        computePipelineDesc.CS = m_HistogramComputeShader;
        computePipelineDesc.bindingLayouts = { m_HistogramBindingLayout };
        m_HistogramPso = device->createComputePipeline(computePipelineDesc);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.CS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Buffer_SRV },
            { 0, nvrhi::ResourceType::Texture_UAV }
        };
        m_ExposureBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.CS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::Buffer_SRV(0, m_HistogramBuffer, nvrhi::Format::R32_UINT),
            nvrhi::BindingSetItem::Texture_UAV(0, m_ExposureTexture)
        };
        m_ExposureBindingSet = device->createBindingSet(bindingSetDesc, m_ExposureBindingLayout);

        nvrhi::ComputePipelineDesc computePipelineDesc;
        computePipelineDesc.CS = m_ExposureComputeShader;
        computePipelineDesc.bindingLayouts = { m_ExposureBindingLayout };
        m_ExposurePso = device->createComputePipeline(computePipelineDesc);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 1, nvrhi::ResourceType::Texture_SRV },
            { 2, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Sampler }
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout};

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

void ToneMappingPass::Render(
    nvrhi::ICommandList* commandList, 
    const ToneMappingParameters& params,
    const ICompositeView& compositeView,
    nvrhi::ITexture* sourceTexture)
{
    nvrhi::BindingSetHandle& bindingSet = m_RenderBindingSets[sourceTexture];
    if (!bindingSet)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
            nvrhi::BindingSetItem::Texture_SRV(1, m_ExposureTexture),
            nvrhi::BindingSetItem::Texture_SRV(2, m_ColorLUT),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler)
        };
        bindingSet = m_Device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);
    }

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { bindingSet };
        state.viewport = view->GetViewportState();

        bool enableColorLUT = params.enableColorLUT && m_ColorLUTSize > 0;

        ToneMappingConstants toneMappingConstants = {};
        toneMappingConstants.exposureScale = ::exp2f(params.exposureBias);
        toneMappingConstants.whitePointInvSquared = 1.f / powf(params.whitePoint, 2.f);
        toneMappingConstants.minAdaptedLuminance = params.minAdaptedLuminance;
        toneMappingConstants.maxAdaptedLuminance = params.maxAdaptedLuminance;
        toneMappingConstants.sourceSlice = view->GetSubresources().baseArraySlice;
        toneMappingConstants.colorLUTTextureSize = enableColorLUT ? float2(m_ColorLUTSize * m_ColorLUTSize, m_ColorLUTSize) : float2(0.f);
        toneMappingConstants.colorLUTTextureSizeInv = enableColorLUT ? 1.f / toneMappingConstants.colorLUTTextureSize : float2(0.f);
        commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }
}

void ToneMappingPass::SimpleRender(
    nvrhi::ICommandList* commandList, 
    const ToneMappingParameters& params, 
    const ICompositeView& compositeView, nvrhi
    ::ITexture* sourceTexture)
{
    commandList->beginMarker("ToneMapping");
    ResetHistogram(commandList);
    AddFrameToHistogram(commandList, compositeView, sourceTexture);
    ComputeExposure(commandList, params);
    Render(commandList, params, compositeView, sourceTexture);
    commandList->endMarker();
}

nvrhi::TextureHandle ToneMappingPass::GetExposureTexture()
{
    return m_ExposureTexture;
}

nvrhi::ResourceStates::Enum ToneMappingPass::GetExposureTextureState()
{
    return m_ExposureTextureState;
}

void ToneMappingPass::AdvanceFrame(float frameTime)
{
    m_FrameTime = frameTime;
}

void ToneMappingPass::ResetExposure(nvrhi::ICommandList* commandList, float initialExposure)
{
    commandList->clearTextureFloat(m_ExposureTexture, nvrhi::AllSubresources, nvrhi::Color(initialExposure));
}

void ToneMappingPass::ResetHistogram(nvrhi::ICommandList* commandList)
{
    commandList->clearBufferUInt(m_HistogramBuffer, 0);
}

static const float g_minLogLuminance = -10; // TODO: figure out how to set these properly
static const float g_maxLogLuminamce = 4;

void ToneMappingPass::AddFrameToHistogram(nvrhi::ICommandList* commandList, const ICompositeView& compositeView, nvrhi::ITexture* sourceTexture)
{
    nvrhi::BindingSetHandle& bindingSet = m_HistogramBindingSets[sourceTexture];
    if (!bindingSet)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.CS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
            nvrhi::BindingSetItem::Buffer_UAV(0, m_HistogramBuffer, nvrhi::Format::R32_UINT)
        };

        bindingSet = m_Device->createBindingSet(bindingSetDesc, m_HistogramBindingLayout);
    }

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::ViewportState viewportState = view->GetViewportState();

        for (uint viewportIndex = 0; viewportIndex < viewportState.scissorRects.size(); viewportIndex++)
        {
            ToneMappingConstants toneMappingConstants = {};
            toneMappingConstants.logLuminanceScale = 1.0f / (g_maxLogLuminamce - g_minLogLuminance);
            toneMappingConstants.logLuminanceBias = -g_minLogLuminance * toneMappingConstants.logLuminanceScale;

            nvrhi::Rect& scissor = viewportState.scissorRects[viewportIndex];
            toneMappingConstants.viewOrigin = uint2(scissor.minX, scissor.minY);
            toneMappingConstants.viewSize = uint2(scissor.maxX - scissor.minX, scissor.maxY - scissor.minY);
            toneMappingConstants.sourceSlice = view->GetSubresources().baseArraySlice;

            commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

            nvrhi::ComputeState state;
            state.pipeline = m_HistogramPso;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);

            uint2 numGroups = (toneMappingConstants.viewSize + uint2(15)) / uint2(16);
            commandList->dispatch(numGroups.x, numGroups.y, 1);
        }
    }
}

void ToneMappingPass::ComputeExposure(nvrhi::ICommandList* commandList, const ToneMappingParameters& params)
{
    ToneMappingConstants toneMappingConstants = {};
    toneMappingConstants.logLuminanceScale = g_maxLogLuminamce - g_minLogLuminance;
    toneMappingConstants.logLuminanceBias = g_minLogLuminance;
    toneMappingConstants.histogramLowPercentile = std::min(0.99f, std::max(0.f, params.histogramLowPercentile));
    toneMappingConstants.histogramHighPercentile = std::min(1.f, std::max(toneMappingConstants.histogramLowPercentile, params.histogramHighPercentile));
    toneMappingConstants.eyeAdaptationSpeedUp = params.eyeAdaptationSpeedUp;
    toneMappingConstants.eyeAdaptationSpeedDown = params.eyeAdaptationSpeedDown;
    toneMappingConstants.minAdaptedLuminance = params.minAdaptedLuminance;
    toneMappingConstants.maxAdaptedLuminance = params.maxAdaptedLuminance;
    toneMappingConstants.frameTime = m_FrameTime;

    commandList->writeBuffer(m_ToneMappingCB, &toneMappingConstants, sizeof(toneMappingConstants));

    nvrhi::ComputeState state;
    state.pipeline = m_ExposurePso;
    state.bindings = { m_ExposureBindingSet };
    commandList->setComputeState(state);

    commandList->dispatch(1);
}

void ToneMappingPass::BeginTrackingState(nvrhi::ICommandList* commandList)
{
    commandList->beginTrackingTextureState(m_ExposureTexture, nvrhi::AllSubresources, m_ExposureTextureState);
    commandList->beginTrackingBufferState(m_HistogramBuffer, m_HistogramBufferState);
}

void ToneMappingPass::SaveCurrentState(nvrhi::ICommandList* commandList)
{
    m_ExposureTextureState = commandList->getTextureSubresourceState(m_ExposureTexture, 0, 0);
    m_HistogramBufferState = commandList->getBufferState(m_HistogramBuffer);
}
