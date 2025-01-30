/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/render/GBufferFillPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <utility>

#if DONUT_WITH_STATIC_SHADERS
#if DONUT_WITH_DX11
#include "compiled_shaders/passes/cubemap_gs.dxbc.h"
#include "compiled_shaders/passes/gbuffer_ps.dxbc.h"
#include "compiled_shaders/passes/gbuffer_vs_input_assembler.dxbc.h"
#include "compiled_shaders/passes/gbuffer_vs_buffer_loads.dxbc.h"
#include "compiled_shaders/passes/material_id_ps.dxbc.h"
#endif
#if DONUT_WITH_DX12
#include "compiled_shaders/passes/cubemap_gs.dxil.h"
#include "compiled_shaders/passes/gbuffer_ps.dxil.h"
#include "compiled_shaders/passes/gbuffer_vs_input_assembler.dxil.h"
#include "compiled_shaders/passes/gbuffer_vs_buffer_loads.dxil.h"
#include "compiled_shaders/passes/material_id_ps.dxil.h"
#endif
#if DONUT_WITH_VULKAN
#include "compiled_shaders/passes/cubemap_gs.spirv.h"
#include "compiled_shaders/passes/gbuffer_ps.spirv.h"
#include "compiled_shaders/passes/gbuffer_vs_input_assembler.spirv.h"
#include "compiled_shaders/passes/gbuffer_vs_buffer_loads.spirv.h"
#include "compiled_shaders/passes/material_id_ps.spirv.h"
#endif
#endif

using namespace donut::math;
#include <donut/shaders/gbuffer_cb.h>

using namespace donut::engine;
using namespace donut::render;

GBufferFillPass::GBufferFillPass(nvrhi::IDevice* device, std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_CommonPasses(std::move(commonPasses))
{
    m_IsDX11 = m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11;
}

void GBufferFillPass::Init(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    m_EnableMotionVectors = params.enableMotionVectors;
    m_UseInputAssembler = params.useInputAssembler;

    m_SupportedViewTypes = ViewType::PLANAR;
    if (params.enableSinglePassCubemap)
        m_SupportedViewTypes = ViewType::Enum(m_SupportedViewTypes | ViewType::CUBEMAP);
    
    m_VertexShader = CreateVertexShader(shaderFactory, params);
    m_InputLayout = CreateInputLayout(m_VertexShader, params);
    m_GeometryShader = CreateGeometryShader(shaderFactory, params);
    m_PixelShader = CreatePixelShader(shaderFactory, params, false);
    m_PixelShaderAlphaTested = CreatePixelShader(shaderFactory, params, true);

    if (params.materialBindings)
        m_MaterialBindings = params.materialBindings;
    else
        m_MaterialBindings = CreateMaterialBindingCache(*m_CommonPasses);

    m_GBufferCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferFillConstants),
        "GBufferFillConstants", params.numConstantBufferVersions));

    CreateViewBindings(m_ViewBindingLayout, m_ViewBindings, params);

    m_EnableDepthWrite = params.enableDepthWrite;
    m_StencilWriteMask = params.stencilWriteMask;

    m_InputBindingLayout = CreateInputBindingLayout();
}

void GBufferFillPass::ResetBindingCache()
{
    m_MaterialBindings->Clear();
    m_InputBindingSets.clear();
}

nvrhi::ShaderHandle GBufferFillPass::CreateVertexShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{
    char const* sourceFileName = "donut/passes/gbuffer_vs.hlsl";

    std::vector<ShaderMacro> VertexShaderMacros;
    VertexShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));

    if (params.useInputAssembler)
    {
        return shaderFactory.CreateAutoShader(sourceFileName, "input_assembler",
            DONUT_MAKE_PLATFORM_SHADER(g_gbuffer_vs_input_assembler), &VertexShaderMacros, nvrhi::ShaderType::Vertex);
    }
    else
    {
        return shaderFactory.CreateAutoShader(sourceFileName, "buffer_loads",
            DONUT_MAKE_PLATFORM_SHADER(g_gbuffer_vs_buffer_loads), &VertexShaderMacros, nvrhi::ShaderType::Vertex);
    }
}

