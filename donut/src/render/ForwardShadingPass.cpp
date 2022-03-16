
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include <donut/shaders/forward_cb.h>

#ifdef _WIN32
#include <d3d11.h> // for nvapi.h to declare the custom semantic stuff
#include <nvapi.h>
#endif // _WIN32

using namespace donut::engine;
using namespace donut::render;

ForwardShadingPass::ForwardShadingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
{
}

void ForwardShadingPass::Init(ShaderFactory& shaderFactory, FramebufferFactory& framebufferFactory, const ICompositeView& compositeView, const CreateParameters& params)
{
    m_SupportedViewTypes = ViewType::PLANAR;
    if (params.enableSinglePassStereo)
        m_SupportedViewTypes = ViewType::Enum(m_SupportedViewTypes | ViewType::STEREO);
    if (params.enableSinglePassCubemap)
        m_SupportedViewTypes = ViewType::Enum(m_SupportedViewTypes | ViewType::CUBEMAP);

    const IView* sampleView = compositeView.GetChildView(m_SupportedViewTypes, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = framebufferFactory.GetFramebuffer(*sampleView);

    m_VertexShader = CreateVertexShader(shaderFactory, sampleView, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);
    m_GeometryShader = CreateGeometryShader(shaderFactory, sampleView, params);
    m_PixelShader = CreatePixelShader(shaderFactory, sampleView, params);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_BORDER;
    samplerDesc.borderColor = nvrhi::Color(1.0f);
    m_ShadowSampler = m_Device->createSampler(samplerDesc);

    m_ForwardViewCB = m_Device->createBuffer(nvrhi::utils::CreateConstantBufferDesc(sizeof(ForwardShadingViewConstants), "ForwardShadingViewConstants"));
    m_ForwardLightCB = m_Device->createBuffer(nvrhi::utils::CreateConstantBufferDesc(sizeof(ForwardShadingLightConstants), "ForwardShadingLightConstants"));

    m_ViewBindingLayout = CreateViewBindingLayout();
    m_ViewBindingSet = CreateViewBindingSet();
    m_LightBindingLayout = CreateLightBindingLayout();

    m_PsoOpaque = CreateGraphicsPipeline(MD_OPAQUE, sampleView, sampleFramebuffer, params);
    m_PsoAlphaTested = CreateGraphicsPipeline(MD_ALPHA_TESTED, sampleView, sampleFramebuffer, params);
    m_PsoTransparent = CreateGraphicsPipeline(MD_TRANSPARENT, sampleView, sampleFramebuffer, params);
}

void ForwardShadingPass::ResetBindingCache()
{
    m_MaterialBindings->Clear();
    m_LightBindingSets.clear();
    m_CurrentLightBindingSet = nullptr;
}

#ifdef _WIN32
static NV_CUSTOM_SEMANTIC g_CustomSemantics[] = {
    { NV_CUSTOM_SEMANTIC_VERSION, NV_X_RIGHT_SEMANTIC, "NV_X_RIGHT", false, 0, 0, 0 },
    { NV_CUSTOM_SEMANTIC_VERSION, NV_VIEWPORT_MASK_SEMANTIC, "NV_VIEWPORT_MASK", false, 0, 0, 0 }
};
#endif // _WIN32

nvrhi::ShaderHandle ForwardShadingPass::CreateVertexShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    if (sampleView->IsStereoView())
    {
        std::vector<ShaderMacro> Macros;
        Macros.push_back(ShaderMacro("SINGLE_PASS_STEREO", "1"));

        nvrhi::ShaderDesc shaderDesc(nvrhi::ShaderType::SHADER_VERTEX);

#ifdef _WIN32
        shaderDesc.numCustomSemantics = dim(g_CustomSemantics);
        shaderDesc.pCustomSemantics = g_CustomSemantics;
#else // _WIN32
		assert(!"check how custom semantics should work without nvapi / on vk");
#endif // _WIN32

        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/forward_vs.hlsl", "main", &Macros, shaderDesc);
    }
    else
    {
        std::vector<ShaderMacro> Macros;
        Macros.push_back(ShaderMacro("SINGLE_PASS_STEREO", "0"));

        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/forward_vs.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_VERTEX);
    }

}

