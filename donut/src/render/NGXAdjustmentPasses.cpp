#include <donut/render/NGXAdjustmentPasses.h>

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/core/log.h>

using namespace donut::math;
#include <donut/shaders/ngx_adjust_cb.h>

using namespace donut::engine;
using namespace donut::render;

NGXAdjustmentPasses::NGXAdjustmentPasses(nvrhi::IDevice* device, std::shared_ptr<ShaderFactory> shaderFactory, std::shared_ptr<CommonRenderPasses> commonPasses, std::shared_ptr<FramebufferFactory> framebufferFactory, const ICompositeView& compositeView)
    : m_Device(device), m_CommonPasses(commonPasses), m_FramebufferFactory(framebufferFactory)
{
    const IView* view = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* framebuffer = m_FramebufferFactory->GetFramebuffer(*view);

    std::vector<donut::engine::ShaderMacro> Macros;
    m_PixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/ngx_adjust_ps.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_PIXEL);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(NGXAdjustmentConstants);
    constantBufferDesc.debugName = "NGXAdjustmentConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_ToneMappingCB = device->createBuffer(constantBufferDesc);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.PS = {
        { 0, nvrhi::ResourceType::VolatileConstantBuffer },
        { 0, nvrhi::ResourceType::Texture_SRV },
        { 1, nvrhi::ResourceType::Texture_SRV }
    };
    m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
    pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
    pipelineDesc.PS = m_PixelShader;
    pipelineDesc.bindingLayouts = { m_RenderBindingLayout };

    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
    pipelineDesc.renderState.depthStencilState.depthEnable = false;
    pipelineDesc.renderState.depthStencilState.stencilEnable = false;

    m_RenderPso = device->createGraphicsPipeline(pipelineDesc, framebuffer);

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
}

void NGXAdjustmentPasses::Render(
    nvrhi::ICommandList* commandList,
    nvrhi::ITexture* exposureTexture,
    nvrhi::ITexture* sourceTexture,
    const ICompositeView& compositeView,
    bool isHDR,
    float postExposureScale,
    float postBlackLevel,
    float postWhiteLevel,
    float postGamma)
{
    commandList->beginMarker("NGXAdjustment");

    nvrhi::ITexture* exposure = exposureTexture;
    if (!exposure)
    {
        commandList->clearTextureFloat(m_ExposureTexture, nvrhi::AllSubresources, nvrhi::Color(1.f));
        exposure = m_ExposureTexture;
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.PS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ToneMappingCB),
        nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
        nvrhi::BindingSetItem::Texture_SRV(1, exposure)
    };
    nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);

    for (unsigned viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { bindingSet };
        state.viewport = view->GetViewportState();

        NGXAdjustmentConstants cbData = {};
        cbData.isHDR = isHDR;
        cbData.postExposureScale = postExposureScale;
        cbData.postBlackLevel = postBlackLevel;
        cbData.postWhiteLevel = postWhiteLevel;
        cbData.postGamma = postGamma;
        commandList->writeBuffer(m_ToneMappingCB, &cbData, sizeof(cbData));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}
