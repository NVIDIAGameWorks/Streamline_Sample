//----------------------------------------------------------------------------------
// File:        SLWrapper.cpp
// SDK Version: 2.0
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
#include "SLWrapper.h" 

#include <donut/core/log.h>
#include <filesystem>
#include <dxgi.h>
#include <dxgi1_5.h>


#if USE_DX11
#include <d3d11.h>
#include <nvrhi/d3d11.h>
#endif
#if USE_DX12
#include <d3d12.h>
#include <nvrhi/d3d12.h>
#endif
#if USE_VK
#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
#include <../src/vulkan/vulkan-backend.h>
#endif

#include "sl_security.h"

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#include <climits>
#else
#define PATH_MAX MAX_PATH
#endif // _WIN32

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

void logFunctionCallback(sl::LogType type, const char* msg) {

    if (type == sl::LogType::eError) {
        // Add a breakpoint here to break on errors
        donut::log::error(msg);
    }
    if (type == sl::LogType::eWarn) {
        // Add a breakpoint here to break on warnings
        donut::log::warning(msg);
    }
    else {
        donut::log::info(msg);
    }
}

bool successCheck(sl::Result result, char* location) {

    if (result == sl::Result::eOk)
        return true;

    const std::map< const sl::Result, const std::string> errors = {
            {sl::Result::eErrorIO,"eErrorIO"},
            {sl::Result::eErrorDriverOutOfDate,"eErrorDriverOutOfDate"},
            {sl::Result::eErrorOSOutOfDate,"eErrorOSOutOfDate"},
            {sl::Result::eErrorOSDisabledHWS,"eErrorOSDisabledHWS"},
            {sl::Result::eErrorDeviceNotCreated,"eErrorDeviceNotCreated"},
            {sl::Result::eErrorAdapterNotSupported,"eErrorAdapterNotSupported"},
            {sl::Result::eErrorNoPlugins,"eErrorNoPlugins"},
            {sl::Result::eErrorVulkanAPI,"eErrorVulkanAPI"},
            {sl::Result::eErrorDXGIAPI,"eErrorDXGIAPI"},
            {sl::Result::eErrorD3DAPI,"eErrorD3DAPI"},
            {sl::Result::eErrorNRDAPI,"eErrorNRDAPI"},
            {sl::Result::eErrorNVAPI,"eErrorNVAPI"},
            {sl::Result::eErrorReflexAPI,"eErrorReflexAPI"},
            {sl::Result::eErrorNGXFailed,"eErrorNGXFailed"},
            {sl::Result::eErrorJSONParsing,"eErrorJSONParsing"},
            {sl::Result::eErrorMissingProxy,"eErrorMissingProxy"},
            {sl::Result::eErrorMissingResourceState,"eErrorMissingResourceState"},
            {sl::Result::eErrorInvalidIntegration,"eErrorInvalidIntegration"},
            {sl::Result::eErrorMissingInputParameter,"eErrorMissingInputParameter"},
            {sl::Result::eErrorNotInitialized,"eErrorNotInitialized"},
            {sl::Result::eErrorComputeFailed,"eErrorComputeFailed"},
            {sl::Result::eErrorInitNotCalled,"eErrorInitNotCalled"},
            {sl::Result::eErrorExceptionHandler,"eErrorExceptionHandler"},
            {sl::Result::eErrorInvalidParameter,"eErrorInvalidParameter"},
            {sl::Result::eErrorMissingConstants,"eErrorMissingConstants"},
            {sl::Result::eErrorDuplicatedConstants,"eErrorDuplicatedConstants"},
            {sl::Result::eErrorMissingOrInvalidAPI,"eErrorMissingOrInvalidAPI"},
            {sl::Result::eErrorCommonConstantsMissing,"eErrorCommonConstantsMissing"},
            {sl::Result::eErrorUnsupportedInterface,"eErrorUnsupportedInterface"},
            {sl::Result::eErrorFeatureMissing,"eErrorFeatureMissing"},
            {sl::Result::eErrorFeatureNotSupported,"eErrorFeatureNotSupported"},
            {sl::Result::eErrorFeatureMissingHooks,"eErrorFeatureMissingHooks"},
            {sl::Result::eErrorFeatureFailedToLoad,"eErrorFeatureFailedToLoad"},
            {sl::Result::eErrorFeatureWrongPriority,"eErrorFeatureWrongPriority"},
            {sl::Result::eErrorFeatureMissingDependency,"eErrorFeatureMissingDependency"},
            {sl::Result::eErrorFeatureManagerInvalidState,"eErrorFeatureManagerInvalidState"},
            {sl::Result::eErrorInvalidState,"eErrorInvalidState"}, 
            {sl::Result::eWarnOutOfVRAM,"eWarnOutOfVRAM"} };

    auto a = errors.find(result);
    if (a != errors.end())
        logFunctionCallback(sl::LogType::eError, ("Error: " + a->second + (location == nullptr ? "" : (" encountered in " + std::string(location)))).c_str());
    else
        logFunctionCallback(sl::LogType::eError, ("Unknown error " + static_cast<int>(result) + (location == nullptr ? "" : (" encountered in " + std::string(location)))).c_str());
    
    return false;

}

std::wstring GetSlInterposerDllLocation() {

    wchar_t path[PATH_MAX] = { 0 };
#ifdef _WIN32
    if (GetModuleFileNameW(nullptr, path, dim(path)) == 0)
        return std::wstring();
#else // _WIN32
#error Unsupported platform for GetSlInterposerDllLocation!
#endif // _WIN32

    auto basePath = std::filesystem::path(path).parent_path();
    auto dllPath = basePath.wstring().append(L"\\sl.interposer.dll");
    return dllPath;
}

SLWrapper& SLWrapper::Get() {
    static SLWrapper instance;
    return instance;
}