nvrhi::ShaderHandle ForwardShadingPass::CreateGeometryShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    if (sampleView->IsStereoView())
    {
        std::vector<ShaderMacro> GeometryShaderMacros;
        GeometryShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", "0"));

        nvrhi::ShaderDesc geometryShaderDesc(nvrhi::ShaderType::SHADER_GEOMETRY);
        geometryShaderDesc.fastGSFlags = nvrhi::FastGeometryShaderFlags::FORCE_FAST_GS;
        
#ifdef _WIN32
        geometryShaderDesc.numCustomSemantics = dim(g_CustomSemantics);
        geometryShaderDesc.pCustomSemantics = g_CustomSemantics;
#else // _WIN32
		assert(!"check how custom semantics should work without nvapi / on vk");
#endif // _WIN32

        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/forward_gs.hlsl", "main", &GeometryShaderMacros, geometryShaderDesc);
    }
    else if (sampleView->IsCubemapView())
    {
        nvrhi::ShaderDesc desc(nvrhi::ShaderType::SHADER_GEOMETRY);
        desc.fastGSFlags = nvrhi::FastGeometryShaderFlags::Enum(
            nvrhi::FastGeometryShaderFlags::FORCE_FAST_GS |
            nvrhi::FastGeometryShaderFlags::USE_VIEWPORT_MASK |
            nvrhi::FastGeometryShaderFlags::OFFSET_RT_INDEX_BY_VP_INDEX);

        desc.pCoordinateSwizzling = CubemapView::GetCubemapCoordinateSwizzle();

        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/cubemap_gs.hlsl", "main", nullptr, desc);
    }
    else
    {
        return nullptr;
    }
}

nvrhi::ShaderHandle ForwardShadingPass::CreatePixelShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    std::vector<ShaderMacro> Macros;
    Macros.push_back(ShaderMacro("SINGLE_PASS_STEREO", sampleView->IsStereoView() ? "1" : "0"));

    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/forward_ps.hlsl", "main", &Macros, nvrhi::ShaderType::SHADER_PIXEL);
}

nvrhi::InputLayoutHandle ForwardShadingPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    std::vector<nvrhi::VertexAttributeDesc> inputDescs =
    {
        VertexAttribute::GetAttributeDesc(VertexAttribute::POSITION, "POS", 0),
        VertexAttribute::GetAttributeDesc(VertexAttribute::TEXCOORD1, "UV", 1),
        VertexAttribute::GetAttributeDesc(VertexAttribute::NORMAL, "NORMAL", 2),
        VertexAttribute::GetAttributeDesc(VertexAttribute::TANGENT, "TANGENT", 3),
        VertexAttribute::GetAttributeDesc(VertexAttribute::BITANGENT, "BITANGENT", 4),
        VertexAttribute::GetAttributeDesc(VertexAttribute::TRANSFORM, "TRANSFORM", 5),
    };

    return m_Device->createInputLayout(inputDescs.data(), static_cast<uint32_t>(inputDescs.size()), vertexShader);
}

nvrhi::BindingLayoutHandle ForwardShadingPass::CreateViewBindingLayout()
{
    nvrhi::BindingLayoutDesc viewLayoutDesc;
    viewLayoutDesc.VS = {
        { 0, nvrhi::ResourceType::VolatileConstantBuffer }
    };
    viewLayoutDesc.PS = {
        { 1, nvrhi::ResourceType::VolatileConstantBuffer },
        { 2, nvrhi::ResourceType::VolatileConstantBuffer },
        { 1, nvrhi::ResourceType::Sampler }
    };

    return m_Device->createBindingLayout(viewLayoutDesc);
}


nvrhi::BindingSetHandle ForwardShadingPass::CreateViewBindingSet()
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.VS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ForwardViewCB)
    };
    bindingSetDesc.PS = {
        nvrhi::BindingSetItem::ConstantBuffer(1, m_ForwardViewCB),
        nvrhi::BindingSetItem::ConstantBuffer(2, m_ForwardLightCB),
        nvrhi::BindingSetItem::Sampler(1, m_ShadowSampler)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    return m_Device->createBindingSet(bindingSetDesc, m_ViewBindingLayout);
}

nvrhi::BindingLayoutHandle ForwardShadingPass::CreateLightBindingLayout()
{
    nvrhi::BindingLayoutDesc lightProbeBindingDesc;
    lightProbeBindingDesc.PS = {
        { 4, nvrhi::ResourceType::Texture_SRV },
        { 5, nvrhi::ResourceType::Texture_SRV },
        { 6, nvrhi::ResourceType::Texture_SRV },
        { 7, nvrhi::ResourceType::Texture_SRV },
        { 2, nvrhi::ResourceType::Sampler },
        { 3, nvrhi::ResourceType::Sampler }
    };

    return m_Device->createBindingLayout(lightProbeBindingDesc);
}

