#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class FramebufferFactory;
    class ICompositeView;
}

#if USE_DX11
class GFSDK_SSAO_Context_D3D11;
#endif
#if USE_DX12
class GFSDK_SSAO_Context_D3D12;
struct ID3D12DescriptorHeap;
#endif

namespace donut::render
{
    struct SsaoParameters;

    class HbaoPlusPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
#if USE_DX11
        GFSDK_SSAO_Context_D3D11* m_HbaoContextD3D11 = nullptr;
#endif
#if USE_DX12
        GFSDK_SSAO_Context_D3D12* m_HbaoContextD3D12 = nullptr;
        uint32_t m_BaseSrvDescriptor;
        uint32_t m_BaseRtvDescriptor;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_SrvHeap;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_RtvHeap;
#endif

        void CreateHbaoPlusContext();
        void ReleaseHbaoPlusContext();

    public:
        HbaoPlusPass(nvrhi::IDevice* device);
        ~HbaoPlusPass();

        bool CanRender();

        void Render(
            nvrhi::ICommandList* commandList,
            const SsaoParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* inputDepth,
            nvrhi::ITexture* inputNormals,
            nvrhi::ITexture* outputTexture);
    };
}