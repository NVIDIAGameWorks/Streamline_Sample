#include <stdio.h>
#include <assert.h>

#include <string>
#include <algorithm>
#include <locale>
#include <codecvt>
#include <vector>

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <windows.h>
#include <DXGI1_4.h>
#include <dxgidebug.h>

#include <nvrhi/d3d12/d3d12.h>
#include <nvrhi/validation/validation.h>

#ifndef USE_SL
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#endif


using nvrhi::RefCountPtr;

using namespace donut::app;

class DeviceManager_DX12 : public DeviceManager
{
    RefCountPtr<ID3D12Device>                   m_Device12;
    RefCountPtr<ID3D12CommandQueue>             m_DefaultQueue;
    RefCountPtr<IDXGISwapChain3>                m_SwapChain;
    DXGI_SWAP_CHAIN_DESC1                       m_SwapChainDesc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC             m_FullScreenDesc;
    RefCountPtr<IDXGIAdapter>                   m_DxgiAdapter;
    HWND                                        m_hWnd;

    std::vector<RefCountPtr<ID3D12Resource>>    m_SwapChainBuffers;
    std::vector<nvrhi::TextureHandle>           m_rhiSwapChainBuffers;
    RefCountPtr<ID3D12Fence>                    m_FrameFence;
    std::vector<HANDLE>                         m_FrameFenceEvents;

    UINT64                                      m_FrameCount = 1;

    nvrhi::DeviceHandle                         m_NvrhiDevice;

    std::string                                 m_rendererString;

public:
    virtual const char *GetRendererString() override
    {
        return m_rendererString.c_str();
    }

    virtual nvrhi::IDevice *GetDevice() override
    {
        return m_NvrhiDevice;
    }

    virtual void ReportLiveObjects() override;

    virtual nvrhi::GraphicsAPI GetGraphicsAPI() const override
    {
        return nvrhi::GraphicsAPI::D3D12;
    }

protected:
    virtual bool CreateDeviceAndSwapChain() override;
    virtual void DestroyDeviceAndSwapChain() override;
    virtual void ResizeSwapChain() override;
    virtual nvrhi::ITexture* GetCurrentBackBuffer() override;
    virtual nvrhi::ITexture* GetBackBuffer(uint32_t index) override;
    virtual uint32_t GetCurrentBackBufferIndex() override;
    virtual uint32_t GetBackBufferCount() override;
    virtual void BeginFrame() override;
    virtual void Present() override;

private:
    bool CreateRenderTargets();
    void ReleaseRenderTargets();
};

static void SetResourceBarrier(ID3D12GraphicsCommandList* commandList,
	                           ID3D12Resource* res,
	                           D3D12_RESOURCE_STATES before,
	                           D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER desc = {};
	desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	desc.Transition.pResource = res;
	desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	desc.Transition.StateBefore = before;
	desc.Transition.StateAfter = after;
	desc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	commandList->ResourceBarrier(1, &desc);
}

static bool IsNvDeviceID(UINT id)
{
    return id == 0x10DE;
}

// Find an adapter whose name contains the given string.
static RefCountPtr<IDXGIAdapter> FindAdapter(const std::wstring& targetName)
{
    RefCountPtr<IDXGIAdapter> targetAdapter;
    RefCountPtr<IDXGIFactory1> DXGIFactory;
    HRESULT hres = CreateDXGIFactory1(IID_PPV_ARGS(&DXGIFactory));
    if (hres != S_OK)
    {
        donut::log::error("ERROR in CreateDXGIFactory.\n"
            "For more info, get log from debug D3D runtime: (1) Install DX SDK, and enable Debug D3D from DX Control Panel Utility. (2) Install and start DbgView. (3) Try running the program again.\n");
        return targetAdapter;
    }
    
    unsigned int adapterNo = 0;
    while (SUCCEEDED(hres))
    {
        RefCountPtr<IDXGIAdapter> pAdapter;
        hres = DXGIFactory->EnumAdapters(adapterNo, &pAdapter);

        if (SUCCEEDED(hres))
        {
            DXGI_ADAPTER_DESC aDesc;
            pAdapter->GetDesc(&aDesc);

            // If no name is specified, return the first adapater.  This is the same behaviour as the
            // default specified for D3D11CreateDevice when no adapter is specified.
            if (targetName.length() == 0)
            {
                targetAdapter = pAdapter;
                break;
            }

            std::wstring aName = aDesc.Description;

            if (aName.find(targetName) != std::string::npos)
            {
                targetAdapter = pAdapter;
                break;
            }
        }

        adapterNo++;
    }

    return targetAdapter;
}