nvrhi::ShaderHandle GBufferFillPass::CreateGeometryShader(ShaderFactory& shaderFactory, const CreateParameters& params)
{

    ShaderMacro MotionVectorsMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0");

    if (params.enableSinglePassCubemap)
    {
        // MVs will not work with cubemap views because:
        // 1. cubemap_gs does not pass through the previous position attribute;
        // 2. Computing correct MVs for a cubemap is complicated and not implemented.
        assert(!params.enableMotionVectors);

        nvrhi::ShaderDesc desc(nvrhi::ShaderType::Geometry);
        desc.fastGSFlags = nvrhi::FastGeometryShaderFlags(
            nvrhi::FastGeometryShaderFlags::ForceFastGS |
            nvrhi::FastGeometryShaderFlags::UseViewportMask |
            nvrhi::FastGeometryShaderFlags::OffsetTargetIndexByViewportIndex);

        desc.pCoordinateSwizzling = CubemapView::GetCubemapCoordinateSwizzle();

        return shaderFactory.CreateAutoShader("donut/passes/cubemap_gs.hlsl", "main", DONUT_MAKE_PLATFORM_SHADER(g_cubemap_gs), nullptr, desc);
    }
    else
    {
        return nullptr;
    }
}

nvrhi::ShaderHandle GBufferFillPass::CreatePixelShader(ShaderFactory& shaderFactory, const CreateParameters& params, bool alphaTested)
{
    std::vector<ShaderMacro> PixelShaderMacros;
    PixelShaderMacros.push_back(ShaderMacro("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0"));
    PixelShaderMacros.push_back(ShaderMacro("ALPHA_TESTED", alphaTested ? "1" : "0"));

    return shaderFactory.CreateAutoShader("donut/passes/gbuffer_ps.hlsl", "main", DONUT_MAKE_PLATFORM_SHADER(g_gbuffer_ps), &PixelShaderMacros, nvrhi::ShaderType::Pixel);
}

nvrhi::InputLayoutHandle GBufferFillPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
    if (params.useInputAssembler)
    {
        std::vector<nvrhi::VertexAttributeDesc> inputDescs =
        {
            GetVertexAttributeDesc(VertexAttribute::Position, "POS", 0),
            GetVertexAttributeDesc(VertexAttribute::PrevPosition, "PREV_POS", 1),
            GetVertexAttributeDesc(VertexAttribute::TexCoord1, "TEXCOORD", 2),
            GetVertexAttributeDesc(VertexAttribute::Normal, "NORMAL", 3),
            GetVertexAttributeDesc(VertexAttribute::Tangent, "TANGENT", 4),
            GetVertexAttributeDesc(VertexAttribute::Transform, "TRANSFORM", 5),
        };
        if (params.enableMotionVectors)
        {
            inputDescs.push_back(GetVertexAttributeDesc(VertexAttribute::PrevTransform, "PREV_TRANSFORM", 5));
        }

        return m_Device->createInputLayout(inputDescs.data(), static_cast<uint32_t>(inputDescs.size()), vertexShader);
    }

    return nullptr;
}

void GBufferFillPass::CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params)
{
    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
        .setRegisterSpace(m_IsDX11 ? 0 : GBUFFER_SPACE_VIEW)
        .setRegisterSpaceIsDescriptorSet(!m_IsDX11)
        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(GBUFFER_BINDING_VIEW_CONSTANTS))
        .addItem(nvrhi::BindingLayoutItem::Sampler(GBUFFER_BINDING_MATERIAL_SAMPLER));

    layout = m_Device->createBindingLayout(bindingLayoutDesc);

    auto bindingSetDesc = nvrhi::BindingSetDesc()
        .setTrackLiveness(params.trackLiveness)
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(GBUFFER_BINDING_VIEW_CONSTANTS, m_GBufferCB))
        .addItem(nvrhi::BindingSetItem::Sampler(GBUFFER_BINDING_MATERIAL_SAMPLER,
            m_CommonPasses->m_AnisotropicWrapSampler));

    set = m_Device->createBindingSet(bindingSetDesc, layout);
}