bool SLWrapper::Initialize_preDevice(nvrhi::GraphicsAPI api, const bool& checkSig, const bool& SLlog)
{

    if (m_sl_initialised) {
        log::info("SLWrapper is already initialised.");
        return true;
    }

    sl::Preferences pref;

    m_api = api;

    if (m_api != nvrhi::GraphicsAPI::VULKAN) {
        pref.allocateCallback = &allocateResourceCallback;
        pref.releaseCallback = &releaseResourceCallback;
    }

    pref.applicationId = APP_ID;

#if _DEBUG
    pref.showConsole = true;
    pref.logMessageCallback = &logFunctionCallback;
    pref.logLevel = sl::LogLevel::eDefault;
#else
    if (SLlog) {
        pref.showConsole = true;
        pref.logMessageCallback = &logFunctionCallback;
        pref.logLevel = sl::LogLevel::eDefault;
    }
    else {
        pref.logLevel = sl::LogLevel::eOff;
    }
#endif

    sl::Feature featuresToLoad[] = {
#ifdef STREAMLINE_FEATURE_DLSS_SR
        sl::kFeatureDLSS,
#endif
#ifdef STREAMLINE_FEATURE_NIS
        sl::kFeatureNIS,
#endif
#ifdef STREAMLINE_FEATURE_DLSS_FG
        sl::kFeatureDLSS_G,
#endif
#ifdef STREAMLINE_FEATURE_REFLEX
        sl::kFeatureReflex,
#endif
#ifdef STREAMLINE_FEATURE_DEEPDVC
        sl::kFeatureDeepDVC
#endif
    };
    pref.featuresToLoad = featuresToLoad;
    pref.numFeaturesToLoad = _countof(featuresToLoad);

    switch (api) {
    case (nvrhi::GraphicsAPI::D3D11):
        pref.renderAPI = sl::RenderAPI::eD3D11;
        break;
    case (nvrhi::GraphicsAPI::D3D12):
        pref.renderAPI = sl::RenderAPI::eD3D12;
        break;
    case (nvrhi::GraphicsAPI::VULKAN):
        pref.renderAPI = sl::RenderAPI::eVulkan;
        break;
    }

    pref.flags |= sl::PreferenceFlags::eUseManualHooking;

    auto pathDll = GetSlInterposerDllLocation();

    HMODULE interposer = {};
    if (checkSig && sl::security::verifyEmbeddedSignature(pathDll.c_str())) {
        interposer = LoadLibraryW(pathDll.c_str());
    }
    else {
        interposer = LoadLibraryW(pathDll.c_str());
    }

    if (!interposer)
    {
        donut::log::error("Unable to load Streamline Interposer");
        return false;
    }

    m_sl_initialised = successCheck(slInit(pref, SDK_VERSION), "slInit");
    if (!m_sl_initialised) {
        log::error("Failed to initialse SL.");
        return false;
    }

    // turn off dlssg
    if (api == nvrhi::GraphicsAPI::D3D12) {
        slSetFeatureLoaded(sl::kFeatureDLSS_G, false);
    }

    return true;
}

bool SLWrapper::Initialize_postDevice()
{

    // We set reflex consts to a default config. This can be changed at runtime in the UI.
    auto reflexConst = sl::ReflexOptions{};
    reflexConst.mode = sl::ReflexMode::eOff;
    reflexConst.useMarkersToOptimize = true;
    reflexConst.virtualKey = VK_F13;
    reflexConst.frameLimitUs = 0;
    SetReflexConsts(reflexConst);

    return true;
}

