//----------------------------------------------------------------------------------
// File:        SLWrapper.h
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
#pragma once

#include <map>

// Donut
#include <donut/engine/View.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/math/basics.h>
#include <nvrhi/nvrhi.h>
#include <donut/core/log.h>

// Streamline Core
#include <sl.h>
#include <sl_consts.h>
#include <sl_hooks.h>
#include <sl_version.h>

// Streamline Features
#include <sl_dlss.h>
#include <sl_reflex.h>
#include <sl_nis.h>
#include <sl_dlss_g.h>
#include <sl_deepdvc.h>

#ifdef STREAMLINE_FEATURE_LATEWARP
#include <sl_latewarp.h>
#endif

#include "RenderTargets.h"


static constexpr int APP_ID = 231313132;

// Set this to a game's specific sdk version
static constexpr uint64_t SDK_VERSION = sl::kSDKVersion;

// We define a few functions to help with format conversion
inline sl::float2 make_sl_float2(donut::math::float2 donutF) { return sl::float2{ donutF.x, donutF.y }; }
inline sl::float3 make_sl_float3(donut::math::float3 donutF) { return sl::float3{ donutF.x, donutF.y, donutF.z }; }
inline sl::float4 make_sl_float4(donut::math::float4 donutF) {  return sl::float4{ donutF.x, donutF.y, donutF.z, donutF.w }; }
inline sl::float4x4 make_sl_float4x4(donut::math::float4x4 donutF4x4) {
    sl::float4x4 outF4x4;
    outF4x4.setRow(0, make_sl_float4(donutF4x4.row0));
    outF4x4.setRow(1, make_sl_float4(donutF4x4.row1));
    outF4x4.setRow(2, make_sl_float4(donutF4x4.row2));
    outF4x4.setRow(3, make_sl_float4(donutF4x4.row3));
    return outF4x4;
}

#ifdef STREAMLINE_MANUAL_HOOKING
struct CreateVKConfig {
    std::vector<std::string> pluginDeviceExtensions;
    std::vector<std::string> pluginInstanceExtensions;
    uint32_t extraGraphicsQueues = 0;
    uint32_t extraComputeQueues = 0;
    std::vector<std::string> features12;
    std::vector<std::string> features13;
};
#endif

void logFunctionCallback(sl::LogType type, const char* msg);

bool successCheck(sl::Result result, char* location = nullptr);


// This is a wrapper around SL functionality for DLSS. It is seperated to provide focus to the calls specific to NGX for code sample purposes.
class SLWrapper
{
private:

    SLWrapper() {}

    bool m_sl_initialised = false;
    nvrhi::GraphicsAPI m_api = nvrhi::GraphicsAPI::D3D12;
    nvrhi::IDevice* m_Device = nullptr;

#if DONUT_WITH_DX11
    LUID m_d3d11Luid;
#endif

    bool m_dlss_available = false;
    sl::DLSSOptions m_dlss_consts{};

    bool m_nis_available = false;
    sl::NISOptions m_nis_consts{};

    bool m_deepdvc_available = false;
    sl::DeepDVCOptions m_deepdvc_consts{};


    bool m_dlssg_available = false;
    bool m_dlssg_triggerswapchainRecreation = false;
    bool m_dlssg_shoudLoad = false;
    sl::DLSSGOptions m_dlssg_consts{};
    sl::DLSSGState m_dlssg_settings{};

    bool m_latewarp_available = false;
    bool m_latewarp_triggerSwapchainRecreation = false;
    bool m_latewarp_shouldLoad = false;

    bool m_reflex_available = false;
    sl::ReflexOptions m_reflex_consts{};
    bool m_reflex_driverFlashIndicatorEnable = false;
    bool m_pcl_available = false;

    static sl::Resource allocateResourceCallback(const sl::ResourceAllocationDesc* resDesc, void* device);
    static void releaseResourceCallback(sl::Resource* resource, void* device);

