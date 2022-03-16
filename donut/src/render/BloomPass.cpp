
#include <donut/render/BloomPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/blit_cb.h>
#include <donut/shaders/bloom_cb.h>

#include <assert.h>

using namespace donut::engine;
using namespace donut::render;

const int NUM_BLOOM_PASSES = 1;

BloomPass::BloomPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
    , m_FramebufferFactory(framebufferFactory)
{
    m_BloomBlurPixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/bloom_ps.hlsl", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(BloomConstants);
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.debugName = "BloomConstantsH";
    m_BloomHBlurCB = device->createBuffer(constantBufferDesc);
    constantBufferDesc.debugName = "BloomConstantsV";
    m_BloomVBlurCB = device->createBuffer(constantBufferDesc);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.PS = {
        { 0, nvrhi::ResourceType::VolatileConstantBuffer },
        { 0, nvrhi::ResourceType::Sampler },
        { 0, nvrhi::ResourceType::Texture_SRV }
    };
    m_BloomBlurBindingLayout = device->createBindingLayout(layoutDesc);

    m_PerViewData.resize(compositeView.GetNumChildViews(ViewType::PLANAR));
    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        
        nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        PerViewData& perViewData = m_PerViewData[viewIndex];

        nvrhi::Rect viewExtent = view->GetViewExtent();
        int viewportWidth = viewExtent.maxX - viewExtent.minX;
        int viewportHeight = viewExtent.maxY - viewExtent.minY;

        // temporary textures for downscaling
        nvrhi::TextureDesc downscaleTextureDesc;
        downscaleTextureDesc.format = sampleFramebuffer->GetFramebufferInfo().colorFormats[0];
        downscaleTextureDesc.width = (uint32_t)ceil(viewportWidth / 2.f);
        downscaleTextureDesc.height = (uint32_t)ceil(viewportHeight / 2.f);
        downscaleTextureDesc.mipLevels = 1;
        downscaleTextureDesc.isRenderTarget = true;
        downscaleTextureDesc.debugName = "bloom src mip1";
        perViewData.textureDownscale1 = m_Device->createTexture(downscaleTextureDesc);
        perViewData.framebufferDownscale1 = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(perViewData.textureDownscale1, 0, 0)));

        downscaleTextureDesc.debugName = "bloom src mip2";
        downscaleTextureDesc.width = (uint32_t)ceil(downscaleTextureDesc.width / 2.f);
        downscaleTextureDesc.height = (uint32_t)ceil(downscaleTextureDesc.height / 2.f);
        perViewData.textureDownscale2 = m_Device->createTexture(downscaleTextureDesc);
        perViewData.framebufferDownscale2 = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(perViewData.textureDownscale2, 0, 0)));

        // intermediate textures for accumulating blur
        nvrhi::TextureDesc intermediateTextureDesc2;
        intermediateTextureDesc2.format = downscaleTextureDesc.format;
        intermediateTextureDesc2.width = downscaleTextureDesc.width;
        intermediateTextureDesc2.height = downscaleTextureDesc.height;
        intermediateTextureDesc2.mipLevels = 1;
        intermediateTextureDesc2.isRenderTarget = true;

        intermediateTextureDesc2.debugName = "bloom accumulation pass1";
        perViewData.texturePass1Blur = m_Device->createTexture(intermediateTextureDesc2);
        perViewData.framebufferPass1Blur = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(perViewData.texturePass1Blur, 0, 0)));

        intermediateTextureDesc2.debugName = "bloom accumulation pass2";
        perViewData.texturePass2Blur = m_Device->createTexture(intermediateTextureDesc2);
        perViewData.framebufferPass2Blur = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(perViewData.texturePass2Blur, 0, 0)));

        nvrhi::GraphicsPipelineDesc graphicsPipelineDesc;
        graphicsPipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        graphicsPipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        graphicsPipelineDesc.PS = m_BloomBlurPixelShader;
        graphicsPipelineDesc.bindingLayouts = { m_BloomBlurBindingLayout };
        graphicsPipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        graphicsPipelineDesc.renderState.depthStencilState.depthEnable = false;
        graphicsPipelineDesc.renderState.depthStencilState.stencilEnable = false;
        perViewData.bloomBlurPso = device->createGraphicsPipeline(graphicsPipelineDesc, perViewData.framebufferPass1Blur);

        if (true) // additive energy conservative blending for final composite - disable to debug blur
        {
            graphicsPipelineDesc.renderState.blendState.blendEnable[0] = true;
            graphicsPipelineDesc.renderState.blendState.blendOp[0] = nvrhi::BlendState::BlendOp::BLEND_OP_ADD;
            graphicsPipelineDesc.renderState.blendState.srcBlend[0] = nvrhi::BlendState::BlendValue::BLEND_BLEND_FACTOR;
            graphicsPipelineDesc.renderState.blendState.destBlend[0] = nvrhi::BlendState::BlendValue::BLEND_INV_BLEND_FACTOR;
            graphicsPipelineDesc.renderState.blendState.srcBlendAlpha[0] = nvrhi::BlendState::BlendValue::BLEND_ZERO;
            graphicsPipelineDesc.renderState.blendState.destBlendAlpha[0] = nvrhi::BlendState::BlendValue::BLEND_ONE;

            // TODO: the blend factor should be dynamic, but nvrhi doesn't support that yet
            graphicsPipelineDesc.renderState.blendState.blendFactor = nvrhi::Color(0.05f);
        }

        graphicsPipelineDesc.bindingLayouts = { commonPasses->m_BlitBindingLayout };
        graphicsPipelineDesc.VS = commonPasses->m_RectVS;
        graphicsPipelineDesc.PS = commonPasses->m_BlitPS;
        perViewData.bloomApplyPso = m_Device->createGraphicsPipeline(graphicsPipelineDesc, sampleFramebuffer);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_BloomHBlurCB),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, perViewData.textureDownscale2)
        };
        perViewData.bloomBlurBindingSetPass1 = m_Device->createBindingSet(bindingSetDesc, m_BloomBlurBindingLayout);

        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_BloomVBlurCB),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, perViewData.texturePass1Blur)
        };
        perViewData.bloomBlurBindingSetPass2 = m_Device->createBindingSet(bindingSetDesc, m_BloomBlurBindingLayout);

        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_BloomHBlurCB),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, perViewData.texturePass2Blur)
        };
        perViewData.bloomBlurBindingSetPass3 = m_Device->createBindingSet(bindingSetDesc, m_BloomBlurBindingLayout);

        perViewData.blitFromDownscale1BindingSet = m_CommonPasses->CreateBlitBindingSet(perViewData.textureDownscale1, 0, 0);
        perViewData.compositeBlitBindingSet = m_CommonPasses->CreateBlitBindingSet(perViewData.texturePass2Blur, 0, 0);
    }
}