void SLWrapper::FindAdapter(void*& adapterPtr, void* vkDevices) {

    adapterPtr = nullptr;
    sl::AdapterInfo adapterInfo;

    auto checkFeature = [this, &adapterInfo](sl::Feature feature, std::string feature_name) -> bool {
        sl::Result res = slIsFeatureSupported(feature, adapterInfo);
        if (res == sl::Result::eOk) {
            log::info((feature_name + " is supported on this adapter").c_str());
        }
        else {
            std::string errorType;
            switch (res) {
            case(sl::Result::eErrorOSOutOfDate): errorType = "OS out of date"; break;
            case(sl::Result::eErrorDriverOutOfDate): errorType = "Driver out of Date"; break;
            case(sl::Result::eErrorAdapterNotSupported): errorType = "Unsupported adapter (old or non-nvidia gpu)"; break;
            }
            log::info((feature_name + " is NOT supported on this adapter with error: " + errorType).c_str());
        }
        return (res == sl::Result::eOk);
    };

#if USE_DX11 || USE_DX12
    if (m_api == nvrhi::GraphicsAPI::D3D11 || m_api == nvrhi::GraphicsAPI::D3D12) {

        IDXGIAdapter* targetAdapter;
        IDXGIFactory1* DXGIFactory;
        HRESULT hres = CreateDXGIFactory1(IID_PPV_ARGS(&DXGIFactory));
        if (hres != S_OK)
        {
            donut::log::info("failed to CreateDXGIFactory when finding adapters.\n");
            return;
        }

        IDXGIAdapter* pAdapter_best = nullptr;
        DXGI_ADAPTER_DESC adapterDesc_best = {};
        unsigned int adapterNo = 0;
        IDXGIAdapter* pAdapter;

        while (true)
        {
            hres = DXGIFactory->EnumAdapters(adapterNo, &pAdapter);

            if (!(hres == S_OK))
                break;



            DXGI_ADAPTER_DESC adapterDesc;
            pAdapter->GetDesc(&adapterDesc);

            adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
            adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

            log::info("Found adapter: %S, DeviceId=0x%X, Vendor: %i", adapterDesc.Description, adapterDesc.DeviceId, adapterDesc.VendorId);

            bool supported = true;
            #ifdef STREAMLINE_FEATURE_DLSS_SR
            supported &= checkFeature(sl::kFeatureDLSS, "DLSS");
            #endif
            #ifdef STREAMLINE_FEATURE_NIS
            supported &= checkFeature(sl::kFeatureNIS, "NIS");
            #endif
            #ifdef STREAMLINE_FEATURE_DLSS_FG
            supported &= checkFeature(sl::kFeatureDLSS_G, "DLSS_G");
            #endif
            #ifdef STREAMLINE_FEATURE_REFLEX
            supported &= checkFeature(sl::kFeatureReflex, "Reflex");
            #endif
            #ifdef STREAMLINE_FEATURE_DEEPDVC
            supported &= checkFeature(sl::kFeatureDeepDVC, "DeepDVC");
            #endif

            if (supported) {
                pAdapter_best = pAdapter;
                adapterDesc_best = adapterDesc;
                adapterPtr = pAdapter;
            }

            adapterNo++;

        }

        if (pAdapter_best != nullptr) {
            pAdapter_best = pAdapter;
            log::info("Using adapter: %S, DeviceId=0x%X, Vendor: %i", adapterDesc_best.Description, adapterDesc_best.DeviceId, adapterDesc_best.VendorId);
#ifdef USE_DX11
            m_d3d11Luid = adapterDesc_best.AdapterLuid;
#endif
        }
        else {
            log::info("No ideal adapter was found, we will use the default adapter.");
        }

        if (DXGIFactory)
            DXGIFactory->Release();
    }
#endif

#if USE_VK
    if (m_api == nvrhi::GraphicsAPI::VULKAN) {

        vk::PhysicalDevice* pAdapter_best = nullptr;
        vk::PhysicalDeviceProperties adapterDesc_best;
        adapterInfo = {}; // reset the adpater info

        for (auto& devicePtr : *((std::vector < vk::PhysicalDevice>*)vkDevices)) {

            adapterInfo.vkPhysicalDevice = devicePtr;

            auto adapterDesc = ((vk::PhysicalDevice)devicePtr).getProperties();
            auto str = adapterDesc.deviceName.data();
            log::info("Found adapter: %s, DeviceId=0x%X, Vendor: %i", str, adapterDesc.deviceID, adapterDesc.vendorID);

            bool supported = true;
            #ifdef STREAMLINE_FEATURE_DLSS_SR
            supported &= checkFeature(sl::kFeatureDLSS, "DLSS");
            #endif
            #ifdef STREAMLINE_FEATURE_NIS
            supported &= checkFeature(sl::kFeatureNIS, "NIS");
            #endif
            #ifdef STREAMLINE_FEATURE_DLSS_FG
            supported &= checkFeature(sl::kFeatureDLSS_G, "DLSS_G");
            #endif
            #ifdef STREAMLINE_FEATURE_REFLEX
            supported &= checkFeature(sl::kFeatureReflex, "Reflex");
            #endif
            #ifdef STREAMLINE_FEATURE_DEEPDVC
            supported &= checkFeature(sl::kFeatureDeepDVC, "DeepDVC");
            #endif

            if (supported) {
                pAdapter_best = &devicePtr;
                adapterDesc_best = adapterDesc;
            };

        }

        if (pAdapter_best != nullptr) {
            adapterPtr = pAdapter_best;
            auto str = adapterDesc_best.deviceName.data();
            log::info("Using adapter: %s, DeviceId=0x%X, Vendor: %i", str, adapterDesc_best.deviceID, adapterDesc_best.vendorID);
        }
        else {
            log::info("No ideal adapter was found, we will use the default adapter.");
        }
    }
#endif

    return;
}



sl::FeatureRequirements SLWrapper::GetFeatureRequirements(sl::Feature feature) {
    sl::FeatureRequirements req;
    slGetFeatureRequirements(feature, req);
    return req;
}

sl::FeatureVersion SLWrapper::GetFeatureVersion(sl::Feature feature) {
    sl::FeatureVersion ver;
    slGetFeatureVersion(feature, ver);
    return ver;
}

void SLWrapper::SetDevice_raw(void* device_ptr)
{
#if USE_DX11
    if (m_api == nvrhi::GraphicsAPI::D3D11)
        successCheck(slSetD3DDevice((ID3D11Device*) device_ptr), "slSetD3DDevice");
#endif

#ifdef USE_DX12
    if (m_api == nvrhi::GraphicsAPI::D3D12)
        successCheck(slSetD3DDevice((ID3D12Device*) device_ptr), "slSetD3DDevice");
#endif


}

void SLWrapper::SetDevice_nvrhi(nvrhi::IDevice* device)
{
    m_Device = device;
}

void SLWrapper::UpdateFeatureAvailable(donut::app::DeviceManager* deviceManager){

    sl::AdapterInfo adapterInfo;

#ifdef USE_DX11
    if (m_api == nvrhi::GraphicsAPI::D3D11) {
        adapterInfo.deviceLUID = (uint8_t*) &m_d3d11Luid;
        adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);
    }
#endif
#ifdef USE_DX12
    if (m_api == nvrhi::GraphicsAPI::D3D12) {
        auto a = ((ID3D12Device*)m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device))->GetAdapterLuid();
        adapterInfo.deviceLUID = (uint8_t*) &a;
        adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);
    }
#endif
#ifdef USE_VK
    if (m_api == nvrhi::GraphicsAPI::VULKAN) {
        adapterInfo.vkPhysicalDevice = m_Device->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice);
    }
#endif


    // Check if features are fully functional (2nd call of slIsFeatureSupported onwards)
#ifdef STREAMLINE_FEATURE_DLSS_SR
    m_dlss_available = successCheck(slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo), "slIsFeatureSupported_DLSS");
    if (m_dlss_available) log::info("DLSS is supported on this system.");
    else log::warning("DLSS is not fully functional on this system.");
#endif

#ifdef STREAMLINE_FEATURE_NIS
    m_nis_available = successCheck(slIsFeatureSupported(sl::kFeatureNIS, adapterInfo), "slIsFeatureSupported_NIS");
    if (m_nis_available) log::info("NIS is supported on this system.");
    else log::warning("NIS is not fully functional on this system.");
