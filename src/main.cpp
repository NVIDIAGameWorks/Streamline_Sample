//----------------------------------------------------------------------------------
// File:        main.cpp
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


#include <string>
#include <vector>
#include <memory>
#include <chrono>

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ConsoleInterpreter.h>
#include <donut/engine/ConsoleObjects.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/BloomPass.h>
#include <donut/render/CascadedShadowMap.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/LightProbeProcessingPass.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/render/SkyPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/imgui_console.h>
#include <donut/app/imgui_renderer.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#include "SLWrapper.h"
#include "RenderTargets.h"
#include "StreamlineSample.h"
#include "UIRenderer.h"
#include "UIData.h"

#include "DeviceManagerOverride/DeviceManagerOverride.h"

std::ofstream log_file;
void logToFile(donut::log::Severity s, char const* txt) {
    if (log_file.is_open()) {
        auto str = std::string(txt);
        log_file << str.substr(0, str.size() - 1).c_str() << std::endl;
    }
};

bool ProcessCommandLine(int argc, const char* const* argv, donut::app::DeviceCreationParameters& deviceParams, std::string& sceneName, bool& checkSig, bool& enableSLlog)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-width"))
        {
            deviceParams.backBufferWidth = std::stoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-height"))
        {
            deviceParams.backBufferHeight = std::stoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-fullscreen"))
        {
            deviceParams.startFullscreen = true;
        }
        else if (!strcmp(argv[i], "-debug"))
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (!strcmp(argv[i], "-verbose"))
        {
            donut::log::SetMinSeverity(donut::log::Severity::Info);
        }
        else if (!strcmp(argv[i], "-logToFile"))
        {
            log_file = std::ofstream("log.txt");
            donut::log::SetCallback(&logToFile);
        }
        else if (!strcmp(argv[i], "-noSigCheck"))
        {
            checkSig = false;
        }
        else if (!strcmp(argv[i], "-vsync"))
        {
            deviceParams.vsyncEnabled = true;
        }
        else if (!strcmp(argv[i], "-sllog"))
        {
            enableSLlog = true;
        }
        else if (!strcmp(argv[i], "-scene"))
        {
            sceneName = argv[i];
        }

    }

    return true;
}

donut::app::DeviceManager* CreateDeviceManager(nvrhi::GraphicsAPI api)
{
    switch (api)
    {
#if USE_DX11
    case nvrhi::GraphicsAPI::D3D11:
        return CreateD3D11();
#endif
#if USE_DX12
    case nvrhi::GraphicsAPI::D3D12:
        return CreateD3D12();
#endif
#if USE_VK
    case nvrhi::GraphicsAPI::VULKAN:
        return CreateVK();
#endif
    default:
        donut::log::error("DeviceManager::Create: Unsupported Graphics API (%d)", api);
        return nullptr;
    }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    nvrhi::GraphicsAPI api = donut::app::GetGraphicsAPIFromCommandLine(__argc, __argv);
#else //  _WIN32
int main(int __argc, const char* const* __argv)
{
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
#endif //  _WIN32

    donut::app::DeviceCreationParameters deviceParams;

    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.startFullscreen = false;
    deviceParams.vsyncEnabled = false;
    deviceParams.swapChainFormat = nvrhi::Format::BGRA8_UNORM;

    std::string sceneName;
    bool checkSig = true;
    bool SLlog = false;
    if (!ProcessCommandLine(__argc, __argv, deviceParams, sceneName, checkSig, SLlog))
    {
        donut::log::error("Failed to process the command line.");
        return 1;
    }

    auto scripting = ScriptingConfig(__argc, __argv);

#ifdef _DEBUG
    checkSig = false;
#endif

    // Initialise Streamline before creating the device and swapchain.
    auto success = SLWrapper::Get().Initialize_preDevice(api, checkSig, SLlog);

#if USE_DX11 || USE_DX12
    // We choose the best adapter that supports as many of our features as possible. This will be used for device creation.
    if (api == nvrhi::GraphicsAPI::D3D11 || api == nvrhi::GraphicsAPI::D3D12)
        SLWrapper::Get().FindAdapter((void*&) (deviceParams.adapter));
#endif

    if (!success)
        return 0;

    DeviceManager* deviceManager = CreateDeviceManager(api);

    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "Streamline Sample (" + std::string(apiString) + ")";

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        donut::log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
        return 1;
    }

    SLWrapper::Get().SetDevice_nvrhi(deviceManager->GetDevice());

    SLWrapper::Get().Initialize_postDevice();

    SLWrapper::Get().UpdateFeatureAvailable(deviceManager);

    {
        UIData uiData;

        uiData.EnableVsync = deviceParams.vsyncEnabled;
        uiData.Resolution = donut::math::int2{ (int)deviceParams.backBufferWidth, (int)deviceParams.backBufferHeight };

        std::shared_ptr<StreamlineSample> demo = std::make_shared<StreamlineSample>(deviceManager, uiData, sceneName, scripting);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, demo, uiData);

        gui->Init(demo->GetShaderFactory());

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
    }

    // Most "real" apps have significantly more work to do between stopping the rendering loop and shutting down
    // SL.  Here, simulate that time as a WAR.
    Sleep(100);

    // Shut down Streamline before destroying swapchain and device.
    SLWrapper::Get().Shutdown();

    deviceManager->Shutdown();
#ifdef _DEBUG
    deviceManager->ReportLiveObjects();
#endif

    delete deviceManager;

    return 0;
}