// Adjust window rect so that it is centred on the given adapter.  Clamps to fit if it's too big.
static bool MoveWindowOntoAdapter(IDXGIAdapter* targetAdapter, RECT& rect)
{
    assert(targetAdapter != NULL);

    HRESULT hres = S_OK;
    unsigned int outputNo = 0;
    while (SUCCEEDED(hres))
    {
        IDXGIOutput* pOutput = NULL;
        hres = targetAdapter->EnumOutputs(outputNo++, &pOutput);

        if (SUCCEEDED(hres) && pOutput)
        {
            DXGI_OUTPUT_DESC OutputDesc;
            pOutput->GetDesc(&OutputDesc);
            const RECT desktop = OutputDesc.DesktopCoordinates;
            const int centreX = (int)desktop.left + (int)(desktop.right - desktop.left) / 2;
            const int centreY = (int)desktop.top + (int)(desktop.bottom - desktop.top) / 2;
            const int winW = rect.right - rect.left;
            const int winH = rect.bottom - rect.top;
            int left = centreX - winW / 2;
            int right = left + winW;
            int top = centreY - winH / 2;
            int bottom = top + winH;
            rect.left = std::max(left, (int)desktop.left);
            rect.right = std::min(right, (int)desktop.right);
            rect.bottom = std::min(bottom, (int)desktop.bottom);
            rect.top = std::max(top, (int)desktop.top);

            // If there is more than one output, go with the first found.  Multi-monitor support could go here.
            return true;
        }
    }

    return false;
}

void DeviceManager_DX12::ReportLiveObjects()
{
    nvrhi::RefCountPtr<IDXGIDebug> pDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug));

    if (pDebug)
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
}

