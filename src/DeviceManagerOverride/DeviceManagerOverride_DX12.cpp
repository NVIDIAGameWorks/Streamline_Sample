/*
* Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
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

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/
#pragma once
#if DONUT_WITH_DX12
#include <string>
#include <algorithm>
#include <vector>

#include <donut/core/log.h>
#include <donut/app/DeviceManager_DX12.h>

#include <Windows.h>
#include <dxgi1_5.h>
#include <dxgidebug.h>

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <sstream>

#include "DeviceManagerOverride.h"

// STREAMLINE
#include "../SLWrapper.h"
using nvrhi::RefCountPtr;
using namespace donut::app;

#define HR_RETURN(hr) if(FAILED(hr)) return false;

DeviceManagerOverride_DX12::DeviceManagerOverride_DX12()
{
}

IDXGIAdapter* DeviceManagerOverride_DX12::GetAdapter()
{
    return m_DxgiAdapter;
}

bool IsNvDeviceID(UINT id)
{
    return id == 0x10DE;
}

// Find an adapter whose name contains the given string.
static IDXGIAdapter* FindAdapter(int adapterID)
{
    IDXGIFactory1* DXGIFactory;
    HRESULT hres = CreateDXGIFactory1(IID_PPV_ARGS(&DXGIFactory));
    if (hres != S_OK)
    {
        donut::log::error("ERROR in CreateDXGIFactory.\n"
            "For more info, get log from debug D3D runtime: (1) Install DX SDK, and enable Debug D3D from DX Control Panel Utility. (2) Install and start DbgView. (3) Try running the program again.\n");
        return nullptr;
    }
    
    unsigned int adapterNo = 0;
    while (SUCCEEDED(hres))
    {
        if (adapterID >= 0 && adapterNo != adapterID) continue;
        IDXGIAdapter* pAdapter;
        hres = DXGIFactory->EnumAdapters(adapterNo, &pAdapter);

        if (SUCCEEDED(hres))
        {
            DXGI_ADAPTER_DESC aDesc;
            pAdapter->GetDesc(&aDesc);

            std::wstring aName = aDesc.Description;
            std::wstring aLuid = std::to_wstring(aDesc.AdapterLuid.HighPart) + std::to_wstring(aDesc.AdapterLuid.LowPart);

            if (IsNvDeviceID(aDesc.VendorId))
            {
                return pAdapter;
            }
        }

        adapterNo++;
    }

    return nullptr;
}

bool DeviceManagerOverride_DX12::CreateDevice()
{
    if (m_DeviceParams.enableDebugRuntime)
    {
        RefCountPtr<ID3D12Debug> pDebug;
        HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));

        if (SUCCEEDED(hr))
            pDebug->EnableDebugLayer();
        else
            donut::log::warning("Cannot enable DX12 debug runtime, ID3D12Debug is not available.");
    }

    if (m_DeviceParams.enableGPUValidation)
    {
        RefCountPtr<ID3D12Debug3> debugController3;
        HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController3));

        if (SUCCEEDED(hr))
            debugController3->SetEnableGPUBasedValidation(true);
        else
            donut::log::warning("Cannot enable GPU-based validation, ID3D12Debug3 is not available.");
    }

    m_DxgiAdapter = FindAdapter(m_DeviceParams.adapterIndex);
    
    HRESULT hr = D3D12CreateDevice(
        m_DxgiAdapter,
        m_DeviceParams.featureLevel,
        IID_PPV_ARGS(&m_Device12));

    if (FAILED(hr))
    {
        donut::log::error("D3D12CreateDevice failed, error code = 0x%08x", hr);
        return false;
    }

    if (m_DeviceParams.enableDebugRuntime)
    {
        RefCountPtr<ID3D12InfoQueue> pInfoQueue;
        m_Device12->QueryInterface(&pInfoQueue);

        if (pInfoQueue)
        {
#ifdef _DEBUG
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif

            D3D12_MESSAGE_ID disableMessageIDs[] = {
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_COMMAND_LIST_STATIC_DESCRIPTOR_RESOURCE_DIMENSION_MISMATCH, // descriptor validation doesn't understand acceleration structures
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.pIDList = disableMessageIDs;
            filter.DenyList.NumIDs = sizeof(disableMessageIDs) / sizeof(disableMessageIDs[0]);
            pInfoQueue->AddStorageFilterEntries(&filter);
        }
    }

    SLWrapper::Get().ProxyToNative(m_Device12, (void**)&m_Device_native);
    SLWrapper::Get().SetDevice_raw(m_Device_native);

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    ZeroMemory(&queueDesc, sizeof(queueDesc));
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.NodeMask = 1;
    hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_GraphicsQueue));
    HR_RETURN(hr)
    m_GraphicsQueue->SetName(L"Graphics Queue");

    if (m_DeviceParams.enableComputeQueue)
    {
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_ComputeQueue));
        HR_RETURN(hr)
        m_ComputeQueue->SetName(L"Compute Queue");
    }

    if (m_DeviceParams.enableCopyQueue)
    {
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CopyQueue));
        HR_RETURN(hr)
        m_CopyQueue->SetName(L"Copy Queue");
    }

    nvrhi::d3d12::DeviceDesc deviceDesc;
    deviceDesc.errorCB = &DefaultMessageCallback::GetInstance();
    deviceDesc.pDevice = m_Device12;
    deviceDesc.pGraphicsCommandQueue = m_GraphicsQueue;
    deviceDesc.pComputeCommandQueue = m_ComputeQueue;
    deviceDesc.pCopyCommandQueue = m_CopyQueue;
#if DONUT_WITH_AFTERMATH
    deviceDesc.aftermathEnabled = m_DeviceParams.enableAftermath;
#endif

    m_NvrhiDevice = nvrhi::d3d12::createDevice(deviceDesc);

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
    }

    return true;
}

bool DeviceManagerOverride_DX12::CreateSwapChain()
{
    bool success = DeviceManager_DX12::CreateSwapChain();
    if (success)
    {
        SLWrapper::Get().ProxyToNative(m_SwapChain, (void**) &m_SwapChain_native);
    }
    return success;
}

void DeviceManagerOverride_DX12::DestroyDeviceAndSwapChain()
{
    DeviceManager_DX12::DestroyDeviceAndSwapChain();
    m_SwapChain_native = nullptr;
    m_Device_native = nullptr;
}

void DeviceManagerOverride_DX12::waitForQueue() {
    auto syncValue = ++m_FrameCount;
    m_FrameFence->SetEventOnCompletion(syncValue, m_FrameFenceEvents[0]);
    m_GraphicsQueue->Signal(m_FrameFence, syncValue);
    WaitForSingleObject(m_FrameFenceEvents[0], INFINITE);
}

bool DeviceManagerOverride_DX12::BeginFrame()
{

    bool turn_on;

    // STREAMLINE
    if (SLWrapper::Get().Get_DLSSG_SwapChainRecreation(turn_on)) {

        waitForQueue();

        SLWrapper::Get().CleanupDLSSG(true);

        // Get new sizes
        DXGI_SWAP_CHAIN_DESC1 newSwapChainDesc;
        if (SUCCEEDED(m_SwapChain_native->GetDesc1(&newSwapChainDesc))) {
            m_SwapChainDesc.Width = newSwapChainDesc.Width;
            m_SwapChainDesc.Height = newSwapChainDesc.Height;
            m_DeviceParams.backBufferWidth = newSwapChainDesc.Width;
            m_DeviceParams.backBufferHeight = newSwapChainDesc.Height;
        }

        BackBufferResizing();

        // Delete swapchain and resources
        m_SwapChain->SetFullscreenState(false, nullptr);
        ReleaseRenderTargets();

        m_SwapChain = nullptr;
        m_SwapChain_native = nullptr;

        // If we turn off dlssg, then unload dlssg featuree
        if (turn_on) 
            SLWrapper::Get().FeatureLoad(sl::kFeatureDLSS_G, true);
        else {
            SLWrapper::Get().FeatureLoad(sl::kFeatureDLSS_G, false);
        }

        m_UseProxySwapchain = turn_on;

        // Recreate Swapchain and resources 
        RefCountPtr<IDXGISwapChain1> pSwapChain1_base;
        auto hr = m_DxgiFactory2->CreateSwapChainForHwnd(m_GraphicsQueue, m_hWnd, &m_SwapChainDesc, &m_FullScreenDesc, nullptr, &pSwapChain1_base);
        if (hr != S_OK)  donut::log::fatal("CreateSwapChainForHwnd failed");
        hr = pSwapChain1_base->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
        if (hr != S_OK)  donut::log::fatal("QueryInterface failed");
        SLWrapper::Get().ProxyToNative(m_SwapChain, (void**)&m_SwapChain_native);

        if (!CreateRenderTargets()) 
            donut::log::fatal("CreateRenderTarget failed");

        BackBufferResized();

        // Reload DLSSG
        SLWrapper::Get().FeatureLoad(sl::kFeatureDLSS_G, true); 
        SLWrapper::Get().Quiet_DLSSG_SwapChainRecreation();

    } 
    else if (SLWrapper::Get().Get_Latewarp_SwapChainRecreation(turn_on)) {

        waitForQueue();

        // Get new sizes
        DXGI_SWAP_CHAIN_DESC1 newSwapChainDesc;
        if (SUCCEEDED(m_SwapChain_native->GetDesc1(&newSwapChainDesc))) {
            m_SwapChainDesc.Width = newSwapChainDesc.Width;
            m_SwapChainDesc.Height = newSwapChainDesc.Height;
            m_DeviceParams.backBufferWidth = newSwapChainDesc.Width;
            m_DeviceParams.backBufferHeight = newSwapChainDesc.Height;
        }

        BackBufferResizing();

        // Delete swapchain and resources
        m_SwapChain->SetFullscreenState(false, nullptr);
        ReleaseRenderTargets();

        m_SwapChain = nullptr;
        m_SwapChain_native = nullptr;

        // If we turn off Latewarp, then unload Latewarp feature
        if (turn_on) {
            SLWrapper::Get().FeatureLoad(sl::kFeatureLatewarp, true);
        } else {
            SLWrapper::Get().FeatureLoad(sl::kFeatureLatewarp, false);
        }

        m_UseProxySwapchain = turn_on;

        // Recreate Swapchain and resources 
        RefCountPtr<IDXGISwapChain1> pSwapChain1_base;
        auto hr = m_DxgiFactory2->CreateSwapChainForHwnd(m_GraphicsQueue, m_hWnd, &m_SwapChainDesc, &m_FullScreenDesc, nullptr, &pSwapChain1_base);
        if (hr != S_OK)  donut::log::fatal("CreateSwapChainForHwnd failed");
        hr = pSwapChain1_base->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
        if (hr != S_OK)  donut::log::fatal("QueryInterface failed");
        SLWrapper::Get().ProxyToNative(m_SwapChain, (void**)&m_SwapChain_native);

        if (!CreateRenderTargets()) 
            donut::log::fatal("CreateRenderTarget failed");

        BackBufferResized();

        // Reload Latewarp
        SLWrapper::Get().FeatureLoad(sl::kFeatureLatewarp, true); 
        SLWrapper::Get().Quiet_Latewarp_SwapChainRecreation();
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 newSwapChainDesc;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC newFullScreenDesc;
        if (SUCCEEDED(m_SwapChain->GetDesc1(&newSwapChainDesc)) && SUCCEEDED(m_SwapChain->GetFullscreenDesc(&newFullScreenDesc)))
        {
            if (m_FullScreenDesc.Windowed != newFullScreenDesc.Windowed)
            {

                waitForQueue();

                BackBufferResizing();

                m_FullScreenDesc = newFullScreenDesc;
                m_SwapChainDesc = newSwapChainDesc;
                m_DeviceParams.backBufferWidth = newSwapChainDesc.Width;
                m_DeviceParams.backBufferHeight = newSwapChainDesc.Height;

                if (newFullScreenDesc.Windowed)
                    glfwSetWindowMonitor(m_Window, nullptr, 50, 50, newSwapChainDesc.Width, newSwapChainDesc.Height, 0);

                ResizeSwapChain();
                BackBufferResized();
            }

        }

    }
    // STREAMLINE: hook function using proxy api object
    auto bufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
    return true;
}

DeviceManager* CreateD3D12()
{
    return new DeviceManagerOverride_DX12();
}
#endif