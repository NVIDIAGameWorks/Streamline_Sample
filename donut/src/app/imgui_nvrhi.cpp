#include <stddef.h>

#include <imgui.h>

#include <nvrhi/nvrhi.h>
#include <donut/engine/ShaderFactory.h>

#include <donut/app/imgui_nvrhi.h>

using namespace donut::engine;
using namespace donut::app;

struct VERTEX_CONSTANT_BUFFER
{
    float        mvp[4][4];
};

bool ImGui_NVRHI::createFontTexture(nvrhi::ICommandList* commandList)
{
    ImGuiIO& io = ImGui::GetIO();
    unsigned char *pixels;
    int width, height;

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    {
        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = "ImGui font texture";

        fontTexture = renderer->createTexture(desc);

        commandList->beginTrackingTextureState(fontTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);

        if (fontTexture == nullptr)
            return false;

        commandList->writeTexture(fontTexture, 0, 0, pixels, width * 4);

        commandList->endTrackingTextureState(fontTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);

        io.Fonts->TexID = fontTexture;
    }

    {
        nvrhi::SamplerDesc desc;
        desc.wrapMode[0] = nvrhi::SamplerDesc::WRAP_MODE_WRAP;
        desc.wrapMode[1] = nvrhi::SamplerDesc::WRAP_MODE_WRAP;
        desc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_WRAP;
        desc.mipBias = 0.0;
        desc.anisotropy = 1.0;
        desc.minFilter = true;
        desc.magFilter = true;
        desc.mipFilter = true;

        fontSampler = renderer->createSampler(desc);

        if (fontSampler == nullptr)
            return false;
    }

    return true;
}

bool ImGui_NVRHI::init(nvrhi::DeviceHandle renderer, std::shared_ptr<ShaderFactory> shaderFactory)
{
    this->renderer = renderer;

    m_commandList = renderer->createCommandList();

    m_commandList->open();
    
    vertexShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "imgui_vertex", "main", nullptr, nvrhi::ShaderType::SHADER_VERTEX);
    if (vertexShader == nullptr)
    {
        printf("error creating NVRHI vertex shader object\n");
        assert(0);
        return false;
    }

    pixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "imgui_pixel", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
    if (pixelShader == nullptr)
    {
        printf("error creating NVRHI pixel shader object\n");
        assert(0);
        return false;
    } 

    // create attribute layout object
    nvrhi::VertexAttributeDesc vertexAttribLayout[] = {
        { "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,pos), sizeof(ImDrawVert), false },
        { "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,uv),  sizeof(ImDrawVert), false },
        { "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert,col), sizeof(ImDrawVert), false },
    };

    shaderAttribLayout = renderer->createInputLayout(vertexAttribLayout, sizeof(vertexAttribLayout) / sizeof(vertexAttribLayout[0]), vertexShader);

    // create constant buffer
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(VERTEX_CONSTANT_BUFFER);
        desc.isConstantBuffer = true;
        desc.debugName = "IMGui constant buffer";

        constantBuffer = renderer->createBuffer(desc);
    }

    // create font texture
    if (!createFontTexture(m_commandList))
    {
        return false;
    }

    // create PSO
    {
        nvrhi::BlendState blendState;
        blendState.blendEnable[0] = true;
        blendState.srcBlend[0] = nvrhi::BlendState::BLEND_SRC_ALPHA;
        blendState.destBlend[0] = nvrhi::BlendState::BLEND_INV_SRC_ALPHA;
        blendState.blendOp[0] = nvrhi::BlendState::BLEND_OP_ADD;
        blendState.srcBlendAlpha[0] = nvrhi::BlendState::BLEND_INV_SRC_ALPHA;
        blendState.destBlendAlpha[0] = nvrhi::BlendState::BLEND_ZERO;
        blendState.blendOpAlpha[0] = nvrhi::BlendState::BLEND_OP_ADD;
        blendState.colorWriteEnable[0] = nvrhi::BlendState::COLOR_MASK_ALL;

        nvrhi::RasterState rasterState;
        rasterState.fillMode = nvrhi::RasterState::FILL_SOLID;
        rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        rasterState.scissorEnable = true;
        rasterState.depthClipEnable = true;

        nvrhi::DepthStencilState depthStencilState;
        depthStencilState.depthEnable = false;
        depthStencilState.depthWriteMask = nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ALL;
        depthStencilState.depthFunc = nvrhi::DepthStencilState::COMPARISON_ALWAYS;
        depthStencilState.stencilEnable = false;

        nvrhi::RenderState renderState;
        renderState.blendState = blendState;
        renderState.depthStencilState = depthStencilState;
        renderState.rasterState = rasterState;

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.VS = { 
            { 0, nvrhi::ResourceType::ConstantBuffer } 
        };
        layoutDesc.PS = { 
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Sampler } 
        };
        bindingLayout = renderer->createBindingLayout(layoutDesc);

        basePSODesc.primType = nvrhi::PrimitiveType::TRIANGLE_LIST;
        basePSODesc.inputLayout = shaderAttribLayout;
        basePSODesc.VS = vertexShader;
        basePSODesc.PS = pixelShader;
        basePSODesc.renderState = renderState;
        basePSODesc.bindingLayouts = { bindingLayout };
    }

    auto& io = ImGui::GetIO();
    io.RenderDrawListsFn = nullptr;
    io.Fonts->AddFontDefault();

    m_commandList->close();
    renderer->executeCommandList(m_commandList);

    return true;
}