nvrhi::GraphicsPipelineHandle GBufferFillPass::CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* sampleFramebuffer)
{
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.inputLayout = m_InputLayout;
    pipelineDesc.VS = m_VertexShader;
    pipelineDesc.GS = m_GeometryShader;
    pipelineDesc.renderState.rasterState
        .setFrontCounterClockwise(key.bits.frontCounterClockwise)
        .setCullMode(key.bits.cullMode);
    pipelineDesc.renderState.blendState.disableAlphaToCoverage();
    pipelineDesc.bindingLayouts = { m_MaterialBindings->GetLayout(), m_ViewBindingLayout };
    if (!m_UseInputAssembler)
        pipelineDesc.bindingLayouts.push_back(m_InputBindingLayout);

    pipelineDesc.renderState.depthStencilState
        .setDepthWriteEnable(m_EnableDepthWrite)
        .setDepthFunc(key.bits.reverseDepth
            ? nvrhi::ComparisonFunc::GreaterOrEqual
            : nvrhi::ComparisonFunc::LessOrEqual);
        
    if (m_StencilWriteMask)
    {
        pipelineDesc.renderState.depthStencilState
            .enableStencil()
            .setStencilReadMask(0)
            .setStencilWriteMask(uint8_t(m_StencilWriteMask))
            .setStencilRefValue(uint8_t(m_StencilWriteMask))
            .setFrontFaceStencil(nvrhi::DepthStencilState::StencilOpDesc().setPassOp(nvrhi::StencilOp::Replace))
            .setBackFaceStencil(nvrhi::DepthStencilState::StencilOpDesc().setPassOp(nvrhi::StencilOp::Replace));
    }

    if (key.bits.alphaTested)
    {
        pipelineDesc.renderState.rasterState.setCullNone();

        if (m_PixelShaderAlphaTested)
        {
            pipelineDesc.PS = m_PixelShaderAlphaTested;
        }
        else
        {
            pipelineDesc.PS = m_PixelShader;
            pipelineDesc.renderState.blendState.alphaToCoverageEnable = true;
        }
    }
    else
    {
        pipelineDesc.PS = m_PixelShader;
    }

    return m_Device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
}

std::shared_ptr<MaterialBindingCache> GBufferFillPass::CreateMaterialBindingCache(CommonRenderPasses& commonPasses)
{
    std::vector<MaterialResourceBinding> materialBindings = {
        { MaterialResource::ConstantBuffer,         GBUFFER_BINDING_MATERIAL_CONSTANTS },
        { MaterialResource::DiffuseTexture,         GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE },
        { MaterialResource::SpecularTexture,        GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE },
        { MaterialResource::NormalTexture,          GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE },
        { MaterialResource::EmissiveTexture,        GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE },
        { MaterialResource::OcclusionTexture,       GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE },
        { MaterialResource::TransmissionTexture,    GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE },
        { MaterialResource::OpacityTexture,         GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE }
    };

    return std::make_shared<MaterialBindingCache>(
        m_Device,
        nvrhi::ShaderType::Pixel,
        /* registerSpace = */ m_IsDX11 ? 0 : GBUFFER_SPACE_MATERIAL,
        /* registerSpaceIsDescriptorSet = */ !m_IsDX11,
        materialBindings,
        commonPasses.m_AnisotropicWrapSampler,
        commonPasses.m_GrayTexture,
        commonPasses.m_BlackTexture);
}

ViewType::Enum GBufferFillPass::GetSupportedViewTypes() const
{
    return m_SupportedViewTypes;
}

