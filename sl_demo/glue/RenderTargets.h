//----------------------------------------------------------------------------------
// File:        RenderTargets.h
// SDK Version: 1.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------
#pragma once

#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/TextureCache.h>

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

class RenderTargets
{
public:
    nvrhi::TextureHandle m_Depth;
    nvrhi::TextureHandle m_GBufferDiffuse;
    nvrhi::TextureHandle m_GBufferSpecular;
    nvrhi::TextureHandle m_GBufferNormals;
    nvrhi::TextureHandle m_HdrColor;
    nvrhi::TextureHandle m_LdrColor;
    nvrhi::TextureHandle m_MotionVectors;
    nvrhi::TextureHandle m_ResolvedColor;
    nvrhi::TextureHandle m_TemporalFeedback1;
    nvrhi::TextureHandle m_TemporalFeedback2;

    std::shared_ptr<FramebufferFactory> m_ForwardFramebuffer;
    std::shared_ptr<FramebufferFactory> m_HdrFramebuffer;
    std::shared_ptr<FramebufferFactory> m_GBufferFramebuffer;
    std::shared_ptr<FramebufferFactory> m_LdrFramebuffer;

    int2 m_MaximumRenderSize; // In dynamic scaling scenarios this is the maximum render size (as we could render to a subrect of it)
    int2 m_DisplaySize;
    bool m_PreTonemapping;

    RenderTargets(nvrhi::IDevice* device, int2 maximumRenderSize, int2 displaySize, bool preTonemapping)
        : m_MaximumRenderSize(maximumRenderSize)
        , m_DisplaySize(displaySize)
        , m_PreTonemapping(preTonemapping)
    {
        nvrhi::TextureDesc desc;
        desc.width = maximumRenderSize.x;
        desc.height = maximumRenderSize.y;
        desc.isRenderTarget = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(1.f);
        desc.sampleCount = 1;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.keepInitialState = true;

        desc.debugName = "DepthBuffer";
        desc.isTypeless = true;
        desc.format = nvrhi::Format::D24S8;
        desc.initialState = nvrhi::ResourceStates::DEPTH_WRITE;
        m_Depth = device->createTexture(desc);

        desc.debugName = "HdrColor";
        desc.clearValue = nvrhi::Color(0.f);
        desc.isTypeless = false;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.initialState = nvrhi::ResourceStates::RENDER_TARGET;
        m_HdrColor = device->createTexture(desc);

        desc.debugName = "GBufferDiffuse";
        desc.format = nvrhi::Format::SRGBA8_UNORM;
        m_GBufferDiffuse = device->createTexture(desc);

        desc.debugName = "GBufferSpecular";
        desc.format = nvrhi::Format::SRGBA8_UNORM;
        m_GBufferSpecular = device->createTexture(desc);

        desc.debugName = "GBufferNormals";
        desc.format = nvrhi::Format::RGBA16_SNORM;
        m_GBufferNormals = device->createTexture(desc);

        desc.debugName = "MotionVectors";
        desc.format = nvrhi::Format::RG16_FLOAT;
        m_MotionVectors = device->createTexture(desc);

        desc.debugName = "TemporalFeedback1";
        desc.format = nvrhi::Format::RGBA16_SNORM;
        desc.isUAV = true;
        m_TemporalFeedback1 = device->createTexture(desc);
        desc.debugName = "TemporalFeedback2";
        m_TemporalFeedback2 = device->createTexture(desc);

        desc.debugName = "ResolvedColor";
        desc.format = preTonemapping ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RGBA8_UNORM;
        desc.width = displaySize.x;
        desc.height = displaySize.y;
        desc.isUAV = true;
        m_ResolvedColor = device->createTexture(desc);

        desc.debugName = "LdrColor";
        // we blt or upscale from ldr to upscaled LDR, so keep fp16 until then
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isUAV = true;
        if (m_PreTonemapping)
        {
            desc.width = displaySize.x;
            desc.height = displaySize.y;
        }
        else
        {
            desc.width = maximumRenderSize.x;
            desc.height = maximumRenderSize.y;
        }
        m_LdrColor = device->createTexture(desc);

        /// --------------------
        m_ForwardFramebuffer = std::make_shared<FramebufferFactory>(device);
        m_ForwardFramebuffer->RenderTargets = { m_HdrColor };
        m_ForwardFramebuffer->DepthTarget = m_Depth;

        m_GBufferFramebuffer = std::make_shared<FramebufferFactory>(device);
        m_GBufferFramebuffer->RenderTargets = { m_GBufferDiffuse, m_GBufferSpecular, m_GBufferNormals, m_MotionVectors };
        m_GBufferFramebuffer->DepthTarget = m_Depth;

        m_HdrFramebuffer = std::make_shared<FramebufferFactory>(device);
        m_HdrFramebuffer->RenderTargets = { m_HdrColor };

        m_LdrFramebuffer = std::make_shared<FramebufferFactory>(device);
        m_LdrFramebuffer->RenderTargets = { m_LdrColor };
    }

    bool IsUpdateRequired(int2 maximumRenderSize, int2 displaySize, bool preTonemapping)
    {
        if (any(m_MaximumRenderSize != maximumRenderSize) ||
            any(m_DisplaySize != displaySize) ||
            m_PreTonemapping != preTonemapping)
            return true;

        return false;
    }

    void Clear(nvrhi::ICommandList* commandList)
    {
        commandList->clearTextureFloat(m_Depth, nvrhi::AllSubresources, nvrhi::Color(1.f, 0.f, 0.f, 0.f));
        commandList->clearTextureFloat(m_HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(m_GBufferDiffuse, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(m_GBufferSpecular, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(m_GBufferNormals, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(m_MotionVectors, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }

    const int2& GetMaximumRenderSize()
    {
        return m_MaximumRenderSize;
    }
};