#endif

#ifdef STREAMLINE_FEATURE_DLSS_FG
    m_dlssg_available = successCheck(slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo), "slIsFeatureSupported_DLSSG");
    if (m_dlssg_available) log::info("DLSS-G is supported on this system.");
    else log::warning("DLSS-G is not fully functional on this system.");
#endif

#ifdef STREAMLINE_FEATURE_REFLEX
    m_reflex_available = successCheck(slIsFeatureSupported(sl::kFeatureReflex, adapterInfo), "slIsFeatureSupported_REFLEX");
    if (m_reflex_available) log::info("Reflex is supported on this system.");
    else log::warning("Reflex is not fully functional on this system.");

    m_pcl_available = successCheck(slIsFeatureSupported(sl::kFeaturePCL, adapterInfo), "slIsFeatureSupported_PCL");
    if (m_pcl_available) log::info("PCL is supported on this system.");
    else log::warning("PCL is not fully functional on this system.");
#endif

#ifdef STREAMLINE_FEATURE_DEEPDVC
    m_deepdvc_available = successCheck(slIsFeatureSupported(sl::kFeatureDeepDVC, adapterInfo), "slIsFeatureSupported_DeepDVC");
    if (m_deepdvc_available) log::info("DeepDVC is supported on this system.");
    else log::warning("DeepDVC is not fully functional on this system.");
#endif

    // We do not leverage the outcome of in the sample, however this is how it would be implemented.
    // sl::FeatureRequirements dlss_requirements;
    // slGetFeatureRequirements(sl::kFeatureDLSS, dlss_requirements);

    // sl::FeatureRequirements reflex_requirements;
    // slGetFeatureRequirements(sl::kFeatureReflex, reflex_requirements);

    // sl::FeatureRequirements nis_requirements;
    // slGetFeatureRequirements(sl::kFeatureNIS, nis_requirements);

    // sl::FeatureRequirements deepdvc_requirements;
    // slGetFeatureRequirements(sl::kFeatureDeepDVC, deepdvc_requirements);

    // sl::FeatureRequirements dlssg_requirements;
    // slGetFeatureRequirements(sl::kFeatureDLSS, dlssg_requirements);
}


void SLWrapper::Shutdown()
{

    // Un-set all tags
    sl::ResourceTag inputs[] = {
        sl::ResourceTag{nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent},
        sl::ResourceTag{nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent},
        sl::ResourceTag{nullptr, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent},
        sl::ResourceTag{nullptr, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent},
        sl::ResourceTag{nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent} };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), nullptr), "slSetTag_clear");

    // Shutdown Streamline
    if (m_sl_initialised) {
        successCheck(slShutdown(), "slShutdown");
        m_sl_initialised = false;
    }

}

void SLWrapper::ProxyToNative(void* proxy, void** native)
{ 
    successCheck(slGetNativeInterface(proxy, native), "slGetNativeInterface");
    assert(native != nullptr);
};

void SLWrapper::NativeToProxy(void* native, void** proxy)
{
    proxy = &native;
    successCheck(slUpgradeInterface(proxy), "slUpgradeInterface");
    assert(proxy != nullptr);
};

void SLWrapper::SetSLConsts(const sl::Constants& consts) {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }

    successCheck(slSetConstants(consts, *m_currentFrame, m_viewport), "slSetConstants");
}

void SLWrapper::FeatureLoad(sl::Feature feature, const bool turn_on) {

    if (m_api == nvrhi::GraphicsAPI::D3D12) {
        bool loaded;
        slIsFeatureLoaded(feature, loaded);
        if (loaded && !turn_on) {
            slSetFeatureLoaded(feature, turn_on);
        }
        else if (!loaded && turn_on) {
            slSetFeatureLoaded(feature, turn_on);
        }
    }
}

void SLWrapper::SetDLSSOptions(const sl::DLSSOptions consts)
{
    if (!m_sl_initialised || !m_dlss_available) {
        log::warning("SL not initialised or DLSS not available.");
        return;
    }

    m_dlss_consts = consts;
    successCheck(slDLSSSetOptions(m_viewport , m_dlss_consts), "slDLSSSetOptions");

}

void SLWrapper::QueryDLSSOptimalSettings(DLSSSettings& settings) {
    if (!m_sl_initialised || !m_dlss_available) {
        log::warning("SL not initialised or DLSS not available.");
        settings = DLSSSettings{};
        return;
    }

    sl::DLSSOptimalSettings dlssOptimal = {};
    successCheck(slDLSSGetOptimalSettings(m_dlss_consts, dlssOptimal), "slDLSSGetOptimalSettings");

    settings.optimalRenderSize.x = static_cast<int>(dlssOptimal.optimalRenderWidth);
    settings.optimalRenderSize.y = static_cast<int>(dlssOptimal.optimalRenderHeight);
    settings.sharpness = dlssOptimal.optimalSharpness;

    settings.minRenderSize.x = dlssOptimal.renderWidthMin;
    settings.minRenderSize.y = dlssOptimal.renderHeightMin;
    settings.maxRenderSize.x = dlssOptimal.renderWidthMax;
    settings.maxRenderSize.y = dlssOptimal.renderHeightMax;
}

void SLWrapper::CleanupDLSS(bool wfi) {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }

    if (wfi) {
        m_Device->waitForIdle();
    }

    sl::Result status = slFreeResources(sl::kFeatureDLSS, m_viewport);
    // if we've never ran the feature on this viewport, this call may return 'eErrorInvalidParameter'
    assert(status == sl::Result::eOk || status == sl::Result::eErrorInvalidParameter);
}

void SLWrapper::SetNISOptions(const sl::NISOptions consts)
{
    if (!m_sl_initialised || !m_nis_available) {
        log::warning("SL not initialised or DLSS not available.");
        return;
    }

    m_nis_consts = consts;
    successCheck(slNISSetOptions(m_viewport, m_nis_consts), "slNISSetOptions");

}