    sl::FrameToken* m_currentFrame;
    sl::ViewportHandle m_viewport = {0};

//    // SL Interposer Functions
//    PFun_slInit* slInit{};
//    PFun_slShutdown* slShutdown{};
//    PFun_slIsFeatureSupported* slIsFeatureSupported{};
//    PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
//    PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
//    PFun_slEvaluateFeature* slEvaluateFeature{};
//    PFun_slAllocateResources* slAllocateResources{};
//    PFun_slFreeResources* slFreeResources{};
//    PFun_slSetTag* slSetTag{};
//    PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
//    PFun_slGetFeatureVersion* slGetFeatureVersion{};
//    PFun_slUpgradeInterface* slUpgradeInterface{};
//    PFun_slSetConstants* slSetConstants{};
//    PFun_slGetNativeInterface* slGetNativeInterface{};
//    PFun_slGetFeatureFunction* slGetFeatureFunction{};
//    PFun_slGetNewFrameToken* slGetNewFrameToken{};
//    PFun_slSetD3DDevice* slSetD3DDevice{};
//
//    // DLSS specific functions
//    PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
//    PFun_slDLSSGetState* slDLSSGetState{};
//    PFun_slDLSSSetOptions* slDLSSSetOptions{};
//
//    // NIS specific functions
//    PFun_slNISSetOptions* slNISSetOptions{};
//    PFun_slNISGetState* slNISGetState{};
//
//    // Reflex specific functions
//    PFun_slReflexGetState* slReflexGetState{};
//    PFun_slReflexSetMarker* slReflexSetMarker{};
//    PFun_slReflexSleep* slReflexSleep{};
//    PFun_slReflexSetOptions* slReflexSetOptions{};
//
////    // DLSSG specific functions
//    PFun_slDLSSGGetState* slDLSSGGetState{};
//    PFun_slDLSSGSetOptions* slDLSSGSetOptions{};
//


public:

    static SLWrapper& Get();
    SLWrapper(const SLWrapper&) = delete;
    SLWrapper(SLWrapper&&) = delete;
    SLWrapper& operator=(const SLWrapper&) = delete;
    SLWrapper& operator=(SLWrapper&&) = delete;
   
    bool Initialize_preDevice(nvrhi::GraphicsAPI api, const bool& checkSig = true, const bool& SLlog = false);
    bool Initialize_postDevice();

    bool GetSLInitialized() { return m_sl_initialised; }
    void SetDevice_raw(void* device_ptr);
    void SetDevice_nvrhi(nvrhi::IDevice* device);
    void UpdateFeatureAvailable(donut::app::DeviceManager* adapter);
    void Shutdown();
    nvrhi::GraphicsAPI getAPI() { return m_api; }
    void ProxyToNative(void* proxy, void** native);
    void NativeToProxy(void* proxy, void** native);
    void FindAdapter(void*& adapterPtr, void* vkDevices = nullptr);
    static void QueueGPUWaitOnSyncObjectSet(nvrhi::IDevice* pDevice, nvrhi::CommandQueue cmdQType, void* syncObj, uint64_t syncObjVal);
#if DONUT_WITH_DX11
    LUID& getD3D11LUID() { return m_d3d11Luid; }
#endif

    sl::FeatureRequirements GetFeatureRequirements(sl::Feature feature);
    sl::FeatureVersion GetFeatureVersion(sl::Feature feature);

    void SetViewportHandle(sl::ViewportHandle vpHandle)
    {
        m_viewport = vpHandle;
    }

    void SetSLConsts(const sl::Constants& consts);
    void FeatureLoad(sl::Feature feature, const bool turn_on);
    void TagResources_General(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        nvrhi::ITexture* motionVectors,
        nvrhi::ITexture* depth,
        nvrhi::ITexture* finalColorHudless);

    void TagResources_DLSS_NIS(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        nvrhi::ITexture* output,
        nvrhi::ITexture* input);

    void TagResources_DLSS_FG(
        nvrhi::ICommandList* commandList,
        bool validViewportExtent = false,
        sl::Extent backBufferExtent = {});
    
    void TagResources_DeepDVC(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        nvrhi::ITexture* output);

    void UnTagResources_DeepDVC();

    void TagResources_Latewarp(
        nvrhi::ICommandList *commandList,
        const donut::engine::IView *view,
        nvrhi::ITexture *backbuffer,
        nvrhi::ITexture *uiColorAlpha,
        nvrhi::ITexture *noWarpMask,
        sl::Extent backBufferExtent);