nvrhi::BindingSetHandle ForwardShadingPass::CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.PS = {
        nvrhi::BindingSetItem::Texture_SRV(4, shadowMapTexture ? shadowMapTexture : m_CommonPasses->m_BlackTexture2DArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(5, diffuse ? diffuse : m_CommonPasses->m_BlackCubeMapArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(6, specular ? specular : m_CommonPasses->m_BlackCubeMapArray.Get()),
        nvrhi::BindingSetItem::Texture_SRV(7, environmentBrdf ? environmentBrdf : m_CommonPasses->m_BlackTexture.Get()),
        nvrhi::BindingSetItem::Sampler(2, m_CommonPasses->m_LinearWrapSampler),
        nvrhi::BindingSetItem::Sampler(3, m_CommonPasses->m_LinearClampSampler)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    return m_Device->createBindingSet(bindingSetDesc, m_LightBindingLayout);
}

nvrhi::GraphicsPipelineHandle ForwardShadingPass::CreateGraphicsPipeline(MaterialDomain domain, const IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.GS = m_GeometryShader;
    pipelineDesc.PS = m_PixelShader;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_BACK;
    pipelineDesc.renderState.blendState.alphaToCoverage = false;
    pipelineDesc.bindingLayouts = { m_MaterialBindings->GetLayout(), m_ViewBindingLayout, m_LightBindingLayout };

    pipelineDesc.renderState.depthStencilState.depthFunc = sampleView->IsReverseDepth()
        ? nvrhi::DepthStencilState::COMPARISON_GREATER_EQUAL
        : nvrhi::DepthStencilState::COMPARISON_LESS_EQUAL;

    if (sampleView->IsStereoView())
    {
        pipelineDesc.renderState.singlePassStereo.enabled = true;
        pipelineDesc.renderState.singlePassStereo.independentViewportMask = true;
        pipelineDesc.renderState.singlePassStereo.renderTargetIndexOffset = 0;
    }

    switch (domain)
    {
    case MD_ALPHA_TESTED:
        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.blendState.alphaToCoverage = true;
        break;

    case MD_TRANSPARENT:
        pipelineDesc.renderState.blendState.alphaToCoverage = false;
        pipelineDesc.renderState.blendState.blendEnable[0] = true;
        pipelineDesc.renderState.blendState.blendOp[0] = nvrhi::BlendState::BLEND_OP_ADD;
        pipelineDesc.renderState.blendState.srcBlend[0] = nvrhi::BlendState::BLEND_ONE;
        pipelineDesc.renderState.blendState.destBlend[0] = nvrhi::BlendState::BLEND_INV_SRC_ALPHA;
        pipelineDesc.renderState.blendState.srcBlendAlpha[0] = nvrhi::BlendState::BLEND_ZERO;
        pipelineDesc.renderState.blendState.destBlendAlpha[0] = nvrhi::BlendState::BLEND_ONE;
        pipelineDesc.renderState.depthStencilState.depthWriteMask = nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ZERO;
        break;

    case MD_OPAQUE:
    default:
        break;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
}

std::shared_ptr<MaterialBindingCache> ForwardShadingPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
{
    std::vector<MaterialResourceBinding> materialBindings = {
        { MaterialResource::CONSTANT_BUFFER, 0, 0 },
        { MaterialResource::DIFFUSE_TEXTURE, 0, 0 },
        { MaterialResource::SPECULAR_TEXTURE, 1, 0 },
        { MaterialResource::NORMALS_TEXTURE, 2, 0 },
        { MaterialResource::EMISSIVE_TEXTURE, 3, 0 },
        { MaterialResource::SAMPLER, 0, 0 },
    };

    return std::make_shared<MaterialBindingCache>(
        m_Device,
        nvrhi::ShaderType::SHADER_PIXEL,
        materialBindings,
        commonPasses.m_AnisotropicWrapSampler,
        commonPasses.m_GrayTexture,
        commonPasses.m_BlackTexture);
}

void ForwardShadingPass::PrepareForView(
    nvrhi::ICommandList* commandList, 
    const IView* view, 
    const IView* viewPrev)
{
    ForwardShadingViewConstants viewConstants = {};
    if (view->IsStereoView())
    {
        view->GetChildView(ViewType::PLANAR, 0)->FillPlanarViewConstants(viewConstants.leftView);
        view->GetChildView(ViewType::PLANAR, 1)->FillPlanarViewConstants(viewConstants.rightView);
    }
    else
    {
        view->FillPlanarViewConstants(viewConstants.leftView);
    }
    commandList->writeBuffer(m_ForwardViewCB, &viewConstants, sizeof(viewConstants));
}

void ForwardShadingPass::PrepareLights(
    nvrhi::ICommandList* commandList,
    const std::vector<std::shared_ptr<Light>>& lights,
    dm::float3 ambientColorTop,
    dm::float3 ambientColorBottom,
    const std::vector<std::shared_ptr<LightProbe>>& lightProbes)
{
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
                log::error("All lights probe submitted to ForwardShadingPass::PrepareLights(...) must use the same set of textures");
                return;
            }
        }
    }

    nvrhi::BindingSetHandle& lightBindings = m_LightBindingSets[std::make_pair(shadowMapTexture, lightProbeDiffuse)];

    if (!lightBindings)
    {
        lightBindings = CreateLightBindingSet(shadowMapTexture, lightProbeDiffuse, lightProbeSpecular, lightProbeEnvironmentBrdf);
    }

    m_CurrentLightBindingSet = lightBindings;


    ForwardShadingLightConstants constants = {};

    constants.shadowMapTextureSize = float2(shadowMapTextureSize);

    int numShadows = 0;

    for (int nLight = 0; nLight < std::min(static_cast<int>(lights.size()), FORWARD_MAX_LIGHTS); nLight++)
    {
        const auto& light = lights[nLight];

        LightConstants& lightConstants = constants.lights[constants.numLights];
        light->FillLightConstants(lightConstants);

        if (light->shadowMap)
        {
            for (uint32_t cascade = 0; cascade < light->shadowMap->GetNumberOfCascades(); cascade++)
            {
                if (numShadows < FORWARD_MAX_SHADOWS)
                {
                    light->shadowMap->GetCascade(cascade)->FillShadowConstants(constants.shadows[numShadows]);
                    lightConstants.shadowCascades[cascade] = numShadows;
                    ++numShadows;
                }
            }

            for (uint32_t perObjectShadow = 0; perObjectShadow < light->shadowMap->GetNumberOfPerObjectShadows(); perObjectShadow++)
            {
                if (numShadows < FORWARD_MAX_SHADOWS)
                {
                    light->shadowMap->GetPerObjectShadow(perObjectShadow)->FillShadowConstants(constants.shadows[numShadows]);
                    lightConstants.perObjectShadows[perObjectShadow] = numShadows;
                    ++numShadows;
                }
            }
        }

        ++constants.numLights;
    }

    constants.ambientColorTop = float4(ambientColorTop, 0.f);
    constants.ambientColorBottom = float4(ambientColorBottom, 0.f);

    for (auto probe : lightProbes)
    {
        if (!probe->IsActive())
            continue;

        LightProbeConstants& lightProbeConstants = constants.lightProbes[constants.numLightProbes];
        probe->FillLightProbeConstants(lightProbeConstants);

        ++constants.numLightProbes;

        if (constants.numLightProbes >= FORWARD_MAX_LIGHT_PROBES)
            break;
    }

    commandList->writeBuffer(m_ForwardLightCB, &constants, sizeof(constants));
}