bool ImGui_NVRHI::reallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize, size_t reallocateSize, const bool indexBuffer)
{
    if (buffer == nullptr || size_t(buffer->GetDesc().byteSize) < requiredSize)
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = uint32_t(reallocateSize);
        desc.structStride = 0;
        desc.debugName = indexBuffer ? "ImGui index buffer" : "ImGui vertex buffer";
        desc.canHaveUAVs = false;
        desc.isVertexBuffer = !indexBuffer;
        desc.isIndexBuffer = indexBuffer;
        desc.isDrawIndirectArgs = false;
        desc.disableGPUsSync = false;
        desc.isVolatile = false;

        buffer = renderer->createBuffer(desc);

        if (!buffer)
        {
            return false;
        }
    }

    return true;
}

bool ImGui_NVRHI::beginFrame(float elapsedTimeSeconds)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = elapsedTimeSeconds;
    io.MouseDrawCursor = false;

    ImGui::NewFrame();

    return true;
}

nvrhi::IGraphicsPipeline* ImGui_NVRHI::getPSO(nvrhi::IFramebuffer* fb)
{
    if (pso)
        return pso;

    pso = renderer->createGraphicsPipeline(basePSODesc, fb);
    assert(pso);

    return pso;
}

nvrhi::IBindingSet* ImGui_NVRHI::getBindingSet(nvrhi::ITexture* texture)
{
    auto iter = bindingsCache.find(texture);
    if (iter != bindingsCache.end())
    {
        return iter->second;
    }

    nvrhi::BindingSetDesc desc;

    desc.VS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, constantBuffer)
    };

    desc.PS = {
        nvrhi::BindingSetItem::Texture_SRV(0, texture),
        nvrhi::BindingSetItem::Sampler(0, fontSampler)
    };

    nvrhi::BindingSetHandle binding;
    binding = renderer->createBindingSet(desc, bindingLayout);
    assert(binding);

    bindingsCache[texture] = binding;
    return binding;
}

bool ImGui_NVRHI::updateGeometry(nvrhi::ICommandList* commandList)
{
    ImDrawData *drawData = ImGui::GetDrawData();

    // create/resize vertex and index buffers if needed
    if (!reallocateBuffer(vertexBuffer, 
        drawData->TotalVtxCount * sizeof(ImDrawVert), 
        (drawData->TotalVtxCount + 5000) * sizeof(ImDrawVert), 
        false))
    {
        return false;
    }

    if (!reallocateBuffer(indexBuffer,
        drawData->TotalIdxCount * sizeof(ImDrawIdx),
        (drawData->TotalIdxCount + 5000) * sizeof(ImDrawIdx),
        true))
    {
        return false;
    }

    vtxBuffer.resize(vertexBuffer->GetDesc().byteSize / sizeof(ImDrawVert));
    idxBuffer.resize(indexBuffer->GetDesc().byteSize / sizeof(ImDrawIdx));

    // copy and convert all vertices into a single contiguous buffer
    ImDrawVert *vtxDst = &vtxBuffer[0];
    ImDrawIdx *idxDst = &idxBuffer[0];

    for(int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList *cmdList = drawData->CmdLists[n];

        memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

        vtxDst += cmdList->VtxBuffer.Size;
        idxDst += cmdList->IdxBuffer.Size;
    }
    
    commandList->beginTrackingBufferState(vertexBuffer, nvrhi::ResourceStates::COMMON);
    commandList->beginTrackingBufferState(indexBuffer, nvrhi::ResourceStates::COMMON);
    commandList->writeBuffer(vertexBuffer, &vtxBuffer[0], vertexBuffer->GetDesc().byteSize);
    commandList->writeBuffer(indexBuffer, &idxBuffer[0], indexBuffer->GetDesc().byteSize);

    return true;
}

