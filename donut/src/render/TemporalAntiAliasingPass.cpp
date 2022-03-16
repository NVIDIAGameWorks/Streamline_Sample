
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/taa_cb.h>

#include <assert.h>

using namespace donut::engine;
using namespace donut::render;

TemporalAntiAliasingPass::TemporalAntiAliasingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<CommonRenderPasses> commonPasses,
    const ICompositeView& compositeView,
    nvrhi::ITexture* sourceDepth,
    nvrhi::ITexture* motionVectors,
    nvrhi::ITexture* unresolvedColor,
    nvrhi::ITexture* resolvedColor,
    nvrhi::ITexture* feedback1,
    nvrhi::ITexture* feedback2,
    uint32_t motionVectorStencilMask,
    bool useCatmullRomFilter)
    : m_CommonPasses(commonPasses)
    , m_FrameIndex(0)
    , m_StencilMask(motionVectorStencilMask)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);

    const nvrhi::TextureDesc& unresolvedColorDesc = unresolvedColor->GetDesc();
    const nvrhi::TextureDesc& resolvedColorDesc = resolvedColor->GetDesc();
    const nvrhi::TextureDesc& feedback1Desc = feedback1->GetDesc();
    const nvrhi::TextureDesc& feedback2Desc = feedback2->GetDesc();

    assert(feedback1Desc.width == feedback2Desc.width);
    assert(feedback1Desc.height == feedback2Desc.height);
    assert(feedback1Desc.format == feedback2Desc.format);
    assert(feedback1Desc.isUAV);
    assert(feedback2Desc.isUAV);
    assert(resolvedColorDesc.isUAV);

    bool useStencil = false;
    nvrhi::Format stencilFormat = nvrhi::Format::UNKNOWN;
    if (motionVectorStencilMask)
    {
        useStencil = true;

        nvrhi::Format depthFormat = sourceDepth->GetDesc().format;

        if (depthFormat == nvrhi::Format::D24S8)
            stencilFormat = nvrhi::Format::X24G8_UINT;
        else if (depthFormat == nvrhi::Format::D32S8)
            stencilFormat = nvrhi::Format::X32G8_UINT;
        else
            assert(!"the format of sourceDepth texture doesn't have a stencil plane");
    }

    std::vector<ShaderMacro> MotionVectorMacros;
    MotionVectorMacros.push_back(ShaderMacro("USE_STENCIL", useStencil ? "1" : "0"));
    m_MotionVectorPS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/motion_vectors_ps.hlsl", "main", &MotionVectorMacros, nvrhi::ShaderType::SHADER_PIXEL);
    
    std::vector<ShaderMacro> ResolveMacros;
    ResolveMacros.push_back(ShaderMacro("SAMPLE_COUNT", std::to_string(unresolvedColorDesc.sampleCount)));
    ResolveMacros.push_back(ShaderMacro("USE_CATMULL_ROM_FILTER", useCatmullRomFilter ? "1" : "0"));
    m_TemporalAntiAliasingCS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/taa_cs.hlsl", "main", &ResolveMacros, nvrhi::ShaderType::SHADER_COMPUTE);

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_BORDER;
    samplerDesc.borderColor = nvrhi::Color(0.0f);
    m_BilinearSampler = device->createSampler(samplerDesc);

    m_ResolvedColorSize = float2(float(resolvedColorDesc.width), float(resolvedColorDesc.height));

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(TemporalAntiAliasingConstants);
    constantBufferDesc.debugName = "TemporalAntiAliasingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_TemporalAntiAliasingCB = device->createBuffer(constantBufferDesc);

    if(sourceDepth)
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV }
        };

        if (useStencil)
        {
            layoutDesc.PS.push_back({ 1, nvrhi::ResourceType::Texture_SRV });
        }

        m_MotionVectorsBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_TemporalAntiAliasingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceDepth),
        };
        if (useStencil)
        {
            bindingSetDesc.PS.push_back(nvrhi::BindingSetItem::Texture_SRV(1, sourceDepth, stencilFormat));
        }
        m_MotionVectorsBindingSet = device->createBindingSet(bindingSetDesc, m_MotionVectorsBindingLayout);

        m_MotionVectorsFramebufferFactory = std::make_unique<FramebufferFactory>(device);
        m_MotionVectorsFramebufferFactory->RenderTargets = { motionVectors };

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_MotionVectorPS;
        pipelineDesc.bindingLayouts = { m_MotionVectorsBindingLayout };

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        nvrhi::IFramebuffer* sampleFramebuffer = m_MotionVectorsFramebufferFactory->GetFramebuffer(*sampleView);
        m_MotionVectorsPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.CS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Sampler },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 1, nvrhi::ResourceType::Texture_SRV },
            { 2, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Texture_UAV },
            { 1, nvrhi::ResourceType::Texture_UAV },
        };
        m_ResolveBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.CS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_TemporalAntiAliasingCB),
            nvrhi::BindingSetItem::Sampler(0, m_BilinearSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, unresolvedColor),
            nvrhi::BindingSetItem::Texture_SRV(1, motionVectors),
            nvrhi::BindingSetItem::Texture_SRV(2, feedback1),
            nvrhi::BindingSetItem::Texture_UAV(0, resolvedColor),
            nvrhi::BindingSetItem::Texture_UAV(1, feedback2)
        };
        m_ResolveBindingSet = device->createBindingSet(bindingSetDesc, m_ResolveBindingLayout);

        // Swap resolvedColor and resolvedColorPrevious (t2 and u0)
        bindingSetDesc.CS[4].resourceHandle = feedback2;
        bindingSetDesc.CS[6].resourceHandle = feedback1;
        m_ResolveBindingSetPrevious = device->createBindingSet(bindingSetDesc, m_ResolveBindingLayout);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = m_TemporalAntiAliasingCS;
        pipelineDesc.bindingLayouts = { m_ResolveBindingLayout };

        m_ResolvePso = device->createComputePipeline(pipelineDesc);
    }
}