void SLWrapper::CleanupNIS(bool wfi) {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }

    if (wfi) {
        m_Device->waitForIdle();
    }

    successCheck(slFreeResources(sl::kFeatureNIS, m_viewport), "slFreeResources_NIS");
}


void SLWrapper::SetDeepDVCOptions(const sl::DeepDVCOptions consts)
{
    if (!m_sl_initialised || !m_deepdvc_available) {
        log::warning("SL not initialised or DeepDVC not available.");
        return;
    }

    m_deepdvc_consts = consts;
    successCheck(slDeepDVCSetOptions(m_viewport, m_deepdvc_consts), "slDeepDVCSetOptions");

}

void SLWrapper::CleanupDeepDVC() {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }
    m_Device->waitForIdle();
    successCheck(slFreeResources(sl::kFeatureDeepDVC, m_viewport), "slFreeResources_DeepDVC");
}


void SLWrapper::SetDLSSGOptions(const sl::DLSSGOptions consts) {
    if (!m_sl_initialised || !m_dlssg_available) {
        log::warning("SL not initialised or DLSSG not available.");
        return;
    }

    m_dlssg_consts = consts;

    successCheck(slDLSSGSetOptions(m_viewport, m_dlssg_consts), "slDLSSGSetOptions");
}

void SLWrapper::QueryDLSSGState(uint64_t& estimatedVRamUsage, int& fps_multiplier, sl::DLSSGStatus& status, int& minSize) {
    if (!m_sl_initialised || !m_dlssg_available) {
        log::warning("SL not initialised or DLSSG not available.");
        return;
    }

    successCheck(slDLSSGGetState(m_viewport, m_dlssg_settings, &m_dlssg_consts), "slDLSSGGetState");

    estimatedVRamUsage = m_dlssg_settings.estimatedVRAMUsageInBytes;
    fps_multiplier = m_dlssg_settings.numFramesActuallyPresented;
    status = m_dlssg_settings.status;
    minSize = m_dlssg_settings.minWidthOrHeight;
}

bool SLWrapper::Get_DLSSG_SwapChainRecreation(bool& turn_on) const {
    turn_on = m_dlssg_shoudLoad;
    auto tmp = m_dlssg_triggerswapchainRecreation;
    return tmp;
}

void SLWrapper::CleanupDLSSG(bool wfi) {
    if (!m_sl_initialised) {
        log::warning("SL not initialised.");
        return;
    }

    if (wfi) {
        m_Device->waitForIdle();
    }

    sl::Result status = slFreeResources(sl::kFeatureDLSS_G, m_viewport);
    // if we've never ran the feature on this viewport, this call may return 'eErrorInvalidParameter'
    assert(status == sl::Result::eOk || status == sl::Result::eErrorInvalidParameter);
}



