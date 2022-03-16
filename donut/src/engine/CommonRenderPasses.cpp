#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>

using namespace donut::math;
#include <donut/shaders/blit_cb.h>

using namespace donut::engine;

CommonRenderPasses::CommonRenderPasses(nvrhi::DeviceHandle device, std::shared_ptr<ShaderFactory> shaderFactory)
    : m_Device(device)
{
    {
        std::vector<ShaderMacro> VsMacros;
        VsMacros.push_back(ShaderMacro("QUAD_Z", "0"));
        m_FullscreenVS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "fullscreen_vs", "main", &VsMacros, nvrhi::ShaderType::SHADER_VERTEX);

        VsMacros[0].definition = "1";
        m_FullscreenAtOneVS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "fullscreen_vs", "main", &VsMacros, nvrhi::ShaderType::SHADER_VERTEX);
    }

    m_RectVS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "rect_vs", "main", nullptr, nvrhi::ShaderType::SHADER_VERTEX);
    m_BlitPS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "blit_ps", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
    m_SharpenPS = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "sharpen_ps", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);
    
    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(BlitConstants);
    constantBufferDesc.debugName = "BlitConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_BlitCB = m_Device->createBuffer(constantBufferDesc);

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_CLAMP;
    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = false;
    m_PointClampSampler = m_Device->createSampler(samplerDesc);

    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = true;
    m_LinearClampSampler = m_Device->createSampler(samplerDesc);
    
    samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = nvrhi::SamplerDesc::WRAP_MODE_WRAP;
    m_LinearWrapSampler = m_Device->createSampler(samplerDesc);

    samplerDesc.anisotropy = 16;
    m_AnisotropicWrapSampler = m_Device->createSampler(samplerDesc);

    {
        unsigned int blackImage = 0xff000000;
        unsigned int grayImage = 0xff808080;
        unsigned int whiteImage = 0xffffffff;
        unsigned int zeroImage = 0x00000000;

        nvrhi::TextureDesc textureDesc;
        textureDesc.format = nvrhi::Format::RGBA8_UNORM;
        textureDesc.width = 1;
        textureDesc.height = 1;
        textureDesc.mipLevels = 1;

        textureDesc.debugName = "BlackTexture";
        m_BlackTexture = m_Device->createTexture(textureDesc);

        textureDesc.debugName = "GrayTexture";
        m_GrayTexture = m_Device->createTexture(textureDesc);

        textureDesc.debugName = "WhiteTexture";
        m_WhiteTexture = m_Device->createTexture(textureDesc);

        textureDesc.dimension = nvrhi::TextureDimension::TextureCubeArray;
        textureDesc.debugName = "BlackCubeMapArray";
        textureDesc.arraySize = 6;
        m_BlackCubeMapArray = m_Device->createTexture(textureDesc);

        textureDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
        textureDesc.debugName = "BlackTexture2DArray";
        textureDesc.arraySize = 6;
        m_BlackTexture2DArray = m_Device->createTexture(textureDesc);

        textureDesc.format = nvrhi::Format::R32_FLOAT;
        textureDesc.debugName = "ZeroDepthStencil2DArray";
        textureDesc.arraySize = 6;
        m_ZeroDepthStencil2DArray = m_Device->createTexture(textureDesc);

        // Write the textures using a temporary CL

        nvrhi::CommandListHandle commandList = m_Device->createCommandList();
        commandList->open();

        commandList->beginTrackingTextureState(m_BlackTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(m_GrayTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(m_WhiteTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(m_BlackCubeMapArray, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(m_BlackTexture2DArray, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);
        commandList->beginTrackingTextureState(m_ZeroDepthStencil2DArray, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);

        commandList->writeTexture(m_BlackTexture, 0, 0, &blackImage, 0);
        commandList->writeTexture(m_GrayTexture, 0, 0, &grayImage, 0);
        commandList->writeTexture(m_WhiteTexture, 0, 0, &whiteImage, 0);
        
        for (uint32_t arraySlice = 0; arraySlice < 6; arraySlice += 1)
        {
            commandList->writeTexture(m_BlackTexture2DArray, arraySlice, 0, &blackImage, 0);
            commandList->writeTexture(m_BlackCubeMapArray, arraySlice, 0, &blackImage, 0);

            commandList->writeTexture(m_ZeroDepthStencil2DArray, arraySlice, 0, &zeroImage, 0);
        }

        // Permanently transition them to a SHADER_RESOURCE state

        commandList->endTrackingTextureState(m_BlackTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
        commandList->endTrackingTextureState(m_GrayTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
        commandList->endTrackingTextureState(m_WhiteTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
        commandList->endTrackingTextureState(m_BlackCubeMapArray, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
        commandList->endTrackingTextureState(m_BlackTexture2DArray, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
        commandList->endTrackingTextureState(m_ZeroDepthStencil2DArray, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);

        commandList->close();
        m_Device->executeCommandList(commandList);
    }

    {
        nvrhi::BindingLayoutDesc layoutDesc;

        layoutDesc.VS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer }
        };

        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer },
            { 0, nvrhi::ResourceType::Texture_SRV },
            { 0, nvrhi::ResourceType::Sampler }
        };

        m_BlitBindingLayout = m_Device->createBindingLayout(layoutDesc);
    }
}

nvrhi::BindingSetHandle CommonRenderPasses::CreateBlitBindingSet(nvrhi::ITexture* source, uint32_t sourceArraySlice, uint32_t sourceMip)
{
    nvrhi::BindingSetDesc bindingDesc;

    bindingDesc.VS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_BlitCB)
    };

    bindingDesc.PS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_BlitCB),
        nvrhi::BindingSetItem::Texture_SRV(0, source, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(sourceMip, 1, sourceArraySlice, 1)),
        nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler)
    };

    return m_Device->createBindingSet(bindingDesc, m_BlitBindingLayout);
}