void ImGui_NVRHI::updateConstantBuffer(nvrhi::ICommandList* commandList)
{
    const auto& io = ImGui::GetIO();

    if (io.DisplaySize.x == lastDisplaySize.x &&
        io.DisplaySize.y == lastDisplaySize.y)
    {
        // nothing to do
        return;
    }

    // setup orthgraphic projection matrix into our constant buffer
    float mvp[4][4] =
    {
        { 2.0f/io.DisplaySize.x, 0.0f,                   0.0f, -1.0f },
        { 0.0f,                  2.0f/-io.DisplaySize.y, 0.0f,  1.0f },
        { 0.0f,                  0.0f,                  -1.0f,  0.0f },
        { 0.0f,                  0.0f,                   0.0f,  1.0f },
    };

    commandList->writeBuffer(constantBuffer, mvp, sizeof(mvp));
    lastDisplaySize = io.DisplaySize;
}

bool ImGui_NVRHI::render(nvrhi::IFramebuffer* framebuffer)
{
    ImDrawData *drawData = ImGui::GetDrawData();
    const auto& io = ImGui::GetIO();

    m_commandList->open();
    m_commandList->beginMarker("ImGUI");

    m_commandList->beginTrackingBufferState(constantBuffer, nvrhi::ResourceStates::COMMON);
    
    if (!updateGeometry(m_commandList))
    {
        return false;
    }

    // handle DPI scaling
    drawData->ScaleClipRects(io.DisplayFramebufferScale);

    updateConstantBuffer(m_commandList);

    // set up graphics state
    nvrhi::GraphicsState drawState;

    drawState.framebuffer = framebuffer;
    assert(drawState.framebuffer);
    
    drawState.pipeline = getPSO(drawState.framebuffer);

    drawState.viewport.viewports.push_back(nvrhi::Viewport(io.DisplaySize.x * io.DisplayFramebufferScale.x,
                                           io.DisplaySize.y * io.DisplayFramebufferScale.y));
    drawState.viewport.scissorRects.resize(1);  // updated below

    nvrhi::VertexBufferBinding vbufBinding;
    vbufBinding.buffer = vertexBuffer;
    vbufBinding.slot = 0;
    vbufBinding.offset = 0;
    drawState.vertexBuffers.push_back(vbufBinding);

    drawState.indexBuffer.handle = indexBuffer;
    drawState.indexBuffer.format = (sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT);
    drawState.indexBuffer.offset = 0;

    // render command lists
    int vtxOffset = 0;
    int idxOffset = 0;
    for(int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList *cmdList = drawData->CmdLists[n];
        for(int i = 0; i < cmdList->CmdBuffer.Size; i++)
        {
            const ImDrawCmd *pCmd = &cmdList->CmdBuffer[i];

            if (pCmd->UserCallback)
            {
                pCmd->UserCallback(cmdList, pCmd);
            } else {
                drawState.bindings = { getBindingSet((nvrhi::ITexture*)pCmd->TextureId) };
                assert(drawState.bindings[0]);

                drawState.viewport.scissorRects[0] = nvrhi::Rect(int(pCmd->ClipRect.x),
                                                                 int(pCmd->ClipRect.z),
                                                                 int(pCmd->ClipRect.y),
                                                                 int(pCmd->ClipRect.w));

                nvrhi::DrawArguments drawArguments;
                drawArguments.vertexCount = pCmd->ElemCount;
                drawArguments.startIndexLocation = idxOffset;
                drawArguments.startVertexLocation = vtxOffset;

                m_commandList->setGraphicsState(drawState);
                m_commandList->drawIndexed(drawArguments);
            }

            idxOffset += pCmd->ElemCount;
        }

        vtxOffset += cmdList->VtxBuffer.Size;
    }

    m_commandList->endTrackingBufferState(vertexBuffer, nvrhi::ResourceStates::COMMON);
    m_commandList->endTrackingBufferState(indexBuffer, nvrhi::ResourceStates::COMMON);
    m_commandList->endTrackingBufferState(constantBuffer, nvrhi::ResourceStates::COMMON);

    m_commandList->endMarker();
    m_commandList->close();
    renderer->executeCommandList(m_commandList);

    return true;
}

void ImGui_NVRHI::backbufferResizing()
{
    pso = nullptr;
}