void TemporalAntiAliasingPass::RenderMotionVectors(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const ICompositeView& compositeViewPrevious)
{
    assert(compositeView.GetNumChildViews(ViewType::PLANAR) == compositeViewPrevious.GetNumChildViews(ViewType::PLANAR));
    assert(m_MotionVectorsPso);

    commandList->beginMarker("MotionVectors");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const IView* viewPrevious = compositeViewPrevious.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::ViewportState viewportState = view->GetViewportState();
        nvrhi::ViewportState prevViewportState = viewPrevious->GetViewportState();

        // This pass only works for planar, single-viewport views
        assert(viewportState.viewports.size() == 1 && prevViewportState.viewports.size() == 1);

        const nvrhi::Viewport& prevViewport = prevViewportState.viewports[0];

        TemporalAntiAliasingConstants taaConstants = {};
        affine3 viewReprojection = inverse(view->GetViewMatrix()) * viewPrevious->GetViewMatrix();
        taaConstants.reprojectionMatrix = inverse(view->GetProjectionMatrix(false)) * affineToHomogeneous(viewReprojection) * viewPrevious->GetProjectionMatrix(false);
        taaConstants.previousViewOrigin = float2(prevViewport.minX, prevViewport.minY);
        taaConstants.previousViewSize = float2(prevViewport.width(), prevViewport.height());
        taaConstants.stencilMask = m_StencilMask;
        commandList->writeBuffer(m_TemporalAntiAliasingCB, &taaConstants, sizeof(taaConstants));

        nvrhi::GraphicsState state;
        state.pipeline = m_MotionVectorsPso;
        state.framebuffer = m_MotionVectorsFramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_MotionVectorsBindingSet};
        state.viewport = viewportState;
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}

void TemporalAntiAliasingPass::TemporalResolve(
    nvrhi::ICommandList* commandList,
    const TemporalAntiAliasingParameters& params,
    bool feedbackIsValid,
    const ICompositeView& compositeView,
    const ICompositeView& compositeViewPrevious)
{
    assert(compositeView.GetNumChildViews(ViewType::PLANAR) == compositeViewPrevious.GetNumChildViews(ViewType::PLANAR));

    commandList->beginMarker("TemporalAA");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const IView* viewPrevious = compositeViewPrevious.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::ViewportState viewportState = view->GetViewportState();
        nvrhi::Rect previousExtent = viewPrevious->GetViewExtent();

        for (uint viewportIndex = 0; viewportIndex < viewportState.scissorRects.size(); viewportIndex++)
        {
            const nvrhi::Rect& scissorRect = viewportState.scissorRects[viewportIndex];

            TemporalAntiAliasingConstants taaConstants = {};
            int marginSize = 1;
            taaConstants.previousViewOrigin = float2(float(previousExtent.minX + marginSize), float(previousExtent.minY + marginSize));
            taaConstants.previousViewSize = float2(float(previousExtent.maxX - previousExtent.minX - marginSize * 2), float(previousExtent.maxY - previousExtent.minY - marginSize * 2));
            taaConstants.viewOrigin = float2(float(scissorRect.minX), float(scissorRect.minY));
            taaConstants.viewSize = float2(float(scissorRect.maxX - scissorRect.minX), float(scissorRect.maxY - scissorRect.minY));
            taaConstants.sourceTextureSizeInv = 1.0f / m_ResolvedColorSize;
            taaConstants.clampingFactor = params.enableHistoryClamping ? params.clampingFactor : -1.f;
            taaConstants.newFrameWeight = feedbackIsValid ? params.newFrameWeight : 1.f;
            commandList->writeBuffer(m_TemporalAntiAliasingCB, &taaConstants, sizeof(taaConstants));

            int2 viewportSize = int2(taaConstants.viewSize);
            int2 gridSize = (viewportSize + 15) / 16;

            nvrhi::ComputeState state;
            state.pipeline = m_ResolvePso;
            state.bindings = { m_ResolveBindingSet };
            commandList->setComputeState(state);

            commandList->dispatch(gridSize.x, gridSize.y, 1);
        }
    }

    commandList->endMarker();
}

void TemporalAntiAliasingPass::AdvanceFrame()
{
    m_FrameIndex = (m_FrameIndex + 1) % 8;

    std::swap(m_ResolveBindingSet, m_ResolveBindingSetPrevious);
}

dm::float2 TemporalAntiAliasingPass::GetCurrentPixelOffset()
{
    const float2 offsets[] = {
        float2(0.0625f, -0.1875f), float2(-0.0625f, 0.1875f), float2(0.3125f, 0.0625f), float2(-0.1875f, -0.3125f),
        float2(-0.3125f, 0.3125f), float2(-0.4375f, 0.0625f), float2(0.1875f, 0.4375f), float2(0.4375f, -0.4375f)
    };

    return offsets[m_FrameIndex];
}