    struct DLSSSettings
    {
        donut::math::int2 optimalRenderSize;
        donut::math::int2 minRenderSize;
        donut::math::int2 maxRenderSize;
        float sharpness;
    };
    void SetDLSSOptions(const sl::DLSSOptions consts);
    bool GetDLSSAvailable() { return m_dlss_available; }
    bool GetDLSSLastEnable() { return m_dlss_consts.mode != sl::DLSSMode::eOff; }
    void QueryDLSSOptimalSettings(DLSSSettings& settings);
    void EvaluateDLSS(nvrhi::ICommandList* commandList);
    void CleanupDLSS(bool wfi);

    void SetNISOptions(const sl::NISOptions consts);
    bool GetNISAvailable() { return m_nis_available; }
    bool GetNISLastEnable() { return m_nis_consts.mode != sl::NISMode::eOff; }
    void EvaluateNIS(nvrhi::ICommandList* commandList);
    void CleanupNIS(bool wfi);

    void SetDeepDVCOptions(const sl::DeepDVCOptions consts);
    bool GetDeepDVCAvailable() { return m_deepdvc_available; }
    bool GetDeepDVCLastEnable() { return m_deepdvc_consts.mode != sl::DeepDVCMode::eOff; }
    void QueryDeepDVCState(uint64_t& estimatedVRamUsage);
    void EvaluateDeepDVC(nvrhi::ICommandList* commandList);
    void CleanupDeepDVC();

    bool GetReflexAvailable() { return m_reflex_available; }
    bool GetPCLAvailable() const { return m_pcl_available; }
    static void Callback_FrameCount_Reflex_Sleep_Input_SimStart(donut::app::DeviceManager& manager);

    void ReflexTriggerFlash();
    void ReflexTriggerPcPing();
    void QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats);
    void SetReflexFlashIndicator(bool enabled) {m_reflex_driverFlashIndicatorEnable = enabled; }
    bool GetReflexFlashIndicatorEnable() { return m_reflex_driverFlashIndicatorEnable; }

    void SetReflexConsts(const sl::ReflexOptions consts);
    void ReflexCallback_Sleep(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_SimStart(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_SimEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_RenderStart(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_RenderEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_PresentStart(donut::app::DeviceManager& manager, uint32_t frameID);
    void ReflexCallback_PresentEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    sl::FrameToken *GetCurrentFrameToken() { return m_currentFrame; }

    void SetDLSSGOptions(const sl::DLSSGOptions consts);
    bool GetDLSSGAvailable() { return m_dlssg_available; }
    bool GetDLSSGLastEnable() { return m_dlssg_consts.mode != sl::DLSSGMode::eOff; }
    uint64_t GetDLSSGLastFenceValue() { return m_dlssg_settings.lastPresentInputsProcessingCompletionFenceValue; }
    void QueryDLSSGState(uint64_t& estimatedVRamUsage, int& fps_multiplier, sl::DLSSGStatus& status, int& minSize, int& framesMax, void*& pFence, uint64_t& fenceValue);
    void Set_DLSSG_SwapChainRecreation(bool on) { m_dlssg_triggerswapchainRecreation = true; m_dlssg_shoudLoad = on; }
    bool Get_DLSSG_SwapChainRecreation(bool& turn_on) const;
    void Quiet_DLSSG_SwapChainRecreation() { m_dlssg_triggerswapchainRecreation = false; }
    void CleanupDLSSG(bool wfi);

    void SetReflexCameraData(sl::FrameToken& frameToken, const sl::ReflexCameraData& cameraData);
    bool GetLatewarpAvailable() { return m_latewarp_available; }
#ifdef STREAMLINE_FEATURE_LATEWARP
    void SetLatewarpOptions(const sl::LatewarpOptions& options);
#endif
    void Set_Latewarp_SwapChainRecreation(bool on) { m_latewarp_triggerSwapchainRecreation = true; m_latewarp_shouldLoad = on; }
    bool Get_Latewarp_SwapChainRecreation(bool &turn_on) const { turn_on = m_latewarp_shouldLoad; return m_latewarp_triggerSwapchainRecreation; }
    void Quiet_Latewarp_SwapChainRecreation() { m_latewarp_triggerSwapchainRecreation = false; }
    void EvaluateLatewarp(nvrhi::ICommandList* commandList, RenderTargets* renderTargets, nvrhi::ITexture* inputColor, nvrhi::ITexture* outputColor, const donut::engine::IView* view) {}
};