void GBufferFillPass::SetupView(GeometryPassContext& abstractContext, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    GBufferFillConstants gbufferConstants = {};
    view->FillPlanarViewConstants(gbufferConstants.view);
    viewPrev->FillPlanarViewConstants(gbufferConstants.viewPrev);
    commandList->writeBuffer(m_GBufferCB, &gbufferConstants, sizeof(gbufferConstants));

    context.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
    context.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

bool GBufferFillPass::SetupMaterial(GeometryPassContext& abstractContext, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
    auto& context = static_cast<Context&>(abstractContext);
    
    PipelineKey key = context.keyTemplate;
    key.bits.cullMode = cullMode;

    switch (material->domain)
    {
    case MaterialDomain::Opaque:
    case MaterialDomain::AlphaBlended: // Blended and transmissive domains are for the material ID pass, shouldn't be used otherwise
    case MaterialDomain::Transmissive:
    case MaterialDomain::TransmissiveAlphaTested:
    case MaterialDomain::TransmissiveAlphaBlended:
        key.bits.alphaTested = false;
        break;
    case MaterialDomain::AlphaTested:
        key.bits.alphaTested = true;
        break;
    default:
        return false;
    }

    nvrhi::IBindingSet* materialBindingSet = m_MaterialBindings->GetMaterialBindingSet(material);

    if (!materialBindingSet)
        return false;

    nvrhi::GraphicsPipelineHandle& pipeline = m_Pipelines[key.value];

    if (!pipeline)
    {
        std::lock_guard<std::mutex> lockGuard(m_Mutex);

        if (!pipeline)
            pipeline = CreateGraphicsPipeline(key, state.framebuffer);

        if (!pipeline)
            return false;
    }

    assert(pipeline->getFramebufferInfo() == state.framebuffer->getFramebufferInfo());

    state.pipeline = pipeline;
    state.bindings = { materialBindingSet, m_ViewBindings };
    
    if (!m_UseInputAssembler)
        state.bindings.push_back(context.inputBindingSet);

    return true;
}

void GBufferFillPass::SetupInputBuffers(GeometryPassContext& abstractContext, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state)
{
    auto& context = static_cast<Context&>(abstractContext);

    state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };

    if (m_UseInputAssembler)
    {
        state.vertexBuffers = {
            { buffers->vertexBuffer, 0, buffers->getVertexBufferRange(VertexAttribute::Position).byteOffset },
            { buffers->vertexBuffer, 1, buffers->getVertexBufferRange(VertexAttribute::PrevPosition).byteOffset },
            { buffers->vertexBuffer, 2, buffers->getVertexBufferRange(VertexAttribute::TexCoord1).byteOffset },
            { buffers->vertexBuffer, 3, buffers->getVertexBufferRange(VertexAttribute::Normal).byteOffset },
            { buffers->vertexBuffer, 4, buffers->getVertexBufferRange(VertexAttribute::Tangent).byteOffset },
            { buffers->instanceBuffer, 5, 0 }
        };
    }
    else
    {
        context.inputBindingSet = GetOrCreateInputBindingSet(buffers);
        context.positionOffset = uint32_t(buffers->getVertexBufferRange(VertexAttribute::Position).byteOffset);
        context.prevPositionOffset = uint32_t(buffers->getVertexBufferRange(VertexAttribute::PrevPosition).byteOffset);
        context.texCoordOffset = uint32_t(buffers->getVertexBufferRange(VertexAttribute::TexCoord1).byteOffset);
        context.normalOffset = uint32_t(buffers->getVertexBufferRange(VertexAttribute::Normal).byteOffset);
        context.tangentOffset = uint32_t(buffers->getVertexBufferRange(VertexAttribute::Tangent).byteOffset);
    }
}

