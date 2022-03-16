
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/View.h>
#include <donut/engine/MaterialBindingCache.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include <donut/shaders/depth_cb.h>

using namespace donut::engine;
using namespace donut::render;

DepthPass::DepthPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_CommonPasses(commonPasses)
    , m_Device(device)
{
}

void DepthPass::Init(ShaderFactory& shaderFactory, FramebufferFactory& framebufferFactory, const ICompositeView& compositeView, const CreateParameters& params)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = framebufferFactory.GetFramebuffer(*sampleView);

    m_VertexShader = CreateVertexShader(shaderFactory, sampleView, params);
    m_PixelShader = CreatePixelShader(shaderFactory, sampleView, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    m_DepthCB = m_Device->createBuffer(nvrhi::utils::CreateConstantBufferDesc(sizeof(DepthPassConstants), "DepthPassConstants"));

    CreateViewBindings(m_ViewBindingLayout, m_ViewBindingSet, params);

    m_PsoOpaque = CreateGraphicsPipeline(MD_OPAQUE, sampleView, sampleFramebuffer, params);
    m_PsoAlphaTested = CreateGraphicsPipeline(MD_ALPHA_TESTED, sampleView, sampleFramebuffer, params);
}

void DepthPass::ResetBindingCache()
{
    m_MaterialBindings->Clear();
}

nvrhi::ShaderHandle DepthPass::CreateVertexShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/depth_vs.hlsl", "main", nullptr, nvrhi::ShaderType::SHADER_VERTEX);
}

nvrhi::ShaderHandle DepthPass::CreatePixelShader(ShaderFactory& shaderFactory, const IView* sampleView, const CreateParameters& params)
{
    return shaderFactory.CreateShader(ShaderLocation::FRAMEWORK, "passes/depth_ps.hlsl", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
}

nvrhi::InputLayoutHandle DepthPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    nvrhi::VertexAttributeDesc aInputDescs[] =
    {
        VertexAttribute::GetAttributeDesc(VertexAttribute::POSITION, "POSITION", 0),
        VertexAttribute::GetAttributeDesc(VertexAttribute::TEXCOORD1, "UV", 1),
        VertexAttribute::GetAttributeDesc(VertexAttribute::TRANSFORM, "TRANSFORM", 2)
    };

    return m_Device->createInputLayout(aInputDescs, dim(aInputDescs), vertexShader);
}

void DepthPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.VS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_DepthCB)
    };
    bindingSetDesc.trackLiveness = m_TrackLiveness;

    nvrhi::utils::CreateBindingSetAndLayout(m_Device, bindingSetDesc, layout, set);
}

std::shared_ptr<MaterialBindingCache> DepthPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
{
    std::vector<MaterialResourceBinding> materialBindings = {
        { MaterialResource::DIFFUSE_TEXTURE, 0, 0 },
        { MaterialResource::SAMPLER, 0, 0 }
    };

    return std::make_shared<MaterialBindingCache>(
        m_Device,
        nvrhi::ShaderType::SHADER_PIXEL,
        materialBindings,
        commonPasses.m_AnisotropicWrapSampler,
        commonPasses.m_GrayTexture,
        commonPasses.m_BlackTexture);
}

nvrhi::GraphicsPipelineHandle DepthPass::CreateGraphicsPipeline(MaterialDomain domain, const IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.renderState.rasterState = params.rasterState;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

    switch (domain)
    {
    case MD_OPAQUE:
        pipelineDesc.PS = nullptr;
        pipelineDesc.bindingLayouts = { m_ViewBindingLayout };
        break;

    case MD_ALPHA_TESTED:
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_ViewBindingLayout, m_MaterialBindings->GetLayout() };
        break;

    case MD_TRANSPARENT:
    default:
        return nullptr;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
}

ViewType::Enum DepthPass::GetSupportedViewTypes() const
{
    return ViewType::PLANAR;
}

void DepthPass::PrepareForView(nvrhi::ICommandList* commandList, const IView* view, const IView* viewPrev)
{
    DepthPassConstants depthConstants = {};
    depthConstants.matWorldToClip = view->GetViewProjectionMatrix();
    commandList->writeBuffer(m_DepthCB, &depthConstants, sizeof(depthConstants));
}

bool DepthPass::SetupMaterial(const Material* material, nvrhi::GraphicsState& state)
{
    if (material->domain == MD_ALPHA_TESTED && material->diffuseTexture->texture)
    {
        nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

        if (!materialBindingSet)
            return false;

        state.pipeline = m_PsoAlphaTested;
        state.bindings = { m_ViewBindingSet, materialBindingSet };
    }
    else if (material->domain == MD_TRANSPARENT)
    {
        return false;
    }
    else
    {
        state.pipeline = m_PsoOpaque;
        state.bindings = { m_ViewBindingSet };
    }

    return true;
}

void DepthPass::SetupInputBuffers(const BufferGroup* buffers, nvrhi::GraphicsState& state)
{
    state.vertexBuffers = {
        { buffers->vertexBuffers.at(VertexAttribute::POSITION), 0, 0 },
        { buffers->vertexBuffers.at(VertexAttribute::TEXCOORD1), 1, 0 },
        { buffers->vertexBuffers.at(VertexAttribute::TRANSFORM), 2, 0 }
    };

    state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}