sl::Resource SLWrapper::allocateResourceCallback(const sl::ResourceAllocationDesc* resDesc, void* device) {

    sl::Resource res = {};

    if (device == nullptr) {
        log::warning("No device available for allocation.");
        return res;
    }

    bool isBuffer = (resDesc->type == sl::ResourceType::eBuffer);

    if (isBuffer) {

#ifdef USE_DX11

        if (Get().m_api == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_BUFFER_DESC* desc = (D3D11_BUFFER_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = (ID3D11Device*)device;
            ID3D11Buffer* pbuffer;
            bool success = SUCCEEDED(pd3d11Device->CreateBuffer(desc, nullptr, &pbuffer));
            if (!success) log::error("Failed to create buffer in SL allocation callback");
            res.type = resDesc->type;
            res.native = pbuffer;

        }
#endif

#ifdef USE_DX12
        if (Get().m_api == nvrhi::GraphicsAPI::D3D12)
        {
            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            ID3D12Device* pd3d12Device = (ID3D12Device*)device;
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

        if (Get().m_api == nvrhi::GraphicsAPI::D3D11)
        {
            D3D11_TEXTURE2D_DESC* desc = (D3D11_TEXTURE2D_DESC*)resDesc->desc;
            ID3D11Device* pd3d11Device = (ID3D11Device*)device;
            ID3D11Texture2D* ptexture;
            bool success = SUCCEEDED(pd3d11Device->CreateTexture2D(desc, nullptr, &ptexture));
            if (!success) log::error("Failed to create texture in SL allocation callback");
            res.type = resDesc->type;
            res.native = ptexture;

        }
#endif

#ifdef USE_DX12
        if (Get().m_api == nvrhi::GraphicsAPI::D3D12)
        {
            D3D12_RESOURCE_DESC* desc = (D3D12_RESOURCE_DESC*)resDesc->desc;
            D3D12_RESOURCE_STATES state = (D3D12_RESOURCE_STATES)resDesc->state;
            D3D12_HEAP_PROPERTIES* heap = (D3D12_HEAP_PROPERTIES*)resDesc->heap;
            ID3D12Device* pd3d12Device = (ID3D12Device*)device;
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

void SLWrapper::releaseResourceCallback(sl::Resource* resource, void* device)
{
    if (resource)
    {
        auto i = (IUnknown*)resource->native;
        i->Release();
    }
};

D3D12_RESOURCE_STATES D3D12convertResourceStates(nvrhi::ResourceStates stateBits)
{
    if (stateBits == nvrhi::ResourceStates::Common)
        return D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON; // also 0

    if ((stateBits & nvrhi::ResourceStates::ConstantBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::VertexBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::IndexBuffer) != 0) result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::IndirectArgument) != 0) result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    if ((stateBits & nvrhi::ResourceStates::ShaderResource) != 0) result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if ((stateBits & nvrhi::ResourceStates::UnorderedAccess) != 0) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if ((stateBits & nvrhi::ResourceStates::RenderTarget) != 0) result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
    if ((stateBits & nvrhi::ResourceStates::DepthWrite) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if ((stateBits & nvrhi::ResourceStates::DepthRead) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_READ;
    if ((stateBits & nvrhi::ResourceStates::StreamOut) != 0) result |= D3D12_RESOURCE_STATE_STREAM_OUT;
    if ((stateBits & nvrhi::ResourceStates::CopyDest) != 0) result |= D3D12_RESOURCE_STATE_COPY_DEST;
    if ((stateBits & nvrhi::ResourceStates::CopySource) != 0) result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    if ((stateBits & nvrhi::ResourceStates::ResolveDest) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
    if ((stateBits & nvrhi::ResourceStates::ResolveSource) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    if ((stateBits & nvrhi::ResourceStates::Present) != 0) result |= D3D12_RESOURCE_STATE_PRESENT;
    if ((stateBits & nvrhi::ResourceStates::AccelStructRead) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructBuildBlas) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::ShadingRateSurface) != 0) result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

    return result;
}

void SLWrapper::TagResources_General(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView* view,
    nvrhi::ITexture* motionVectors,
    nvrhi::ITexture* depth,
    nvrhi::ITexture* finalColorHudless)
{
    if (!m_sl_initialised) {
        log::warning("Streamline not initialised.");
        return;
    }
    if (m_Device == nullptr) {
        log::error("No device available.");
        return;
    }
    
    sl::Extent renderExtent{ 0, 0, depth->getDesc().width, depth->getDesc().height };
    sl::Extent fullExtent{ 0, 0, finalColorHudless->getDesc().width, finalColorHudless->getDesc().height };
    sl::Resource motionVectorsResource, depthResource, finalColorHudlessResource;
    void* cmdbuffer;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        motionVectorsResource = sl::Resource{ sl::ResourceType::eTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        depthResource = sl::Resource{ sl::ResourceType::eTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        finalColorHudlessResource = sl::Resource{ sl::ResourceType::eTex2d, finalColorHudless->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        cmdbuffer = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        motionVectorsResource = sl::Resource{ sl::ResourceType::eTex2d, motionVectors->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(motionVectors->getDesc().initialState)) };
        depthResource = sl::Resource{ sl::ResourceType::eTex2d, depth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(depth->getDesc().initialState)) };
        finalColorHudlessResource = sl::Resource{ sl::ResourceType::eTex2d, finalColorHudless->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(finalColorHudless->getDesc().initialState)) };
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();
        auto view = (uint32_t)m_viewport;

        auto rsc_lambda = [this, &subresources, &view](sl::Resource& rsc, nvrhi::ITexture* inp) {
            auto const& desc = inp->getDesc();
            auto const& vkDesc = ((nvrhi::vulkan::Texture*)inp)->imageInfo;
            rsc = sl::Resource{ sl::ResourceType::eTex2d, inp->getNativeObject(nvrhi::ObjectTypes::VK_Image),
                inp->getNativeObject(nvrhi::ObjectTypes::VK_DeviceMemory),
                inp->getNativeView(nvrhi::ObjectTypes::VK_ImageView, desc.format, subresources),
                static_cast<uint32_t>(vkDesc.initialLayout) };
            rsc.width = desc.width;
            rsc.height = desc.height;
            rsc.nativeFormat = static_cast<uint32_t>(nvrhi::vulkan::convertFormat(desc.format));
            rsc.mipLevels = desc.mipLevels;
            rsc.arrayLayers = vkDesc.arrayLayers;
            rsc.flags = static_cast<uint32_t>(vkDesc.flags);
            rsc.usage = static_cast<uint32_t>(vkDesc.usage);
        };

        rsc_lambda(motionVectorsResource, motionVectors);
        rsc_lambda(depthResource, depth);
        rsc_lambda(finalColorHudlessResource, finalColorHudless);
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif

    sl::ResourceTag motionVectorsResourceTag = sl::ResourceTag{ &motionVectorsResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag depthResourceTag = sl::ResourceTag{ &depthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag finalColorHudlessResourceTag = sl::ResourceTag{ &finalColorHudlessResource, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

    sl::ResourceTag inputs[] = {motionVectorsResourceTag, depthResourceTag, finalColorHudlessResourceTag };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), cmdbuffer), "slSetTag_General");

}

void SLWrapper::TagResources_DLSS_NIS(
    nvrhi::ICommandList * commandList,
    const donut::engine::IView * view,
    nvrhi::ITexture * Output,
    nvrhi::ITexture * Input)
{
    if (!m_sl_initialised) {
        log::warning("Streamline not initialised.");
        return;
    }
    if (m_Device == nullptr) {
        log::error("No device available.");
        return;
    }

    sl::Extent renderExtent{ 0, 0, Input->getDesc().width, Input->getDesc().height };
    sl::Extent fullExtent{ 0, 0, Output->getDesc().width, Output->getDesc().height };
    sl::Resource outputResource, inputResource;
    void* cmdbuffer;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        outputResource = sl::Resource{ sl::ResourceType::eTex2d, Output->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        inputResource = sl::Resource{ sl::ResourceType::eTex2d, Input->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        cmdbuffer = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        outputResource = sl::Resource{ sl::ResourceType::eTex2d, Output->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(Output->getDesc().initialState)) };
        inputResource = sl::Resource{ sl::ResourceType::eTex2d, Input->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(Input->getDesc().initialState)) };
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();
        auto view = (uint32_t)m_viewport;

        auto rsc_lambda = [this, &subresources, &view](sl::Resource& rsc, nvrhi::ITexture* inp) {
            auto const& desc = inp->getDesc();
            auto const& vkDesc = ((nvrhi::vulkan::Texture*)inp)->imageInfo;
            rsc = sl::Resource{ sl::ResourceType::eTex2d, inp->getNativeObject(nvrhi::ObjectTypes::VK_Image),
                inp->getNativeObject(nvrhi::ObjectTypes::VK_DeviceMemory),
                inp->getNativeView(nvrhi::ObjectTypes::VK_ImageView, desc.format, subresources),
                static_cast<uint32_t>(vkDesc.initialLayout) };
            rsc.width = desc.width;
            rsc.height = desc.height;
            rsc.nativeFormat = static_cast<uint32_t>(nvrhi::vulkan::convertFormat(desc.format));
            rsc.mipLevels = desc.mipLevels;
            rsc.arrayLayers = vkDesc.arrayLayers;
            rsc.flags = static_cast<uint32_t>(vkDesc.flags);
            rsc.usage = static_cast<uint32_t>(vkDesc.usage);
        };

        rsc_lambda(outputResource, Output);
        rsc_lambda(inputResource, Input);
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif

    sl::ResourceTag inputResourceTag = sl::ResourceTag{ &inputResource, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag outputResourceTag = sl::ResourceTag{ &outputResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

    sl::ResourceTag inputs[] = { inputResourceTag, outputResourceTag };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), cmdbuffer), "slSetTag_dlss_nis");

}


void SLWrapper::TagResources_DLSS_FG(
    nvrhi::ICommandList* commandList,
    bool validViewportExtent,
    sl::Extent backBufferExtent)
{
    if (!m_sl_initialised) {
        log::warning("Streamline not initialised.");
        return;
    }
    if (m_Device == nullptr) {
        log::error("No device available.");
        return;
    }

    void* cmdbuffer{};

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        cmdbuffer = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif

    // tag backbuffer resource mainly to pass extent data and therefore resource can be nullptr.
    // If the viewport extent is invalid - set extent to null. This informs streamline that full resource extent needs to be used
    sl::ResourceTag backBufferResourceTag = sl::ResourceTag{ nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle{}, validViewportExtent ? &backBufferExtent : nullptr };
    sl::ResourceTag inputs[] = { backBufferResourceTag };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), cmdbuffer), "slSetTag_dlss_fg");
}

void SLWrapper::TagResources_DeepDVC(
    nvrhi::ICommandList * commandList,
    const donut::engine::IView * view,
    nvrhi::ITexture * Output)
{
    if (!m_sl_initialised) {
        log::warning("Streamline not initialised.");
        return;
    }
    if (m_Device == nullptr) {
        log::error("No device available.");
        return;
    }

    sl::Extent fullExtent{ 0, 0, Output->getDesc().width, Output->getDesc().height };
    sl::Resource outputResource;
    void* cmdbuffer;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        outputResource = sl::Resource{ sl::ResourceType::eTex2d, Output->getNativeObject(nvrhi::ObjectTypes::D3D11_Resource), 0 };
        cmdbuffer = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
    }
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        outputResource = sl::Resource{ sl::ResourceType::eTex2d, Output->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), nullptr, nullptr, static_cast<uint32_t>(D3D12convertResourceStates(Output->getDesc().initialState)) };
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();
        auto view = (uint32_t)m_viewport;

        auto rsc_lambda = [this, &subresources, &view](sl::Resource& rsc, nvrhi::ITexture* inp) {
            auto const& desc = inp->getDesc();
            auto const& vkDesc = ((nvrhi::vulkan::Texture*)inp)->imageInfo;
            rsc = sl::Resource{ sl::ResourceType::eTex2d, inp->getNativeObject(nvrhi::ObjectTypes::VK_Image),
                inp->getNativeObject(nvrhi::ObjectTypes::VK_DeviceMemory),
                inp->getNativeView(nvrhi::ObjectTypes::VK_ImageView, desc.format, subresources),
                static_cast<uint32_t>(vkDesc.initialLayout) };
            rsc.width = desc.width;
            rsc.height = desc.height;
            rsc.nativeFormat = static_cast<uint32_t>(nvrhi::vulkan::convertFormat(desc.format));
            rsc.mipLevels = desc.mipLevels;
            rsc.arrayLayers = vkDesc.arrayLayers;
            rsc.flags = static_cast<uint32_t>(vkDesc.flags);
            rsc.usage = static_cast<uint32_t>(vkDesc.usage);
            };

        rsc_lambda(outputResource, Output);
        cmdbuffer = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif
    sl::ResourceTag outputResourceTag = sl::ResourceTag{ &outputResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

    sl::ResourceTag inputs[] = { outputResourceTag };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), cmdbuffer), "slSetTag_deepdvc");
}


