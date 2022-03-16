#include <stdio.h>
#include <assert.h>

#include <string>
#include <algorithm>
#include <locale>
#include <codecvt>

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <windows.h>
#include <DXGI1_3.h>
#include <dxgidebug.h>

#include <GLFW/glfw3.h>

#include <nvrhi/d3d11/d3d11.h>
#include <nvrhi/validation/validation.h>

#ifndef USE_SL
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif


using nvrhi::RefCountPtr;

using namespace donut::app;

class DeviceManager_DX11 : public DeviceManager
{
    RefCountPtr<ID3D11Device> m_Device;
    RefCountPtr<ID3D11DeviceContext> m_ImmediateContext;
    RefCountPtr<IDXGISwapChain> m_SwapChain;
    DXGI_SWAP_CHAIN_DESC    m_SwapChainDesc;
    HWND                    m_hWnd;

    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_rhiBackBuffer;
    RefCountPtr<ID3D11Texture2D> m_D3D11BackBuffer;

    std::string                     m_rendererString = "";

public:
    virtual const char *GetRendererString() override
    {
        return m_rendererString.c_str();
    }

    virtual nvrhi::IDevice *GetDevice() override
    {
        return m_NvrhiDevice;
    }

	virtual void BeginFrame() override;

    virtual void ReportLiveObjects() override;

    virtual nvrhi::GraphicsAPI GetGraphicsAPI() const override
    {
        return nvrhi::GraphicsAPI::D3D11;
    }
protected:
    virtual bool CreateDeviceAndSwapChain() override;
    virtual void DestroyDeviceAndSwapChain() override;
    virtual void ResizeSwapChain() override;

    virtual nvrhi::ITexture* GetCurrentBackBuffer() override
    {
        return m_rhiBackBuffer;
    }

    virtual nvrhi::ITexture* GetBackBuffer(uint32_t index) override
    {
        if (index == 0)
            return m_rhiBackBuffer;

        return nullptr;
    }

    virtual uint32_t GetCurrentBackBufferIndex() override
    {
        return 0;
    }

    virtual uint32_t GetBackBufferCount() override
    {
        return 1;
    }

    virtual void Present() override;


private:
    bool CreateRenderTarget();
    void ReleaseRenderTarget();
};

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

void DeviceManager_DX11::BeginFrame()
{
	DXGI_SWAP_CHAIN_DESC newSwapChainDesc;
	if (SUCCEEDED(m_SwapChain->GetDesc(&newSwapChainDesc)))
	{
		if (m_SwapChainDesc.Windowed != newSwapChainDesc.Windowed)
		{
			BackBufferResizing();

			m_SwapChainDesc = newSwapChainDesc;
			m_DeviceParams.backBufferWidth = newSwapChainDesc.BufferDesc.Width;
			m_DeviceParams.backBufferHeight = newSwapChainDesc.BufferDesc.Height;

			if (newSwapChainDesc.Windowed)
				glfwSetWindowMonitor(m_Window, nullptr, 50, 50, newSwapChainDesc.BufferDesc.Width, newSwapChainDesc.BufferDesc.Height, 0);

			ResizeSwapChain();
			BackBufferResized();
		}

	}
}

void DeviceManager_DX11::ReportLiveObjects()
{
    nvrhi::RefCountPtr<IDXGIDebug> pDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug));

    if (pDebug)
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
}

