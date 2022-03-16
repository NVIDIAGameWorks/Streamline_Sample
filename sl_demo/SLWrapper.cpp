//----------------------------------------------------------------------------------
// File:        SLWrapper.cpp
// SDK Version: 1.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <SLWrapper.h>

#ifdef USE_SL

#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <assert.h>
#include <stdexcept>
#include <filesystem>


#if USE_DX11
#include <d3d11.h>
#include <nvrhi/d3d11/d3d11.h>
#endif
#if USE_DX12
#include <d3d12.h>
#include <nvrhi/d3d12/d3d12.h>
#endif

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

bool SLWrapper::m_sl_initialised = false;
nvrhi::IDevice* SLWrapper::m_Device = nullptr;


SLWrapper::SLWrapper(nvrhi::IDevice* device)
{
    m_Device = device;

    if (!m_sl_initialised) log::error("Must initialise SL before creating the wrapper.");

    m_dlss_available = CheckSupportDLSS();
    m_dlss_consts = {};

}

void SLWrapper::logFunctionCallback(sl::LogType type, const char* msg) {

    if (type == sl::LogType::eLogTypeError) {
        // Add a breakpoint here to break on errors
        donut::log::error(msg);
    }
    if (type == sl::LogType::eLogTypeWarn) {
        // Add a breakpoint here to break on warnings
        donut::log::warning(msg);
    }
    else {
        donut::log::info(msg);
    }
}

void SLWrapper::Initialize()
{
    sl::Preferences pref;

    pref.allocateCallback = &allocateResourceCallback;
    pref.releaseCallback = &releaseResourceCallback;

    // Current path is successful when when the exe is run, but mislead when run through VS. 
    // std::wstring baseDir = std::filesystem::current_path().wstring();
    // pref.numPathsToPlugins = 1;
    // const wchar_t* dir[] = { baseDir.c_str() };
    // pref.pathsToPlugins = dir;

#if _DEBUG
    pref.showConsole = true;
    pref.logMessageCallback = &logFunctionCallback;
    pref.logLevel = sl::LogLevel::eLogLevelDefault;
    // pref.pathToLogsAndData = baseDir.c_str();
#else
    pref.logLevel = sl::LogLevel::eLogLevelOff;
#endif

    m_sl_initialised = sl::init(pref, APP_ID);
    if (!m_sl_initialised) log::error("Failed to initialse SL.");
}

void SLWrapper::Shutdown()
{
    if (m_sl_initialised) {
        bool success = sl::shutdown();
        if (!success) log::error("Failed to shutdown SL properly.");
        m_sl_initialised = false;
    }
}

void SLWrapper::SetSLConsts(const sl::Constants& consts, int frameNumber) {
    if (!m_sl_initialised) log::error("SL not initialised.");
    m_sl_consts = consts;
    if (!sl::setConstants(m_sl_consts, frameNumber)) log::error("Failed to set SL constants.");
}

void SLWrapper::SetDLSSConsts(const sl::DLSSConstants consts, int frameNumber)
{
    if (!m_sl_initialised || !m_dlss_available) log::error("SL not initialised or DLSS not available.");
    m_dlss_consts = consts;
    if (!sl::setFeatureConstants(sl::eFeatureDLSS, &m_dlss_consts, frameNumber)) log::error("Failed to set DLSS constants.");

}

void SLWrapper::QueryDLSSOptimalSettings(int2& outRenderSize, float& sharpness) {
    if (!m_sl_initialised || !m_dlss_available) log::error("SL not initialised or DLSS not available.");

    sl::DLSSSettings dlssSettings = {};
    if (!sl::getFeatureSettings(sl::eFeatureDLSS, &m_dlss_consts, &dlssSettings)) log::error("Failed to get DLSS optimal settings.");

    outRenderSize.x = static_cast<int>(dlssSettings.renderWidth);
    outRenderSize.y = static_cast<int>(dlssSettings.renderHeight);
    sharpness = dlssSettings.optimalSharpness;

}

bool SLWrapper::CheckSupportDLSS() {
    if (!m_sl_initialised) log::error("SL not initialised.");

    bool support = sl::isFeatureSupported(sl::eFeatureDLSS);
    if (support) log::info("DLSS is supported on this system.");
    else log::warning("DLSS is not supported on this system.");
    return support;
}

