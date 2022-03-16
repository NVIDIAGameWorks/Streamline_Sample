
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>

using namespace donut::math;
#include <donut/shaders/deferred_lighting_cb.h>

using namespace donut::engine;
using namespace donut::render;

DeferredLightingPass::DeferredLightingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_CommonPasses(commonPasses)
    , m_Device(device)
{
}

void donut::render::DeferredLightingPass::Init(ShaderFactory& shaderFactory, FramebufferFactory& framebufferFactory, const ICompositeView& compositeView)
{
    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_BORDER;
    samplerDesc.borderColor = nvrhi::Color(1.0f);
    m_ShadowSampler = m_Device->createSampler(samplerDesc);

    samplerDesc.reductionType = nvrhi::SamplerDesc::REDUCTION_COMPARISON;
    m_ShadowSamplerComparison = m_Device->createSampler(samplerDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(DeferredLightingConstants);
    constantBufferDesc.debugName = "DeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_DeferredLightingCB = m_Device->createBuffer(constantBufferDesc);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = framebufferFactory.GetFramebuffer(*sampleView);

    {
        m_GBufferBindingLayout = CreateGBufferBindingLayout();

        nvrhi::BindingLayoutDesc shadowMapLayoutDesc;
        shadowMapLayoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Sampler },
            { 1, nvrhi::ResourceType::Sampler }
        };
        m_ShadowMapBindingLayout = m_Device->createBindingLayout(shadowMapLayoutDesc);

        nvrhi::BindingLayoutDesc lightProbeBindingDesc;
        lightProbeBindingDesc.PS = {
            { 1, nvrhi::ResourceType::Texture_SRV },
            { 2, nvrhi::ResourceType::Texture_SRV },
            { 3, nvrhi::ResourceType::Texture_SRV },
            { 2, nvrhi::ResourceType::Sampler },
            { 3, nvrhi::ResourceType::Sampler }
        };
        m_LightProbeBindingLayout = m_Device->createBindingLayout(lightProbeBindingDesc);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
        pipelineDesc.PS = CreatePixelShader(shaderFactory, sampleView);
        pipelineDesc.bindingLayouts = { m_GBufferBindingLayout, m_ShadowMapBindingLayout, m_LightProbeBindingLayout };

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_Pso = m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

nvrhi::ShaderHandle DeferredLightingPass::CreatePixelShader(ShaderFactory& shaderFactory, const IView* sampleView)
{
    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/deferred_lighting_ps.hlsl", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
}

nvrhi::BindingLayoutHandle donut::render::DeferredLightingPass::CreateGBufferBindingLayout()
{
    nvrhi::BindingLayoutDesc gbufferLayoutDesc;
    gbufferLayoutDesc.PS = {
        { 8, nvrhi::ResourceType::Texture_SRV },
        { 9, nvrhi::ResourceType::Texture_SRV },
        { 10, nvrhi::ResourceType::Texture_SRV },
        { 11, nvrhi::ResourceType::Texture_SRV },
        { 12, nvrhi::ResourceType::Texture_SRV },
        { 13, nvrhi::ResourceType::Texture_SRV }
    };
    return m_Device->createBindingLayout(gbufferLayoutDesc);
}

nvrhi::BindingSetHandle DeferredLightingPass::CreateGBufferBindingSet(
    nvrhi::TextureSubresourceSet subresources,
    nvrhi::ITexture* depth, 
    nvrhi::ITexture* albedo,
    nvrhi::ITexture* specular,
    nvrhi::ITexture* normals,
    nvrhi::ITexture* indirectDiffuse,
    nvrhi::ITexture* indirectSpecular)
{
    nvrhi::BindingSetDesc gbufferBindingDesc;

    gbufferBindingDesc.PS = {
        nvrhi::BindingSetItem::Texture_SRV(8, depth, nvrhi::Format::UNKNOWN, subresources),
        nvrhi::BindingSetItem::Texture_SRV(9, albedo, nvrhi::Format::UNKNOWN, subresources),
        nvrhi::BindingSetItem::Texture_SRV(10, specular, nvrhi::Format::UNKNOWN, subresources),
        nvrhi::BindingSetItem::Texture_SRV(11, normals, nvrhi::Format::UNKNOWN, subresources),
        nvrhi::BindingSetItem::Texture_SRV(12, indirectDiffuse ? indirectDiffuse : m_CommonPasses->m_BlackTexture.Get(), nvrhi::Format::UNKNOWN, indirectDiffuse ? subresources : nvrhi::AllSubresources),
        nvrhi::BindingSetItem::Texture_SRV(13, indirectSpecular ? indirectSpecular : m_CommonPasses->m_BlackTexture.Get(), nvrhi::Format::UNKNOWN, indirectDiffuse ? subresources : nvrhi::AllSubresources),
    };

    return m_Device->createBindingSet(gbufferBindingDesc, m_GBufferBindingLayout);
}

void DeferredLightingPass::Render(
    nvrhi::ICommandList* commandList,
    FramebufferFactory& framebufferFactory,
    const ICompositeView& compositeView,
    const std::vector<std::shared_ptr<Light>>& lights,
    nvrhi::IBindingSet* gbufferBindingSet,
    dm::float3 ambientColorTop,
    dm::float3 ambientColorBottom,
    const std::vector<std::shared_ptr<LightProbe>>& lightProbes,
    dm::float2 randomOffset)
{
    commandList->beginMarker("DeferredLighting");

    nvrhi::ITexture* shadowMapTexture = nullptr;
    int2 shadowMapTextureSize = 0;
    for (auto light : lights)
    {
        if (light->shadowMap)
        {
            shadowMapTexture = light->shadowMap->GetTexture();
            shadowMapTextureSize = light->shadowMap->GetTextureSize();
            break;
        }
    }

    nvrhi::BindingSetHandle& shadowMapBindings = m_ShadowMapBindingSets[shadowMapTexture];

    if (!shadowMapBindings)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_DeferredLightingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, shadowMapTexture ? shadowMapTexture : m_CommonPasses->m_ZeroDepthStencil2DArray.Get()),
            nvrhi::BindingSetItem::Sampler(0, m_ShadowSampler),
            nvrhi::BindingSetItem::Sampler(1, m_ShadowSamplerComparison)
        };

        shadowMapBindings = m_Device->createBindingSet(bindingSetDesc, m_ShadowMapBindingLayout);
    }

    nvrhi::ITexture* lightProbeDiffuse = nullptr;
    nvrhi::ITexture* lightProbeSpecular = nullptr;
    nvrhi::ITexture* lightProbeEnvironmentBrdf = nullptr;

    for (auto probe : lightProbes)
    {
        if (!probe->enabled)
            continue;;

        if (lightProbeDiffuse == nullptr || lightProbeSpecular == nullptr || lightProbeEnvironmentBrdf == nullptr)
        {
            lightProbeDiffuse = probe->diffuseMap;
            lightProbeSpecular = probe->specularMap;
            lightProbeEnvironmentBrdf = probe->environmentBrdf;
        }
        else
        {
            if (lightProbeDiffuse != probe->diffuseMap || lightProbeSpecular != probe->specularMap || lightProbeEnvironmentBrdf != probe->environmentBrdf)
            {
                log::error("All lights probe submitted to DeferredLightingPass::Render(...) must use the same set of textures");
                return;
            }
        }
    }

    nvrhi::BindingSetHandle& lightProbeBindings = m_LightProbeBindingSets[lightProbeDiffuse];

    if (!lightProbeBindings)
    {
        nvrhi::BindingSetDesc bindingSetDesc;

        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::Texture_SRV(1, lightProbeDiffuse ? lightProbeDiffuse : m_CommonPasses->m_BlackCubeMapArray.Get()),
            nvrhi::BindingSetItem::Texture_SRV(2, lightProbeSpecular ? lightProbeSpecular : m_CommonPasses->m_BlackCubeMapArray.Get()),
            nvrhi::BindingSetItem::Texture_SRV(3, lightProbeEnvironmentBrdf ? lightProbeEnvironmentBrdf : m_CommonPasses->m_BlackTexture.Get()),
            nvrhi::BindingSetItem::Sampler(2, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(3, m_CommonPasses->m_LinearClampSampler)
        };
        
        lightProbeBindings = m_Device->createBindingSet(bindingSetDesc, m_LightProbeBindingLayout);
    }

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        DeferredLightingConstants deferredConstants = {};
        view->FillPlanarViewConstants(deferredConstants.view);
        deferredConstants.shadowMapTextureSize = float2(shadowMapTextureSize);
        deferredConstants.randomOffset = randomOffset;
        deferredConstants.noisePattern[0] = float4(0.059f, 0.529f, 0.176f, 0.647f);
        deferredConstants.noisePattern[1] = float4(0.765f, 0.294f, 0.882f, 0.412f);
        deferredConstants.noisePattern[2] = float4(0.235f, 0.706f, 0.118f, 0.588f);
        deferredConstants.noisePattern[3] = float4(0.941f, 0.471f, 0.824f, 0.353f);

        int numShadows = 0;

        for (int nLight = 0; nLight < std::min(static_cast<int>(lights.size()), DEFERRED_MAX_LIGHTS); nLight++)
        {
            const auto& light = lights[nLight];

            LightConstants& lightConstants = deferredConstants.lights[deferredConstants.numLights];
            light->FillLightConstants(lightConstants);

            if (light->shadowMap)
            {
                for (uint32_t cascade = 0; cascade < light->shadowMap->GetNumberOfCascades(); cascade++)
                {
                    if (numShadows < DEFERRED_MAX_LIGHTS)
                    {
                        light->shadowMap->GetCascade(cascade)->FillShadowConstants(deferredConstants.shadows[numShadows]);
                        lightConstants.shadowCascades[cascade] = numShadows;
                        ++numShadows;
                    }
                }

                for (uint32_t perObjectShadow = 0; perObjectShadow < light->shadowMap->GetNumberOfPerObjectShadows(); perObjectShadow++)
                {
                    if (numShadows < DEFERRED_MAX_LIGHTS)
                    {
                        light->shadowMap->GetPerObjectShadow(perObjectShadow)->FillShadowConstants(deferredConstants.shadows[numShadows]);
                        lightConstants.perObjectShadows[perObjectShadow] = numShadows;
                        ++numShadows;
                    }
                }
            }

            ++deferredConstants.numLights;
        }

        deferredConstants.ambientColorTop = float4(ambientColorTop, 0.f);
        deferredConstants.ambientColorBottom = float4(ambientColorBottom, 0.f);
        
        deferredConstants.gbufferArraySlice = view->GetSubresources().baseArraySlice;

        for (auto probe : lightProbes)
        {
            if (!probe->IsActive())
                continue;

            LightProbeConstants& lightProbeConstants = deferredConstants.lightProbes[deferredConstants.numLightProbes];
            probe->FillLightProbeConstants(lightProbeConstants);

            ++deferredConstants.numLightProbes;

            if (deferredConstants.numLightProbes >= DEFERRED_MAX_LIGHT_PROBES)
                break;
        }

        deferredConstants.indirectDiffuseScale = 1.f;
        deferredConstants.indirectSpecularScale = 1.f;
        
        commandList->writeBuffer(m_DeferredLightingCB, &deferredConstants, sizeof(deferredConstants));

        nvrhi::GraphicsState state;
        state.pipeline = m_Pso;
        state.framebuffer = framebufferFactory.GetFramebuffer(*view);
        state.bindings = { gbufferBindingSet, shadowMapBindings, lightProbeBindings };
        state.viewport = view->GetViewportState();

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}

void DeferredLightingPass::ResetBindingCache()
{
    m_ShadowMapBindingSets.clear();
    m_LightProbeBindingSets.clear();
}