void SLWrapper::UnTagResources_DeepDVC()
{
    sl::ResourceTag outputResourceTag = sl::ResourceTag{ nullptr, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent };

    sl::ResourceTag inputs[] = { outputResourceTag };
    successCheck(slSetTag(m_viewport, inputs, _countof(inputs), nullptr), "slSetTag_deepdvc_untag");
}



void SLWrapper::EvaluateDLSS(nvrhi::ICommandList* commandList) {

    void* nativeCommandList = nullptr;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        nativeCommandList = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
#endif

    if (nativeCommandList == nullptr) {
        log::warning("Failed to retrieve context for DLSS evaluation.");
        return;
    }

    sl::ViewportHandle view(m_viewport);
    const sl::BaseStructure* inputs[] = { &view };
    successCheck(slEvaluateFeature(sl::kFeatureDLSS, *m_currentFrame, inputs, _countof(inputs), nativeCommandList), "slEvaluateFeature_DLSS");

    //Our pipeline is very simple so we can simply clear it, but normally state tracking should be implemented.
    commandList->clearState();

}

void SLWrapper::EvaluateNIS(nvrhi::ICommandList* commandList) {

    void* nativeCommandList = nullptr;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        nativeCommandList = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
#endif

    if (nativeCommandList == nullptr) {
        log::warning("Failed to retrieve context for NIS evaluation.");
        return;
    }

    sl::ViewportHandle view(m_viewport);
    const sl::BaseStructure* inputs[] = { &view };
    successCheck(slEvaluateFeature(sl::kFeatureNIS, *m_currentFrame, inputs, _countof(inputs), nativeCommandList), "slEvaluateFeature_NIS");

    //Our pipeline is very simple so we can simply clear it, but normally state tracking should be implemented.
    commandList->clearState();

}