void CommonRenderPasses::BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, dm::box2_arg targetBox, nvrhi::IBindingSet* source, dm::box2_arg sourceBox)
{
    const nvrhi::FramebufferAttachment& attachment = target->GetDesc().colorAttachments[0];
    const nvrhi::TextureDesc& targetDesc = attachment.texture->GetDesc();

    nvrhi::GraphicsPipelineHandle& pso = m_BlitPsoCache[target->GetFramebufferInfo()];
    if (!pso)
    {
        nvrhi::GraphicsPipelineDesc psoDesc;
        psoDesc.bindingLayouts = { m_BlitBindingLayout };
        psoDesc.VS = m_RectVS;
        psoDesc.PS = m_BlitPS;
        psoDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        psoDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        psoDesc.renderState.depthStencilState.depthEnable = false;
        psoDesc.renderState.depthStencilState.stencilEnable = false;
        
        pso = m_Device->createGraphicsPipeline(psoDesc, target);
    }

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = target;
    state.bindings = { source };
    state.viewport.addViewport(viewport);
    state.viewport.addScissorRect(nvrhi::Rect(viewport));

    BlitConstants blitConstants = {};
    blitConstants.sourceOrigin = float2(sourceBox.m_mins);
    blitConstants.sourceSize = sourceBox.diagonal();
    blitConstants.targetOrigin = float2(targetBox.m_mins);
    blitConstants.targetSize = targetBox.diagonal();

    commandList->writeBuffer(m_BlitCB, &blitConstants, sizeof(blitConstants));

    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);
}

void CommonRenderPasses::BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, nvrhi::IBindingSet* source)
{
    BlitTexture(commandList, target, viewport, box2(0.f, 1.f), source, box2(0.f, 1.f));
}