void BloomPass::Render(
	nvrhi::ICommandList* commandList,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView,
	nvrhi::ITexture* sourceDestTexture,
    float sigmaInPixels)
{
    float effectiveSigma = clamp(sigmaInPixels * 0.25f, 1.f, 100.f);

    commandList->beginMarker("Bloom");

    nvrhi::DrawArguments fullscreenquadargs;
    fullscreenquadargs.instanceCount = 1;
    fullscreenquadargs.vertexCount = 4;

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        nvrhi::IFramebuffer* framebuffer = framebufferFactory->GetFramebuffer(*view);
        PerViewData& perViewData = m_PerViewData[viewIndex];

        nvrhi::ViewportState viewportState = view->GetViewportState();
        const nvrhi::Rect& scissorRect = viewportState.scissorRects[0];
        const nvrhi::FramebufferInfo& fbinfo = framebuffer->GetFramebufferInfo();

        dm::box2 uvSrcRect = box2(
            float2(
                scissorRect.minX / (float)fbinfo.width,
                scissorRect.minY / (float)fbinfo.height),
            float2(
                scissorRect.maxX / (float)fbinfo.width,
                scissorRect.maxY / (float)fbinfo.height)
            );

        commandList->beginTrackingTextureState(perViewData.textureDownscale2, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(perViewData.textureDownscale1, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(perViewData.texturePass1Blur, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(perViewData.texturePass2Blur, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);

        commandList->beginMarker("Downscale");

        // downscale

        // half-scale down

        if (m_BlitBindingCache.find(sourceDestTexture) == m_BlitBindingCache.end())
            m_BlitBindingCache.emplace(sourceDestTexture, m_CommonPasses->CreateBlitBindingSet(sourceDestTexture, 0, 0));

        m_CommonPasses->BlitTexture(commandList, perViewData.framebufferDownscale1, nvrhi::Viewport(float(perViewData.textureDownscale1->GetDesc().width), float(perViewData.textureDownscale1->GetDesc().height)), box2(0.f, 1.f), m_BlitBindingCache[sourceDestTexture], uvSrcRect);

        // half-scale again down to quarter-scale

        m_CommonPasses->BlitTexture(commandList, perViewData.framebufferDownscale2, nvrhi::Viewport(float(perViewData.textureDownscale2->GetDesc().width), float(perViewData.textureDownscale2->GetDesc().height)), perViewData.blitFromDownscale1BindingSet);

        commandList->endMarker(); // "Downscale"

        // apply blur, composite
        {
            commandList->beginMarker("Blur");
            nvrhi::Viewport viewport;

            nvrhi::GraphicsState state;
            state.pipeline = perViewData.bloomBlurPso;
            viewport = nvrhi::Viewport(float(perViewData.texturePass1Blur->GetDesc().width), float(perViewData.texturePass1Blur->GetDesc().height));
            state.viewport.addViewport(viewport);
            state.viewport.addScissorRect(nvrhi::Rect(viewport));
            state.framebuffer = perViewData.framebufferPass1Blur;
            state.bindings = { perViewData.bloomBlurBindingSetPass1 };

            BloomConstants bloomHorizonal = {};
            bloomHorizonal.pixstep.x = 1.f / perViewData.texturePass1Blur->GetDesc().width;
            bloomHorizonal.pixstep.y = 0.f;
            bloomHorizonal.argumentScale = -1.f / (2 * effectiveSigma * effectiveSigma);
            bloomHorizonal.normalizationScale = 1.f / (sqrtf(2 * PI_f) * effectiveSigma);
            bloomHorizonal.numSamples = ::round(effectiveSigma * 4.f);
            BloomConstants bloomVertical = bloomHorizonal;
            bloomVertical.pixstep.x = 0.f;
            bloomVertical.pixstep.y = 1.f / perViewData.texturePass1Blur->GetDesc().height;
            commandList->writeBuffer(m_BloomHBlurCB, &bloomHorizonal, sizeof(bloomHorizonal));
            commandList->writeBuffer(m_BloomVBlurCB, &bloomVertical, sizeof(bloomVertical));

            for (int i = 0; i < NUM_BLOOM_PASSES; ++i)
            {
                commandList->setGraphicsState(state);
                commandList->draw(fullscreenquadargs); // blur to m_TexturePass1Blur or m_TexturePass3Blur

                viewport = nvrhi::Viewport(float(perViewData.texturePass2Blur->GetDesc().width), float(perViewData.texturePass2Blur->GetDesc().height));
                state.viewport.viewports[0] = viewport;
                state.viewport.scissorRects[0] = nvrhi::Rect(viewport);
                state.framebuffer = perViewData.framebufferPass2Blur;
                state.bindings = { perViewData.bloomBlurBindingSetPass2 };

                commandList->setGraphicsState(state);
                commandList->draw(fullscreenquadargs); // blur to m_TexturePass2Blur

                if (NUM_BLOOM_PASSES > 1)
                {
                    viewport = nvrhi::Viewport(float(perViewData.texturePass1Blur->GetDesc().width), float(perViewData.texturePass1Blur->GetDesc().height));
                    state.viewport.viewports[0] = viewport;
                    state.viewport.scissorRects[0] = nvrhi::Rect(viewport);
                    state.framebuffer = perViewData.framebufferPass1Blur;
                    state.bindings = { perViewData.bloomBlurBindingSetPass3 };
                }
            }

            commandList->endMarker(); // "Blur"

            commandList->beginMarker("Apply");
            {
                nvrhi::GraphicsState state;
                state.pipeline = perViewData.bloomApplyPso;
                state.framebuffer = framebuffer;
                state.bindings = { perViewData.compositeBlitBindingSet };
                state.viewport = viewportState; 

                BlitConstants blitConstants = {};
                blitConstants.sourceOrigin = float2(0, 0);
                blitConstants.sourceSize = float2(1, 1);
                blitConstants.targetOrigin = float2(0, 0);
                blitConstants.targetSize = float2(1, 1);

                commandList->writeBuffer(m_CommonPasses->m_BlitCB, &blitConstants, sizeof(blitConstants));

                commandList->setGraphicsState(state);
                commandList->draw(fullscreenquadargs);
            }
            commandList->endMarker(); // "Apply"
        }

        commandList->endTrackingTextureState(perViewData.textureDownscale2, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->endTrackingTextureState(perViewData.textureDownscale1, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->endTrackingTextureState(perViewData.texturePass1Blur, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->endTrackingTextureState(perViewData.texturePass2Blur, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
    }

    commandList->endMarker(); // "Bloom"
}
