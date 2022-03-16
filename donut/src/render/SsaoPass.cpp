
#include <donut/render/SsaoPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/ssao_cb.h>

#include <sstream>
#include <assert.h>

using namespace donut::engine;
using namespace donut::render;

SsaoPass::SsaoPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    nvrhi::ITexture* gbufferDepth,
    nvrhi::ITexture* gbufferNormals,
    const ICompositeView& compositeView,
    bool useMultiplicativeBlending)
    : m_CommonPasses(commonPasses)
    , m_FramebufferFactory(framebufferFactory)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    const nvrhi::TextureDesc& depthDesc = gbufferDepth->GetDesc();
    const nvrhi::TextureDesc& normalsDesc = gbufferNormals->GetDesc();
    assert(depthDesc.sampleCount == normalsDesc.sampleCount);

    std::vector<ShaderMacro> Macros;
    Macros.push_back(ShaderMacro("SAMPLE_COUNT", std::to_string(depthDesc.sampleCount)));
    m_PixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/ssao_ps.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_PIXEL);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(SsaoConstants);
    constantBufferDesc.debugName = "SsaoConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_SsaoCB = device->createBuffer(constantBufferDesc);
    
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 1, nvrhi::ResourceType::Texture_SRV }
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_SsaoCB),
            nvrhi::BindingSetItem::Texture_SRV(0, gbufferDepth),
            nvrhi::BindingSetItem::Texture_SRV(1, gbufferNormals)
        };
        m_RenderBindingSet = device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout };

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        if (useMultiplicativeBlending)
        {
            pipelineDesc.renderState.blendState.blendEnable[0] = true;
            pipelineDesc.renderState.blendState.blendOp[0] = nvrhi::BlendState::BlendOp::BLEND_OP_ADD;
            pipelineDesc.renderState.blendState.srcBlend[0] = nvrhi::BlendState::BlendValue::BLEND_DEST_COLOR;
            pipelineDesc.renderState.blendState.destBlend[0] = nvrhi::BlendState::BlendValue::BLEND_ZERO;
            pipelineDesc.renderState.blendState.srcBlendAlpha[0] = nvrhi::BlendState::BlendValue::BLEND_ZERO;
            pipelineDesc.renderState.blendState.destBlendAlpha[0] = nvrhi::BlendState::BlendValue::BLEND_ONE;
        }

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}


void SsaoPass::Render(
    nvrhi::ICommandList* commandList,
    const SsaoParameters& params,
    const ICompositeView& compositeView,
    dm::float2 randomOffset)
{
    commandList->beginMarker("SSAO");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_RenderBindingSet };
        state.viewport = view->GetViewportState();

        // This pass only works for planar, single-viewport views
        assert(state.viewport.viewports.size() == 1);

        const nvrhi::Viewport& viewport = state.viewport.viewports[0];
        const float4x4 projectionMatrix = view->GetProjectionMatrix(false);

        SsaoConstants ssaoConstants = {};
        ssaoConstants.matClipToView = inverse(projectionMatrix);
        ssaoConstants.matWorldToView = affineToHomogeneous(view->GetViewMatrix());
        ssaoConstants.randomOffset = randomOffset;
        ssaoConstants.windowToClipScale.x = 2.0f / (viewport.maxX - viewport.minX);
        ssaoConstants.windowToClipScale.y = -2.0f / (viewport.maxY - viewport.minY);
        ssaoConstants.windowToClipBias.x = -1.f - viewport.minX * ssaoConstants.windowToClipScale.x;
        ssaoConstants.windowToClipBias.y = 1.f + viewport.minY * ssaoConstants.windowToClipScale.y;
        ssaoConstants.clipToWindowScale.x = 0.5f * (viewport.maxX - viewport.minX);
        ssaoConstants.clipToWindowScale.y = -0.5f * (viewport.maxY - viewport.minY);
        ssaoConstants.clipToWindowBias.x = (viewport.minX + viewport.maxX) * 0.5f;
        ssaoConstants.clipToWindowBias.y = (viewport.minY + viewport.maxY) * 0.5f;
        ssaoConstants.radiusWorldToClip.x = abs(projectionMatrix[0][0] / projectionMatrix[2][3]);
        ssaoConstants.radiusWorldToClip.y = abs(projectionMatrix[1][1] / projectionMatrix[2][3]);
        ssaoConstants.amount = params.amount;
        ssaoConstants.invBackgroundViewDepth = (params.backgroundViewDepth > 0.f) ? 1.f / params.backgroundViewDepth : 0.f;
        ssaoConstants.radiusWorld = params.radiusWorld;
        ssaoConstants.surfaceBias = params.surfaceBias;
        ssaoConstants.powerExponent = params.powerExponent;
        commandList->writeBuffer(m_SsaoCB, &ssaoConstants, sizeof(ssaoConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}