void CommonRenderPasses::BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, nvrhi::ITexture* source, uint32_t sourceArraySlice /*= 0*/, uint32_t sourceMip /*= 0*/)
{
    BlitTexture(commandList, target, viewport, GetCachedBindingSet(source, sourceArraySlice, sourceMip));
}

void CommonRenderPasses::BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, dm::box2_arg targetBox, nvrhi::ITexture* source, dm::box2_arg sourceBox, uint32_t sourceArraySlice /*= 0*/, uint32_t sourceMip /*= 0*/)
{
    BlitTexture(commandList, target, viewport, targetBox, GetCachedBindingSet(source, sourceArraySlice, sourceMip), sourceBox);
}

void CommonRenderPasses::BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, dm::box2_arg targetBox, nvrhi::IBindingSet* source, dm::box2_arg sourceBox)
{
    const nvrhi::FramebufferAttachment& attachment = target->GetDesc().colorAttachments[0];
    const nvrhi::TextureDesc& targetDesc = attachment.texture->GetDesc();

    nvrhi::GraphicsPipelineHandle& pso = m_SharpenPsoCache[target->GetFramebufferInfo()];
    if (!pso)
    {
        nvrhi::GraphicsPipelineDesc psoDesc;
        psoDesc.bindingLayouts = { m_BlitBindingLayout };
        psoDesc.VS = m_RectVS;
        psoDesc.PS = m_SharpenPS;
        psoDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        psoDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        psoDesc.renderState.depthStencilState.depthEnable = false;
        psoDesc.renderState.depthStencilState.stencilEnable = false;

        pso = m_Device->createGraphicsPipeline(psoDesc, target);
    }

    nvrhi::GraphicsState state;
    state.pipeline = pso;
    state.framebuffer = target;
    state.bindings = { source };
    state.viewport.addViewport(viewport);
    state.viewport.addScissorRect(nvrhi::Rect(viewport));

    BlitConstants blitConstants = {};
    blitConstants.sourceOrigin = float2(sourceBox.m_mins);
    blitConstants.sourceSize = sourceBox.diagonal();
    blitConstants.targetOrigin = float2(targetBox.m_mins);
    blitConstants.targetSize = targetBox.diagonal();
    blitConstants.sharpenFactor = sharpenFactor;

    commandList->writeBuffer(m_BlitCB, &blitConstants, sizeof(blitConstants));

    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);
}

void CommonRenderPasses::BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, nvrhi::IBindingSet* source)
{
    BlitSharpen(commandList, target, viewport, sharpenFactor, box2(0.f, 1.f), source, box2(0.f, 1.f));
}

void CommonRenderPasses::BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, nvrhi::ITexture* source, uint32_t sourceArraySlice /*= 0*/, uint32_t sourceMip /*= 0*/)
{
    BlitSharpen(commandList, target, viewport, sharpenFactor, GetCachedBindingSet(source, sourceArraySlice, sourceMip));
}

void CommonRenderPasses::BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, dm::box2_arg targetBox, nvrhi::ITexture* source, dm::box2_arg sourceBox, uint32_t sourceArraySlice /*= 0*/, uint32_t sourceMip /*= 0*/)
{
    BlitSharpen(commandList, target, viewport, sharpenFactor, targetBox, GetCachedBindingSet(source, sourceArraySlice, sourceMip), sourceBox);
}

void CommonRenderPasses::ResetBindingCache()
{
    m_BlitBindingCache.clear();
}

nvrhi::IBindingSet* CommonRenderPasses::GetCachedBindingSet(nvrhi::ITexture* texture, uint32_t arraySlice, uint32_t mipLevel)
{
    BlitBindingKey key;
    key.texture = texture;
    key.arraySlice = arraySlice;
    key.mipLevel = mipLevel;

    nvrhi::BindingSetHandle& bindingSet = m_BlitBindingCache[key];

    if (!bindingSet)
    {
        bindingSet = CreateBlitBindingSet(texture, arraySlice, mipLevel);
    }

    return bindingSet;
}