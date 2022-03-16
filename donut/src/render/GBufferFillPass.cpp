
#include <donut/render/GBufferFillPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/MaterialBindingCache.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include <donut/shaders/gbuffer_cb.h>

#include <sstream>

#ifdef _WIN32
#include <d3d11.h> // for nvapi.h to declare the custom semantic stuff
#include <nvapi.h>
#endif // _WIN32

using namespace donut::engine;
using namespace donut::render;

GBufferFillPass::GBufferFillPass(nvrhi::IDevice* device, std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(commonPasses)
{

}

void GBufferFillPass::Init(ShaderFactory& shaderFactory, FramebufferFactory& framebufferFactory, const ICompositeView& compositeView, const CreateParameters& params)
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
    m_PixelShader = CreatePixelShader(shaderFactory, sampleView, params, MD_OPAQUE);
    m_PixelShaderAlphaTested = CreatePixelShader(shaderFactory, sampleView, params, MD_ALPHA_TESTED);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    m_GBufferCB = m_Device->createBuffer(nvrhi::utils::CreateConstantBufferDesc(sizeof(GBufferFillConstants), "GBufferFillConstants"));

    CreateViewBindings(m_ViewBindingLayout, m_ViewBindings, params);

    m_PsoOpaque = CreateGraphicsPipeline(MD_OPAQUE, sampleView, sampleFramebuffer, params);
    m_PsoAlphaTested = CreateGraphicsPipeline(MD_ALPHA_TESTED, sampleView, sampleFramebuffer, params);
}

void GBufferFillPass::ResetBindingCache()
{
    m_MaterialBindings->Clear();
}

#ifdef _WIN32
static NV_CUSTOM_SEMANTIC g_CustomSemantics[] = {
    { NV_CUSTOM_SEMANTIC_VERSION, NV_X_RIGHT_SEMANTIC, "NV_X_RIGHT", false, 0, 0, 0 },
    { NV_CUSTOM_SEMANTIC_VERSION, NV_VIEWPORT_MASK_SEMANTIC, "NV_VIEWPORT_MASK", false, 0, 0, 0 }
};
#endif // _WIN32

nvrhi::ShaderHandle GBufferFillPass::CreateVertexShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    std::vector<ShaderMacro> VertexShaderMacros;
    VertexShaderMacros.push_back(ShaderMacro("SINGLE_PASS_STEREO", sampleView->IsStereoView() ? "1" : "0"));
    VertexShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));

    if (sampleView->IsStereoView())
    {
        nvrhi::ShaderDesc shaderDesc(nvrhi::ShaderType::SHADER_VERTEX);
#ifdef _WIN32
        shaderDesc.numCustomSemantics = dim(g_CustomSemantics);
        shaderDesc.pCustomSemantics = g_CustomSemantics;
#else // _WIN32
		assert(!"check how custom semantics should work without nvapi / on vk");
#endif // _WIN32

        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/gbuffer_vs.hlsl", "main", &VertexShaderMacros, shaderDesc);
    }
    else
    {
        return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/gbuffer_vs.hlsl", "main", &VertexShaderMacros, nvrhi::ShaderType::SHADER_VERTEX);
    }
}

nvrhi::ShaderHandle GBufferFillPass::CreateGeometryShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{

    ShaderMacro MotionVectorsMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0");

    if (sampleView->IsStereoView())
    {
        std::vector<ShaderMacro> GeometryShaderMacros;
        GeometryShaderMacros.push_back(MotionVectorsMacro);

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
    else if (params.enableSinglePassCubemap)
    {
        // MVs will not work with cubemap views because:
        // 1. cubemap_gs does not pass through the previous position attribute;
        // 2. Computing correct MVs for a cubemap is complicated and not implemented.
        assert(!params.enableMotionVectors);

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

nvrhi::ShaderHandle GBufferFillPass::CreatePixelShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params, MaterialDomain domain)
{
    if (domain != MD_OPAQUE)
        return nullptr;

    std::vector<ShaderMacro> PixelShaderMacros;
    PixelShaderMacros.push_back(ShaderMacro("SINGLE_PASS_STEREO", sampleView->IsStereoView() ? "1" : "0"));
    PixelShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));

    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/gbuffer_ps.hlsl", "main", &PixelShaderMacros, nvrhi::ShaderType::SHADER_PIXEL);
}

nvrhi::InputLayoutHandle GBufferFillPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
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
    if (params.enableMotionVectors)
    {
        inputDescs.push_back(VertexAttribute::GetAttributeDesc(VertexAttribute::PREV_TRANSFORM, "PREV_TRANSFORM", 5));
    }

    return m_Device->createInputLayout(inputDescs.data(), static_cast<uint32_t>(inputDescs.size()), vertexShader);
}

void GBufferFillPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.VS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_GBufferCB)
    };
    bindingSetDesc.PS = {
        nvrhi::BindingSetItem::ConstantBuffer(1, m_GBufferCB)
    };

    bindingSetDesc.trackLiveness = params.trackLiveness;

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, bindingSetDesc, layout, set);
}

nvrhi::GraphicsPipelineHandle GBufferFillPass::CreateGraphicsPipeline(MaterialDomain domain, const IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.GS = m_GeometryShader;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_BACK;
    pipelineDesc.renderState.blendState.alphaToCoverage = false;
    pipelineDesc.bindingLayouts = { m_MaterialBindings->GetLayout(), m_ViewBindingLayout };

    nvrhi::DepthStencilState& depthStencilState = pipelineDesc.renderState.depthStencilState;

    depthStencilState.depthWriteMask = params.enableDepthWrite
        ? nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ALL
        : nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ZERO;
    depthStencilState.depthFunc = sampleView->IsReverseDepth()
        ? nvrhi::DepthStencilState::COMPARISON_GREATER_EQUAL
        : nvrhi::DepthStencilState::COMPARISON_LESS_EQUAL;

    if (params.stencilWriteMask)
    {
        depthStencilState.stencilEnable = true;
        depthStencilState.stencilReadMask = 0;
        depthStencilState.stencilWriteMask = params.stencilWriteMask;
        depthStencilState.stencilRefValue = params.stencilWriteMask;
        depthStencilState.frontFace.stencilPassOp = nvrhi::DepthStencilState::STENCIL_OP_REPLACE;
        depthStencilState.backFace.stencilPassOp = nvrhi::DepthStencilState::STENCIL_OP_REPLACE;
    }

    if (sampleView->IsStereoView())
    {
        pipelineDesc.renderState.singlePassStereo.enabled = true;
        pipelineDesc.renderState.singlePassStereo.independentViewportMask = true;
        pipelineDesc.renderState.singlePassStereo.renderTargetIndexOffset = 0;
    }

    switch (domain)
    {
    case MD_OPAQUE:
        pipelineDesc.PS = m_PixelShader;
        break;

    case MD_ALPHA_TESTED:
        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;

        if (m_PixelShaderAlphaTested)
        {
            pipelineDesc.PS = m_PixelShaderAlphaTested;
        }
        else
        {
            pipelineDesc.PS = m_PixelShader;
            pipelineDesc.renderState.blendState.alphaToCoverage = true;
        }
        break;

    case MD_TRANSPARENT:
    default:
        return nullptr;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
}

std::shared_ptr<MaterialBindingCache> GBufferFillPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
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

ViewType::Enum GBufferFillPass::GetSupportedViewTypes() const
{
    return m_SupportedViewTypes;
}

void GBufferFillPass::PrepareForView(nvrhi::ICommandList* commandList, const IView* view, const IView* viewPrev)
{
    nvrhi::ViewportState viewportStatePrev = viewPrev->GetViewportState();

    GBufferFillConstants gbufferConstants = {};
    if (view->IsStereoView())
    {
        view->GetChildView(ViewType::PLANAR, 0)->FillPlanarViewConstants(gbufferConstants.leftView);
        view->GetChildView(ViewType::PLANAR, 1)->FillPlanarViewConstants(gbufferConstants.rightView);
        viewPrev->GetChildView(ViewType::PLANAR, 0)->FillPlanarViewConstants(gbufferConstants.leftViewPrev);
        viewPrev->GetChildView(ViewType::PLANAR, 1)->FillPlanarViewConstants(gbufferConstants.rightViewPrev);
    }
    else
    {
        view->FillPlanarViewConstants(gbufferConstants.leftView);
        viewPrev->FillPlanarViewConstants(gbufferConstants.leftViewPrev);
    }

    commandList->writeBuffer(m_GBufferCB, &gbufferConstants, sizeof(gbufferConstants));
}

bool GBufferFillPass::SetupMaterial(const Material* material, nvrhi::GraphicsState& state)
{
    switch (material->domain)
    {
    case MD_OPAQUE:
        state.pipeline = m_PsoOpaque;
        break;
    case MD_ALPHA_TESTED:
        state.pipeline = m_PsoAlphaTested;
        break;
    default:
        return false;
    }

    nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

    if (!materialBindingSet)
        return false;

    state.bindings = { materialBindingSet, m_ViewBindings };

    return true;
}

void GBufferFillPass::SetupInputBuffers(const BufferGroup* buffers, nvrhi::GraphicsState& state)
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

nvrhi::ShaderHandle donut::render::MaterialIDPass::CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params, engine::MaterialDomain domain)
{
    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/material_id_ps.hlsl", domain == MD_ALPHA_TESTED ? "main_alpha_tested" : "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
}
