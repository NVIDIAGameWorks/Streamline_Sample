
#include <donut/render/EnvironmentMapPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <donut/core/math/math.h>

using namespace donut::math;
#include <donut/shaders/sky_cb.h>

using namespace donut::engine;
using namespace donut::render;

EnvironmentMapPass::EnvironmentMapPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView,
    nvrhi::ITexture* environmentMap)
    : m_CommonPasses(commonPasses)
    , m_FramebufferFactory(framebufferFactory)
{
    nvrhi::TextureDimension envMapDimension = environmentMap->GetDesc().dimension;
    bool isCubeMap = (envMapDimension == nvrhi::TextureDimension::TextureCube) || 
        (envMapDimension == nvrhi::TextureDimension::TextureCubeArray);

    std::vector<engine::ShaderMacro> PSMacros;
    PSMacros.push_back(engine::ShaderMacro("LATLONG_TEXTURE", isCubeMap ? "0" : "1"));

    m_PixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/environment_map_ps.hlsl", "main", 
        &PSMacros, nvrhi::ShaderType::SHADER_PIXEL);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(SkyConstants);
    constantBufferDesc.debugName = "DeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_SkyCB = device->createBuffer(constantBufferDesc);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Sampler }
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_SkyCB),
            nvrhi::BindingSetItem::Texture_SRV(0, environmentMap),
            nvrhi::BindingSetItem::Sampler(0, commonPasses->m_LinearWrapSampler)
        };
        m_RenderBindingSet = device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = sampleView->IsReverseDepth() ? m_CommonPasses->m_FullscreenVS : m_CommonPasses->m_FullscreenAtOneVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout };

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = true;
        pipelineDesc.renderState.depthStencilState.depthFunc = sampleView->IsReverseDepth() 
            ? nvrhi::DepthStencilState::COMPARISON_GREATER_EQUAL 
            : nvrhi::DepthStencilState::COMPARISON_LESS_EQUAL;
        pipelineDesc.renderState.depthStencilState.depthWriteMask = nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ZERO;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

void EnvironmentMapPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView)
{
    commandList->beginMarker("Environment Map");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_RenderBindingSet };
        state.viewport = view->GetViewportState();

        SkyConstants skyConstants = {};
        skyConstants.matClipToTranslatedWorld = view->GetInverseViewProjectionMatrix() * affineToHomogeneous(translation(-view->GetViewOrigin()));
        commandList->writeBuffer(m_SkyCB, &skyConstants, sizeof(skyConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}