nvrhi::BindingLayoutHandle GBufferFillPass::CreateInputBindingLayout()
{
    if (m_UseInputAssembler)
        return nullptr;

    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
        .setRegisterSpace(m_IsDX11 ? 0 : GBUFFER_SPACE_INPUT)
        .setRegisterSpaceIsDescriptorSet(!m_IsDX11)
        .addItem(m_IsDX11
            ? nvrhi::BindingLayoutItem::RawBuffer_SRV(GBUFFER_BINDING_INSTANCE_BUFFER)
            : nvrhi::BindingLayoutItem::StructuredBuffer_SRV(GBUFFER_BINDING_INSTANCE_BUFFER))
        .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(GBUFFER_BINDING_VERTEX_BUFFER))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(GBUFFER_BINDING_PUSH_CONSTANTS, sizeof(GBufferPushConstants)));
        
    return m_Device->createBindingLayout(bindingLayoutDesc);
}

nvrhi::BindingSetHandle GBufferFillPass::CreateInputBindingSet(const BufferGroup* bufferGroup)
{
    auto bindingSetDesc = nvrhi::BindingSetDesc()
        .addItem(m_IsDX11
            ? nvrhi::BindingSetItem::RawBuffer_SRV(GBUFFER_BINDING_INSTANCE_BUFFER, bufferGroup->instanceBuffer)
            : nvrhi::BindingSetItem::StructuredBuffer_SRV(GBUFFER_BINDING_INSTANCE_BUFFER, bufferGroup->instanceBuffer))
        .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(GBUFFER_BINDING_VERTEX_BUFFER, bufferGroup->vertexBuffer))
        .addItem(nvrhi::BindingSetItem::PushConstants(GBUFFER_BINDING_PUSH_CONSTANTS, sizeof(GBufferPushConstants)));

    return m_Device->createBindingSet(bindingSetDesc, m_InputBindingLayout);
}

nvrhi::BindingSetHandle GBufferFillPass::GetOrCreateInputBindingSet(const BufferGroup* bufferGroup)
{
    auto it = m_InputBindingSets.find(bufferGroup);
    if (it == m_InputBindingSets.end())
    {
        auto bindingSet = CreateInputBindingSet(bufferGroup);
        m_InputBindingSets[bufferGroup] = bindingSet;
        return bindingSet;
    }

    return it->second;
}

void GBufferFillPass::SetPushConstants(
    donut::render::GeometryPassContext& abstractContext,
    nvrhi::ICommandList* commandList,
    nvrhi::GraphicsState& state,
    nvrhi::DrawArguments& args)
{
    if (m_UseInputAssembler)
        return;
        
    auto& context = static_cast<Context&>(abstractContext);

    GBufferPushConstants constants;
    constants.startInstanceLocation = args.startInstanceLocation;
    constants.startVertexLocation = args.startVertexLocation;
    constants.positionOffset = context.positionOffset;
    constants.prevPositionOffset = context.prevPositionOffset;
    constants.texCoordOffset = context.texCoordOffset;
    constants.normalOffset = context.normalOffset;
    constants.tangentOffset = context.tangentOffset;

    commandList->setPushConstants(&constants, sizeof(constants));

    args.startInstanceLocation = 0;
    args.startVertexLocation = 0;
}

void MaterialIDPass::Init(
    engine::ShaderFactory& shaderFactory,
    const CreateParameters& params)
{
    CreateParameters paramsCopy = params;
    // The material ID pass relies on the push constants filled by the buffer load path (firstInstance)
    paramsCopy.useInputAssembler = false;
    // The material ID pass doesn't support generating motion vectors
    paramsCopy.enableMotionVectors = false;

    GBufferFillPass::Init(shaderFactory, paramsCopy);
}

nvrhi::ShaderHandle MaterialIDPass::CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params, bool alphaTested)
{
    std::vector<ShaderMacro> PixelShaderMacros;
    PixelShaderMacros.push_back(ShaderMacro("ALPHA_TESTED", alphaTested ? "1" : "0"));

    return shaderFactory.CreateAutoShader("donut/passes/material_id_ps.hlsl", "main",
        DONUT_MAKE_PLATFORM_SHADER(g_material_id_ps), &PixelShaderMacros, nvrhi::ShaderType::Pixel);
}
