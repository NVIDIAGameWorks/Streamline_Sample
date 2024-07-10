/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#include <donut/render/MipMapGenPass.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>

using namespace donut::math;
#include <donut/shaders/mipmapgen_cb.h>

#include <cassert>
#include <mutex>

using namespace donut::engine;
using namespace donut::render;

// The compute shader reduces 'NUM_LODS' mip-levels at a time into an
// array of NUM_LODS bound UAVs. For textures that have a number
// of mip levels that is not a multiple of NUM_LODS, we need to bind
// "something" to the UAV slots : a set of static dummy NullTextures.
//
// The set of NullTextures is shared by all the MipMapGen compute pass 
// instances and ownership is thread-safe.
//

static nvrhi::TextureHandle createNullTexture(nvrhi::DeviceHandle device)
{
    nvrhi::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.isRenderTarget = false;
    desc.useClearValue = false;
    desc.sampleCount = 1;
    desc.dimension = nvrhi::TextureDimension::Texture2D;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.keepInitialState = true;
    desc.arraySize = 1;
    desc.isUAV = true;
    desc.format = nvrhi::Format::RGBA8_UNORM;

    return device->createTexture(desc);
}

struct MipMapGenPass::NullTextures {

    nvrhi::TextureHandle lod[NUM_LODS];

    static std::shared_ptr<NullTextures> get(nvrhi::DeviceHandle device)
    {
        static std::mutex _mutex;
        static std::weak_ptr<NullTextures> _nullTextures;

        std::lock_guard<std::mutex> lock(_mutex);

        std::shared_ptr<NullTextures> result = _nullTextures.lock();
        if (!result)
        {
            result = std::make_shared<NullTextures>();
            for (int i = 0; i < NUM_LODS; ++i)
                result->lod[i] = createNullTexture(device);
            _nullTextures = result;
        }
        return result;
    }
};

MipMapGenPass::MipMapGenPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    nvrhi::TextureHandle input, 
    Mode mode)
    : m_Device(device)
    , m_Texture(input)
    , m_BindingSets(MAX_PASSES)
    , m_BindingCache(device)
{
    assert(m_Texture);

    m_NullTextures = NullTextures::get(m_Device);

    uint nmipLevels = m_Texture->getDesc().mipLevels;

    // Shader
    assert(mode>=0 && mode <= MODE_MINMAX);

    std::vector<ShaderMacro> macros = { {"MODE", std::to_string(mode)} };
    m_Shader = shaderFactory->CreateShader(
        "donut/passes/mipmapgen_cs.hlsl", "main", &macros, nvrhi::ShaderType::Compute);

    // Constants
    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(MipmmapGenConstants);
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.debugName = "MipMapGenPass/Constants";
    constantBufferDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
    m_ConstantBuffer = m_Device->createBuffer(constantBufferDesc);

    // BindingLayout
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
    layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(0));
    for (uint mipLevel = 1; mipLevel <= NUM_LODS; ++mipLevel)
    {   
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(mipLevel - 1));
    }
    m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

    // BindingSets
    m_BindingSets.resize(MAX_PASSES);
    nvrhi::BindingSetDesc setDesc;
    for (uint i = 0; i < (uint)m_BindingSets.size(); ++i)
    {
        // Create a unique binding set for each compute pass
        if (i * NUM_LODS >= nmipLevels)
            break;

        nvrhi::BindingSetHandle & set = m_BindingSets[i];

        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
        setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(0, m_Texture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(i*NUM_LODS, 1, 0, 1)));
        for (uint mipLevel = 1; mipLevel <= NUM_LODS; ++mipLevel)
        {   // output UAVs start after the mip-level UAV that was computed last
            if (i * NUM_LODS + mipLevel < nmipLevels)
                setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(mipLevel - 1, m_Texture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(i*NUM_LODS + mipLevel, 1, 0, 1)));
            else
                setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(mipLevel - 1, m_NullTextures->lod[mipLevel-1]));
        }
        set = m_Device->createBindingSet(setDesc, m_BindingLayout);
    }

    nvrhi::ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.CS = m_Shader;
    computePipelineDesc.bindingLayouts = { m_BindingLayout };

    m_Pso = device->createComputePipeline(computePipelineDesc);
}

void MipMapGenPass::Dispatch(nvrhi::ICommandList* commandList, int maxLOD) 
{
    assert(m_Texture);

    commandList->beginMarker("MipMapGen::Dispatch");

    uint nmipLevels = m_Texture->getDesc().mipLevels;
    if (maxLOD > 0 && maxLOD < (int)nmipLevels)
        nmipLevels = maxLOD+1;

    uint npasses = (uint32_t)ceilf((float)nmipLevels/(float)NUM_LODS);

    uint width = m_Texture->getDesc().width,
         height = m_Texture->getDesc().height;

    width = (width + GROUP_SIZE - 1) / GROUP_SIZE;
    height = (height + GROUP_SIZE - 1) / GROUP_SIZE;
 
    for (uint i=0; i < MAX_PASSES; ++i)
    {
        if (i * NUM_LODS >= nmipLevels)
            break;

        MipmmapGenConstants constants = {};
        constants.numLODs = std::min(nmipLevels - i*NUM_LODS -1, (uint32_t)NUM_LODS);
        constants.dispatch = i;
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_Pso;
        state.bindings = { m_BindingSets[i] };
        commandList->setComputeState(state);
        commandList->dispatch(width, height);
    }

    commandList->endMarker(); // "MipMapGen::Dispatch"
}


void MipMapGenPass::Display(std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses, nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target)
{
    assert(m_Texture);
    
    commandList->beginMarker("MipMapGen::Display");
    
    nvrhi::Viewport viewport = nvrhi::Viewport((float)target->getFramebufferInfo().width, (float)target->getFramebufferInfo().height);

    float2 size = { m_Texture->getDesc().width / 2.f, m_Texture->getDesc().height / 2.f };
    float2 corner = { 10.f, uint(viewport.maxY) - 10.f };
 
    for (uint level = 0; level < m_Texture->getDesc().mipLevels-1; ++level)
    {
        BlitParameters blitParams;
        blitParams.targetFramebuffer = target;
        blitParams.sourceTexture = m_Texture;
        blitParams.sourceMip = level + 1;
        blitParams.targetViewport = nvrhi::Viewport(
            corner.x,
            corner.x + size.x,
            corner.y - size.y,
            corner.y, 0.f, 1.f
        );

        commonPasses->BlitTexture(commandList, blitParams, &m_BindingCache);

        // spiral pattern
        switch (level % 4)
        {
            case 0: corner += float2(size.x + 10.f, 0.f); break;
            case 1: corner += float2(size.x / 2.f, -(size.y + 10.f)); break;
            case 2: corner += float2(-size.x / 2.f - 10.f, -size.y / 2.f); break;
            case 3: corner += float2(0.f, size.y); break;
        }
        size = { size.x / 2.f, size.y / 2.f };
    }
    commandList->endMarker(); // "MipMapGen::Display"
}