bool DeviceManager_DX12::CreateDeviceAndSwapChain()
{
#define HR_RETURN(hr) if(FAILED(hr)) return false;

    UINT windowStyle = m_DeviceParams.startFullscreen
        ? (WS_POPUP | WS_SYSMENU | WS_VISIBLE)
        : m_DeviceParams.startMaximized
            ? (WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE)
            : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    RECT rect = { 0, 0, LONG(m_DeviceParams.backBufferWidth), LONG(m_DeviceParams.backBufferHeight) };
    AdjustWindowRect(&rect, windowStyle, FALSE);

    RefCountPtr<IDXGIAdapter> targetAdapter;

    if (m_DeviceParams.adapter)
    {
        targetAdapter = m_DeviceParams.adapter;
    }
    else
    {
        targetAdapter = FindAdapter(m_DeviceParams.adapterNameSubstring);

        if (!targetAdapter)
        {
            std::string adapterNameStr(m_DeviceParams.adapterNameSubstring.begin(), m_DeviceParams.adapterNameSubstring.end());

            donut::log::error("Could not find an adapter matching %s\n", adapterNameStr.c_str());
            return false;
        }
    }

    {
        DXGI_ADAPTER_DESC aDesc;
        targetAdapter->GetDesc(&aDesc);

        std::wstring adapterName = aDesc.Description;
        m_rendererString = std::string(adapterName.begin(), adapterName.end());

        m_IsNvidia = IsNvDeviceID(aDesc.VendorId);
    }

    int widthBefore;
    int heightBefore;
    glfwGetWindowSize(m_Window, &widthBefore, &heightBefore);

    if (MoveWindowOntoAdapter(targetAdapter, rect))
    {
        glfwSetWindowPos(m_Window, rect.left, rect.top);
    }

    int widthAfter;
    int heightAfter;
    glfwGetWindowSize(m_Window, &widthAfter, &heightAfter);
    if ((widthBefore != widthAfter) || (heightBefore != heightAfter))
    {
        glfwSetWindowSize(m_Window, widthBefore, heightBefore);
    }

    m_hWnd = glfwGetWin32Window(m_Window);

    HRESULT hr = E_FAIL;

    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    ZeroMemory(&m_SwapChainDesc, sizeof(m_SwapChainDesc));
    m_SwapChainDesc.Width = width;
    m_SwapChainDesc.Height = height;
    m_SwapChainDesc.SampleDesc.Count = m_DeviceParams.swapChainSampleCount;
    m_SwapChainDesc.SampleDesc.Quality = 0;
    m_SwapChainDesc.BufferUsage = m_DeviceParams.swapChainUsage;
    m_SwapChainDesc.BufferCount = m_DeviceParams.swapChainBufferCount;
    m_SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    m_SwapChainDesc.Flags = m_DeviceParams.allowModeSwitch ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    // Special processing for sRGB swap chain formats.
    // DXGI will not create a swap chain with an sRGB format, but its contents will be interpreted as sRGB.
    // So we need to use a non-sRGB format here, but store the true sRGB format for later framebuffer creation.
    switch (m_DeviceParams.swapChainFormat)
    {
    case nvrhi::Format::SRGBA8_UNORM:
        m_SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case nvrhi::Format::SBGRA8_UNORM:
        m_SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        m_SwapChainDesc.Format = nvrhi::d3d12::GetFormatMapping(m_DeviceParams.swapChainFormat).srvFormat;
        break;
    }

    if (m_DeviceParams.enableDebugRuntime)
    {
        RefCountPtr<ID3D12Debug> pDebug;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));
        HR_RETURN(hr);

        pDebug->EnableDebugLayer();
    }

    RefCountPtr<IDXGIFactory2> pDxgiFactory;
    UINT dxgiFactoryFlags = m_DeviceParams.enableDebugRuntime ? DXGI_CREATE_FACTORY_DEBUG : 0;
    hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pDxgiFactory));
    HR_RETURN(hr);

    IDXGIAdapter* pAdapter = NULL;

    hr = D3D12CreateDevice(
        targetAdapter,
        m_DeviceParams.featureLevel,
        IID_PPV_ARGS(&m_Device12));
    HR_RETURN(hr);

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
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE, // necessary for GameWorks Volumetric Lighting library
                D3D12_MESSAGE_ID_COPY_DESCRIPTORS_INVALID_RANGES, // debug runtime on RS5 doesn't understand the NV driver
                D3D12_MESSAGE_ID_REFLECTSHAREDPROPERTIES_INVALIDOBJECT, // OpenVR generates these errors, but they are safe to ignore
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.pIDList = disableMessageIDs;
            filter.DenyList.NumIDs = sizeof(disableMessageIDs) / sizeof(disableMessageIDs[0]);
            pInfoQueue->AddStorageFilterEntries(&filter);
        }
    }

    m_DxgiAdapter = targetAdapter;

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    ZeroMemory(&queueDesc, sizeof(queueDesc));
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.NodeMask = 1;
    hr = m_Device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_DefaultQueue));
    HR_RETURN(hr);

    m_FullScreenDesc = {};
    m_FullScreenDesc.RefreshRate.Numerator = m_DeviceParams.refreshRate;
    m_FullScreenDesc.RefreshRate.Denominator = 1;
    m_FullScreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    m_FullScreenDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    m_FullScreenDesc.Windowed = !m_DeviceParams.startFullscreen;

    RefCountPtr<IDXGISwapChain1> pSwapChain1;
    hr = pDxgiFactory->CreateSwapChainForHwnd(m_DefaultQueue, m_hWnd, &m_SwapChainDesc, &m_FullScreenDesc, NULL, &pSwapChain1);
    HR_RETURN(hr);

	hr = pSwapChain1->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
	HR_RETURN(hr);

    m_NvrhiDevice = nvrhi::DeviceHandle::Create(new nvrhi::d3d12::Device(&DefaultMessageCallback::GetInstance(), m_Device12, m_DefaultQueue));

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        m_NvrhiDevice = nvrhi::DeviceHandle::Create(new nvrhi::validation::DeviceWrapper(m_NvrhiDevice));
    }

    if (!CreateRenderTargets())
        return false;

    hr = m_Device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFence));
    HR_RETURN(hr);

    for(UINT bufferIndex = 0; bufferIndex < m_SwapChainDesc.BufferCount; bufferIndex++)
    {
        m_FrameFenceEvents.push_back( CreateEvent(NULL, false, true, NULL) );
    }

    return true;
}

void DeviceManager_DX12::DestroyDeviceAndSwapChain()
{
    m_rhiSwapChainBuffers.clear();
    m_rendererString.clear();

    ReleaseRenderTargets();

    m_NvrhiDevice = nullptr;

    for (auto fenceEvent : m_FrameFenceEvents)
    {
        WaitForSingleObject(fenceEvent, INFINITE);
        CloseHandle(fenceEvent);
    }

    m_FrameFenceEvents.clear();

    if (m_SwapChain)
    {
        m_SwapChain->SetFullscreenState(false, nullptr);
    }

    m_SwapChainBuffers.clear();

    m_FrameFence = nullptr;
    m_SwapChain = nullptr;
    m_DefaultQueue = nullptr;
    m_Device12 = nullptr;
    m_DxgiAdapter = nullptr;
}

