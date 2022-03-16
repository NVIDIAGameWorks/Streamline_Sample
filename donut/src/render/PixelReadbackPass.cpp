
#include <donut/render/PixelReadbackPass.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>

using namespace donut::math;
#include <donut/shaders/pixel_readback_cb.h>

using namespace donut::engine;
using namespace donut::render;

PixelReadbackPass::PixelReadbackPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    nvrhi::ITexture* inputTexture, 
    nvrhi::Format format,
    uint32_t arraySlice,
    uint32_t mipLevel)
    : m_Device(device)
{
    const char* formatName = "";
    switch (format)
    {
    case nvrhi::Format::RGBA32_FLOAT: formatName = "float4"; break;
    case nvrhi::Format::RGBA32_UINT: formatName = "uint4"; break;
    case nvrhi::Format::RGBA32_SINT: formatName = "int4"; break;
    default: assert(!"unsupported readback format");
    }

    std::vector<ShaderMacro> macros;
    macros.push_back(ShaderMacro("TYPE", formatName));
    m_Shader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/pixel_readback_cs.hlsl", "main", &macros, nvrhi::ShaderType::SHADER_COMPUTE);

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = 16;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.initialState = nvrhi::ResourceStates::COPY_SOURCE;
    bufferDesc.keepInitialState = true;
    bufferDesc.debugName = "PixelReadbackPass/IntermediateBuffer";
    m_IntermediateBuffer = m_Device->createBuffer(bufferDesc);

    bufferDesc.canHaveUAVs = false;
    bufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    bufferDesc.debugName = "PixelReadbackPass/ReadbackBuffer";
    m_ReadbackBuffer = m_Device->createBuffer(bufferDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(PixelReadbackConstants);
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.debugName = "PixelReadbackPass/Constants";
    m_ConstantBuffer = m_Device->createBuffer(constantBufferDesc);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.CS = { 
        { 0, nvrhi::ResourceType::VolatileConstantBuffer },
        { 0, nvrhi::ResourceType::Texture_SRV },
        { 0, nvrhi::ResourceType::Buffer_UAV }
    };

    m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

    nvrhi::BindingSetDesc setDesc;
    setDesc.CS = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, inputTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(mipLevel, 1, arraySlice, 1)),
        nvrhi::BindingSetItem::Buffer_UAV(0, m_IntermediateBuffer, format)
    };

    m_BindingSet = m_Device->createBindingSet(setDesc, m_BindingLayout);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_Shader;
    m_Pipeline = m_Device->createComputePipeline(pipelineDesc);
}


void PixelReadbackPass::Capture(nvrhi::ICommandList* commandList, dm::uint2 pixelPosition)
{
    PixelReadbackConstants constants = {};
    constants.pixelPosition = pixelPosition;
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    nvrhi::ComputeState state;
    state.pipeline = m_Pipeline;
    state.bindings = { m_BindingSet };
    commandList->setComputeState(state);
    commandList->dispatch(1, 1, 1);

    commandList->copyBuffer(m_ReadbackBuffer, 0, m_IntermediateBuffer, 0, m_ReadbackBuffer->GetDesc().byteSize);
}

dm::float4 PixelReadbackPass::ReadFloats()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    float4 values = *static_cast<float4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}

dm::uint4 PixelReadbackPass::ReadUInts()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    uint4 values = *static_cast<uint4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}

dm::int4 PixelReadbackPass::ReadInts()
{
    void* pData = m_Device->mapBuffer(m_ReadbackBuffer, nvrhi::CpuAccessMode::Read);
    assert(pData);

    int4 values = *static_cast<int4*>(pData);

    m_Device->unmapBuffer(m_ReadbackBuffer);
    return values;
}