void SLWrapper::EvaluateDeepDVC(nvrhi::ICommandList* commandList) {

    void* nativeCommandList = nullptr;

#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        nativeCommandList = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
#endif

#ifdef USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
#endif

#ifdef USE_VK
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
        nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
#endif

    if (nativeCommandList == nullptr) {
        log::warning("Failed to retrieve context for NIS evaluation.");
        return;
    }

    sl::ViewportHandle view(m_viewport);
    const sl::BaseStructure* inputs[] = { &view };
    successCheck(slEvaluateFeature(sl::kFeatureDeepDVC, *m_currentFrame, inputs, _countof(inputs), nativeCommandList), "slEvaluateFeature_DeepDVC");

    //Our pipeline is very simple so we can simply clear it, but normally state tracking should be implemented.
    commandList->clearState();

}

void SLWrapper::QueryDeepDVCState(uint64_t& estimatedVRamUsage)
{
    if (!m_sl_initialised || !m_deepdvc_available) {
        log::warning("SL not initialised or DeepDVC not available.");
        return;
    }
    sl::DeepDVCState state;
    successCheck(slDeepDVCGetState(m_viewport, state), "slDeepDVCGetState");
    estimatedVRamUsage = state.estimatedVRAMUsageInBytes;
}


void SLWrapper::SetReflexConsts(const sl::ReflexOptions options)
{
    if (!m_sl_initialised || !m_reflex_available)
    {
        log::warning("SL not initialised or Reflex not available.");
        return;
    }

    m_reflex_consts = options;
    successCheck(slReflexSetOptions(m_reflex_consts), "Reflex_Options");

    return;
}

void SLWrapper::Callback_FrameCount_Reflex_Sleep_Input_SimStart(donut::app::DeviceManager& manager) {

    successCheck(slGetNewFrameToken(SLWrapper::Get().m_currentFrame, nullptr), "SL_GetFrameToken");

    if (SLWrapper::Get().GetReflexAvailable()) {
        successCheck(slReflexSleep(*SLWrapper::Get().m_currentFrame), "Reflex_Sleep");
        
    }
    if (SLWrapper::Get().GetPCLAvailable()){
        successCheck(slPCLSetMarker(sl::PCLMarker::eSimulationStart, *SLWrapper::Get().m_currentFrame), "PCL_SimStart");
    }
}

void SLWrapper::ReflexCallback_SimEnd(donut::app::DeviceManager& manager) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *SLWrapper::Get().m_currentFrame), "PCL_SimEnd");
    }
}

void SLWrapper::ReflexCallback_RenderStart(donut::app::DeviceManager& manager) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *SLWrapper::Get().m_currentFrame), "PCL_SubmitStart");
    }
}

void SLWrapper::ReflexCallback_RenderEnd(donut::app::DeviceManager& manager) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *SLWrapper::Get().m_currentFrame), "PCL_SubmitEnd");
    }
}

void SLWrapper::ReflexCallback_PresentStart(donut::app::DeviceManager& manager) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::ePresentStart, *SLWrapper::Get().m_currentFrame), "PCL_PresentStart");
    }
}

void SLWrapper::ReflexCallback_PresentEnd(donut::app::DeviceManager& manager) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::ePresentEnd, *SLWrapper::Get().m_currentFrame), "PCL_PresentEnd");
    }
}

void SLWrapper::ReflexTriggerFlash(int frameNumber) {
    successCheck(slPCLSetMarker(sl::PCLMarker::eTriggerFlash, *SLWrapper::Get().m_currentFrame), "Reflex_Flash");
}

void SLWrapper::ReflexTriggerPcPing(int frameNumber) {
    if (SLWrapper::Get().GetPCLAvailable())
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::ePCLatencyPing, *SLWrapper::Get().m_currentFrame), "PCL_PCPing");
    }
}

void SLWrapper::QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats) {
    if (SLWrapper::GetReflexAvailable()) {
        sl::ReflexState state;
        successCheck(slReflexGetState(state), "Reflex_State");

        reflex_lowLatencyAvailable = state.lowLatencyAvailable;
        reflex_flashAvailable = state.flashIndicatorDriverControlled;

        auto rep = state.frameReport[63];
        if (state.latencyReportAvailable && rep.gpuRenderEndTime != 0) {

            auto frameID = rep.frameID;
            auto totalGameToRenderLatencyUs = rep.gpuRenderEndTime - rep.inputSampleTime;
            auto simDeltaUs = rep.simEndTime - rep.simStartTime;
            auto renderDeltaUs = rep.renderSubmitEndTime - rep.renderSubmitStartTime;
            auto presentDeltaUs = rep.presentEndTime - rep.presentStartTime;
            auto driverDeltaUs = rep.driverEndTime - rep.driverStartTime;
            auto osRenderQueueDeltaUs = rep.osRenderQueueEndTime - rep.osRenderQueueStartTime;
            auto gpuRenderDeltaUs = rep.gpuRenderEndTime - rep.gpuRenderStartTime;

            stats =  "frameID: " + std::to_string(frameID);
            stats += "\ntotalGameToRenderLatencyUs: " + std::to_string(totalGameToRenderLatencyUs);
            stats += "\nsimDeltaUs: " + std::to_string(simDeltaUs);
            stats += "\nrenderDeltaUs: " + std::to_string(renderDeltaUs);
            stats += "\npresentDeltaUs: " + std::to_string(presentDeltaUs);
            stats += "\ndriverDeltaUs: " + std::to_string(driverDeltaUs);
            stats += "\nosRenderQueueDeltaUs: " + std::to_string(osRenderQueueDeltaUs);
            stats += "\ngpuRenderDeltaUs: " + std::to_string(gpuRenderDeltaUs);
        }
        else {
            stats = "Latency Report Unavailable";
        }
    }

}


