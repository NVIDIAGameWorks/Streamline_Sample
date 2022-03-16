
#include <donut/app/vr/VrSystemNvrhi.h>
#include <donut/engine/CommonRenderPasses.h>
#include <D3D12.h>
#include <dxgi.h>

using namespace donut::math;
using namespace donut::app;
using namespace donut::engine;

VrResult VrSystemNvrhi::Create(nvrhi::IDevice* device, std::shared_ptr<CommonRenderPasses> commonPasses, VrSystemNvrhi** ppSystem)
{
    *ppSystem = nullptr;
    VrSystem* internalSystem = nullptr;
    VrResult result = VrResult::OtherError;

    switch (device->getGraphicsAPI())
    {
    case nvrhi::GraphicsAPI::D3D11: {
        const auto pD3D11Device = static_cast<ID3D11Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D11_Device));

        result = VrSystem::CreateD3D11(pD3D11Device, &internalSystem);

        break;
    }
    case nvrhi::GraphicsAPI::D3D12: {
        const auto pD3D12Device = static_cast<ID3D12Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
        LUID adapterLuid = pD3D12Device->GetAdapterLuid();

        const auto pD3D12Queue = static_cast<ID3D12CommandQueue*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_CommandQueue));

        result = VrSystem::CreateD3D12(&adapterLuid, pD3D12Queue, &internalSystem);

        break;
    }
    default:
        assert(!"Unsupported graphics API");
    }

    if (result != VrResult::OK)
        return result;

    VrSystemNvrhi* pVrSystemNvrhi = new VrSystemNvrhi();
    pVrSystemNvrhi->m_vrSystem = std::unique_ptr<VrSystem>(internalSystem);
    pVrSystemNvrhi->m_Device = device;
    pVrSystemNvrhi->m_CommandList = device->createCommandList();
    pVrSystemNvrhi->m_CommonPasses = commonPasses;
    pVrSystemNvrhi->CreateSwapChainTextures();


    *ppSystem = pVrSystemNvrhi;
    return VrResult::OK;
}

void VrSystemNvrhi::CreateSwapChainTextures()
{
    switch (m_vrSystem->GetApi())
    {
    case VrApi::OculusVR:
    {
        int swapLength = m_vrSystem->GetSwapChainBufferCount();
        m_SwapTextures.resize(swapLength);

        nvrhi::TextureDesc desc;
        int2 swapChainSize = m_vrSystem->GetSwapChainSize();
        desc.width = swapChainSize.x;
        desc.height = swapChainSize.y;
        desc.format = nvrhi::Format::SRGBA8_UNORM;
        desc.debugName = "Oculus VR Back Buffer";
        desc.isRenderTarget = true;
        desc.initialState = nvrhi::ResourceStates::RENDER_TARGET;
        desc.keepInitialState = true;

        for (int nTexture = 0; nTexture < swapLength; nTexture++)
        {
            if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
            {
                ID3D11Texture2D* swapTex = m_vrSystem->GetSwapChainBufferD3D11(nTexture);

                m_SwapTextures[nTexture] = m_Device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D11_Resource, swapTex, desc);
            }
            else if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
            {
                ID3D12Resource* swapTex = m_vrSystem->GetSwapChainBufferD3D12(nTexture);

                m_SwapTextures[nTexture] = m_Device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, swapTex, desc);
            }

            nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(m_SwapTextures[nTexture], 0, 0, nvrhi::Format::SRGBA8_UNORM));
            m_SwapFramebuffers.push_back(framebuffer);
        }
        break;
    }
    }
}

VrApi VrSystemNvrhi::GetApi() const
{
    return m_vrSystem->GetApi();
}

VrResult VrSystemNvrhi::AcquirePose() const
{
    return m_vrSystem->AcquirePose();
}

VrResult VrSystemNvrhi::Present(
    nvrhi::ITexture* sourceTexture,
    nvrhi::ResourceStates::Enum preTextureState,
    nvrhi::ResourceStates::Enum postTextureState)
{
    int currentIndex = m_vrSystem->GetCurrentSwapChainBuffer();
    nvrhi::IFramebuffer* framebuffer = m_SwapFramebuffers[currentIndex];
    nvrhi::ITexture* swapTexture = m_SwapTextures[currentIndex];

    int2 swapChainSize = m_vrSystem->GetSwapChainSize();
    m_CommandList->open();

    m_CommandList->beginTrackingTextureState(sourceTexture, nvrhi::AllSubresources, preTextureState);
    m_CommonPasses->BlitTexture(m_CommandList, framebuffer, nvrhi::Viewport(float(swapChainSize.x), float(swapChainSize.y)), sourceTexture);

    m_CommandList->endTrackingTextureState(sourceTexture, nvrhi::AllSubresources, postTextureState);

    m_CommandList->close();
    m_Device->executeCommandList(m_CommandList);

    switch (m_vrSystem->GetApi())
    {
    case VrApi::OculusVR:
        return m_vrSystem->PresentOculusVR();
    default:
        assert(!"Unsupported VR API");
        return VrResult::OtherError;
    }
}

void VrSystemNvrhi::SetOculusPerfHudMode(int mode) const
{
    m_vrSystem->SetOculusPerfHudMode(mode);
}

void VrSystemNvrhi::Recenter() const
{
    m_vrSystem->Recenter();
}

int2 VrSystemNvrhi::GetSwapChainSize() const
{
    return m_vrSystem->GetSwapChainSize();
}

float4x4 VrSystemNvrhi::GetProjectionMatrix(int eye, float zNear, float zFar) const
{
    return m_vrSystem->GetProjectionMatrix(eye, zNear, zFar);
}

float4x4 VrSystemNvrhi::GetReverseProjectionMatrix(int eye, float zNear) const
{
    return m_vrSystem->GetReverseProjectionMatrix(eye, zNear);
}

affine3 VrSystemNvrhi::GetEyeToOriginTransform(int eye) const
{
    return m_vrSystem->GetEyeToOriginTransform(eye);
}