sl::Resource SLWrapper::allocateResourceCallback(const sl::ResourceDesc* resDesc) {

    if (m_Device == nullptr) {
        log::error("There is no device to use for resource allocation. Most likely, SLWrapper has not been instantiated yet.");
        return sl::Resource{};
    }

    sl::Resource res = {};

    bool isBuffer = (resDesc->type == sl::ResourceType::eResourceTypeBuffer);

    if (isBuffer) {

#ifdef USE_DX11

        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_BUFFER_DESC* desc = (D3D11_BUFFER_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_Device);
            ID3D11Buffer* pbuffer;
            bool success = SUCCEEDED(pd3d11Device->CreateBuffer(desc, nullptr, &pbuffer));
            if (!success) log::error("Failed to create buffer in SL allocation callback");
            res.type = resDesc->type;
            res.native = pbuffer;

        }
#endif

#ifdef USE_DX12
        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        {
            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            ID3D12Device* pd3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
            ID3D12Resource* pbuffer;
            bool success = SUCCEEDED(pd3d12Device->CreateCommittedResource(heap, D3D12_HEAP_FLAG_NONE, desc, state, nullptr, IID_PPV_ARGS(&pbuffer)));
            if (!success) log::error("Failed to create buffer in SL allocation callback");
            res.type = resDesc->type;
            res.native = pbuffer;
        }
#endif

    }

    else {

#ifdef USE_DX11

        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_TEXTURE2D_DESC* desc = (D3D11_TEXTURE2D_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_Device);
            ID3D11Texture2D* ptexture;
            bool success = SUCCEEDED(pd3d11Device->CreateTexture2D(desc, nullptr, &ptexture));
            if (!success) log::error("Failed to create texture in SL allocation callback");
            res.type = resDesc->type;
            res.native = ptexture;

        }
#endif

#ifdef USE_DX12
        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        {

            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            ID3D12Device* pd3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
            ID3D12Resource* ptexture;
            bool success = SUCCEEDED(pd3d12Device->CreateCommittedResource(heap, D3D12_HEAP_FLAG_NONE, desc, state, nullptr, IID_PPV_ARGS(&ptexture)));
            if (!success) log::error("Failed to create texture in SL allocation callback");
            res.type = resDesc->type;
            res.native = ptexture;
        }
#endif

    }
    return res;

}

void SLWrapper::releaseResourceCallback(sl::Resource * resource)
{
    if (resource)
    {
        auto i = (IUnknown*)resource->native;
        i->Release();
    }
};

void SLWrapper::EvaluateDLSS(nvrhi::ICommandList* commandList,
            nvrhi::ITexture* unresolvedColor,
            nvrhi::ITexture* resolvedColor,
            nvrhi::ITexture* motionVectors,
            nvrhi::ITexture* depth,
            uint32_t frameIndex)
{
    if (!m_sl_initialised || !m_dlss_available) log::error("SL not initialised or DLSS not available.");
    if (m_Device == nullptr) log::error("No device available.");
    
    if (!sl::setFeatureConstants(sl::Feature::eFeatureDLSS, &m_dlss_consts, frameIndex)) log::error("Failed to set DLSS features.");

    void* context;
    bool success = true;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        context = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);

        sl::Resource unresolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, unresolvedColor->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource motionVectorsResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource resolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, resolvedColor->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };
        sl::Resource depthResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource) };

        success = success && sl::setTag(&resolvedColorResource, sl::eBufferTypeDLSSInputColor);
        success = success && sl::setTag(&unresolvedColorResource, sl::eBufferTypeDLSSOutputColor);
        success = success && sl::setTag(&motionVectorsResource, sl::eBufferTypeMVec);
        success = success && sl::setTag(&depthResource, sl::eBufferTypeDepth);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        context = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

        sl::Resource unresolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, unresolvedColor->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource) };
        sl::Resource motionVectorsResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource) };
        sl::Resource resolvedColorResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, resolvedColor->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource) };
        sl::Resource depthResource = sl::Resource{ sl::ResourceType::eResourceTypeTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource) };

        success = success && sl::setTag(&resolvedColorResource, sl::eBufferTypeDLSSInputColor);
        success = success && sl::setTag(&unresolvedColorResource, sl::eBufferTypeDLSSOutputColor);
        success = success && sl::setTag(&motionVectorsResource, sl::eBufferTypeMVec);
        success = success && sl::setTag(&depthResource, sl::eBufferTypeDepth);
    }
#endif

    if (!success) log::error("Failed DLSS tag setting");

    if (!sl::evaluateFeature(context, sl::Feature::eFeatureDLSS, frameIndex)) { log::error("Failed DLSS evaluation"); }

    return;
}

#endif