bool DeviceManager_DX11::CreateDeviceAndSwapChain()
{
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
    m_SwapChainDesc.BufferCount = m_DeviceParams.swapChainBufferCount;
    m_SwapChainDesc.BufferDesc.Width = width;
    m_SwapChainDesc.BufferDesc.Height = height;
    m_SwapChainDesc.BufferDesc.RefreshRate.Numerator = m_DeviceParams.refreshRate;
    m_SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
    m_SwapChainDesc.BufferUsage = m_DeviceParams.swapChainUsage;
    m_SwapChainDesc.OutputWindow = m_hWnd;
    m_SwapChainDesc.SampleDesc.Count = m_DeviceParams.swapChainSampleCount;
    m_SwapChainDesc.SampleDesc.Quality = m_DeviceParams.swapChainSampleQuality;
    m_SwapChainDesc.Windowed = !m_DeviceParams.startFullscreen;
    m_SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    m_SwapChainDesc.Flags = m_DeviceParams.allowModeSwitch ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    // Special processing for sRGB swap chain formats.
    // DXGI will not create a swap chain with an sRGB format, but its contents will be interpreted as sRGB.
    // So we need to use a non-sRGB format here, but store the true sRGB format for later framebuffer creation.
    switch (m_DeviceParams.swapChainFormat)
    {
    case nvrhi::Format::SRGBA8_UNORM:
        m_SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case nvrhi::Format::SBGRA8_UNORM:
        m_SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        m_SwapChainDesc.BufferDesc.Format = nvrhi::d3d11::GetFormatMapping(m_DeviceParams.swapChainFormat).srvFormat;
        break;
    }

    UINT createFlags = 0;
    if (m_DeviceParams.enableDebugRuntime)
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;

    hr = D3D11CreateDeviceAndSwapChain(
        targetAdapter,          // pAdapter
        D3D_DRIVER_TYPE_UNKNOWN,// DriverType
        NULL,                   // Software
        createFlags,            // Flags
        &m_DeviceParams.featureLevel,   // pFeatureLevels
        1,                      // FeatureLevels
        D3D11_SDK_VERSION,      // SDKVersion
        &m_SwapChainDesc,       // pSwapChainDesc
        &m_SwapChain,           // ppSwapChain
        &m_Device,              // ppDevice
        NULL,                   // pFeatureLevel
        &m_ImmediateContext     // ppImmediateContext
    );
    
    if(FAILED(hr))
    {
        return false;
    }

    m_NvrhiDevice = nvrhi::DeviceHandle::Create(new nvrhi::d3d11::Device(&DefaultMessageCallback::GetInstance(), m_ImmediateContext));

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        m_NvrhiDevice = nvrhi::DeviceHandle::Create(new nvrhi::validation::DeviceWrapper(m_NvrhiDevice));
    }

    bool ret = CreateRenderTarget();

    if(!ret)
    {
        return false;
    }

    return true;
}

void DeviceManager_DX11::DestroyDeviceAndSwapChain()
{
    m_rhiBackBuffer = nullptr;
    m_NvrhiDevice = nullptr;

    if (m_SwapChain)
    {
        m_SwapChain->SetFullscreenState(false, nullptr);
    }

    ReleaseRenderTarget();

    m_SwapChain = nullptr;
    m_ImmediateContext = nullptr;
    m_Device = nullptr;
}

bool DeviceManager_DX11::CreateRenderTarget()
{
    ReleaseRenderTarget();

    HRESULT hr;

    hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&m_D3D11BackBuffer);
    if (FAILED(hr))
    {
        return false;
    }

    nvrhi::TextureDesc textureDesc;
    textureDesc.width = m_DeviceParams.backBufferWidth;
    textureDesc.height = m_DeviceParams.backBufferHeight;
    textureDesc.sampleCount = m_DeviceParams.swapChainSampleCount;
    textureDesc.sampleQuality = m_DeviceParams.swapChainSampleQuality;
    textureDesc.format = m_DeviceParams.swapChainFormat;
    textureDesc.debugName = "SwapChainBuffer";
    textureDesc.isRenderTarget = true;
    textureDesc.isUAV = false;

    m_rhiBackBuffer = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D11_Resource, static_cast<ID3D11Resource*>(m_D3D11BackBuffer.Get()), textureDesc);

    if (FAILED(hr))
    {
        return false;
    }

    return true;
}

void DeviceManager_DX11::ReleaseRenderTarget()
{
    m_rhiBackBuffer = nullptr;
    m_D3D11BackBuffer = nullptr;
}

void DeviceManager_DX11::ResizeSwapChain()
{
    ReleaseRenderTarget();

    if (!m_SwapChain)
        return;

    HRESULT hr;
    hr = m_SwapChain->ResizeBuffers(m_DeviceParams.swapChainBufferCount,
                                    m_DeviceParams.backBufferWidth,
                                    m_DeviceParams.backBufferHeight,
                                    m_SwapChainDesc.BufferDesc.Format,
                                    m_SwapChainDesc.Flags);

    if (FAILED(hr))
    {
        donut::log::fatal("ResizeBuffers failed");
    }

    bool ret = CreateRenderTarget();
    if (!ret)
    {
        donut::log::fatal("CreateRenderTarget failed");
    }
}

void DeviceManager_DX11::Present()
{
    m_SwapChain->Present(m_DeviceParams.vsyncEnabled ? 1 : 0, 0);
}

DeviceManager *DeviceManager::CreateD3D11(void)
{
    return new DeviceManager_DX11();
}