bool DeviceManager_DX12::CreateRenderTargets()
{
    HRESULT hr;

    m_SwapChainBuffers.resize(m_SwapChainDesc.BufferCount);
    m_rhiSwapChainBuffers.resize(m_SwapChainDesc.BufferCount);

    for(UINT n = 0; n < m_SwapChainDesc.BufferCount; n++)
    {
        hr = m_SwapChain->GetBuffer(n, IID_PPV_ARGS(&m_SwapChainBuffers[n]));
        HR_RETURN(hr);

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = m_DeviceParams.backBufferWidth;
        textureDesc.height = m_DeviceParams.backBufferHeight;
        textureDesc.sampleCount = m_DeviceParams.swapChainSampleCount;
        textureDesc.sampleQuality = m_DeviceParams.swapChainSampleQuality;
        textureDesc.format = m_DeviceParams.swapChainFormat;
        textureDesc.debugName = "SwapChainBuffer";
        textureDesc.isRenderTarget = true;
        textureDesc.isUAV = false;
        textureDesc.initialState = nvrhi::ResourceStates::PRESENT;
        textureDesc.keepInitialState = true;

        m_rhiSwapChainBuffers[n] = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(m_SwapChainBuffers[n]), textureDesc);
    }

    return true;
}

void DeviceManager_DX12::ReleaseRenderTargets()
{
    // Make sure that all frames have finished rendering
    m_NvrhiDevice->waitForIdle();

    // Set the events so that WaitForSingleObject in OneFrame will not hang later
    for(auto e : m_FrameFenceEvents)
        SetEvent(e);

    // Release the old buffers because ResizeBuffers requires that
    m_rhiSwapChainBuffers.clear();
    m_SwapChainBuffers.clear();
}

void DeviceManager_DX12::ResizeSwapChain()
{
    ReleaseRenderTargets();

    if (!m_NvrhiDevice)
        return;

    if (!m_SwapChain)
        return;

    HRESULT hr;
    hr = m_SwapChain->ResizeBuffers(m_DeviceParams.swapChainBufferCount,
                                    m_DeviceParams.backBufferWidth,
                                    m_DeviceParams.backBufferHeight,
                                    m_SwapChainDesc.Format,
                                    m_SwapChainDesc.Flags);

    if (FAILED(hr))
    {
        donut::log::fatal("ResizeBuffers failed");
    }

    bool ret = CreateRenderTargets();
    if (!ret)
    {
        donut::log::fatal("CreateRenderTarget failed");
    }
}

void DeviceManager_DX12::BeginFrame()
{
    DXGI_SWAP_CHAIN_DESC1 newSwapChainDesc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC newFullScreenDesc;
    if (SUCCEEDED(m_SwapChain->GetDesc1(&newSwapChainDesc)) && SUCCEEDED(m_SwapChain->GetFullscreenDesc(&newFullScreenDesc)))
    {
        if (m_FullScreenDesc.Windowed != newFullScreenDesc.Windowed)
        {
            BackBufferResizing();
            
            m_FullScreenDesc = newFullScreenDesc;
            m_SwapChainDesc = newSwapChainDesc;
            m_DeviceParams.backBufferWidth = newSwapChainDesc.Width;
            m_DeviceParams.backBufferHeight = newSwapChainDesc.Height;

            if(newFullScreenDesc.Windowed)
                glfwSetWindowMonitor(m_Window, nullptr, 50, 50, newSwapChainDesc.Width, newSwapChainDesc.Height, 0);

            ResizeSwapChain();
            BackBufferResized();
        }

    }

    auto bufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    WaitForSingleObject(m_FrameFenceEvents[bufferIndex], INFINITE);
}

nvrhi::ITexture* DeviceManager_DX12::GetCurrentBackBuffer()
{
    return m_rhiSwapChainBuffers[m_SwapChain->GetCurrentBackBufferIndex()];
}

nvrhi::ITexture* DeviceManager_DX12::GetBackBuffer(uint32_t index)
{
    if (index < m_rhiSwapChainBuffers.size())
        return m_rhiSwapChainBuffers[index];
    return nullptr;
}

uint32_t DeviceManager_DX12::GetCurrentBackBufferIndex()
{
    return m_SwapChain->GetCurrentBackBufferIndex();
}

uint32_t DeviceManager_DX12::GetBackBufferCount()
{
    return m_SwapChainDesc.BufferCount;
}

void DeviceManager_DX12::Present()
{
    if (!m_windowVisible)
        return;

    auto bufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    m_SwapChain->Present(m_DeviceParams.vsyncEnabled ? 1 : 0, 0);

    m_FrameFence->SetEventOnCompletion(m_FrameCount, m_FrameFenceEvents[bufferIndex]);
    m_DefaultQueue->Signal(m_FrameFence, m_FrameCount);
    m_FrameCount++;
}

DeviceManager *DeviceManager::CreateD3D12(void)
{
    return new DeviceManager_DX12();
}