ViewType::Enum ForwardShadingPass::GetSupportedViewTypes() const
{
    return m_SupportedViewTypes;
}

bool ForwardShadingPass::SetupMaterial(const Material* material, nvrhi::GraphicsState& state)
{
    nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

    if (!materialBindingSet)
        return false;

    switch (material->domain)
    {
    case MD_OPAQUE:
        state.pipeline = m_PsoOpaque;
        break;
    case MD_ALPHA_TESTED:
        state.pipeline = m_PsoAlphaTested;
        break;
    case MD_TRANSPARENT:
        state.pipeline = m_PsoTransparent;
        break;
    default:
        return false;
    }

    state.bindings = { materialBindingSet, m_ViewBindingSet, m_CurrentLightBindingSet };

    return true;
}

void ForwardShadingPass::SetupInputBuffers(const BufferGroup* buffers, nvrhi::GraphicsState& state)
{
    state.vertexBuffers = {
    { buffers->vertexBuffers.at(VertexAttribute::POSITION), 0, 0 },
    { buffers->vertexBuffers.at(VertexAttribute::TEXCOORD1), 1, 0 },
    { buffers->vertexBuffers.at(VertexAttribute::NORMAL), 2, 0 },
    { buffers->vertexBuffers.at(VertexAttribute::TANGENT), 3, 0 },
    { buffers->vertexBuffers.at(VertexAttribute::BITANGENT), 4, 0 },
    { buffers->vertexBuffers.at(VertexAttribute::TRANSFORM), 5, 0 }
    };

    state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}
