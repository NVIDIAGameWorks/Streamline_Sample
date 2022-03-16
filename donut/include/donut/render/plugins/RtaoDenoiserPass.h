#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class FramebufferFactory;
    class ICompositeView;
}

class GFSDK_RTAO_Context_D3D12;
struct GFSDK_RTAO_DenoiseAOParameters;
struct ID3D12DescriptorHeap;

namespace donut::render
{
    struct SsaoParameters;

    class RtaoDenoiserPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        GFSDK_RTAO_Context_D3D12* m_RtaoContextD3D12 = nullptr;
        uint32_t m_BaseSrvDescriptor;
        uint32_t m_BaseRtvDescriptor;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_SrvHeap;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_RtvHeap;

        void CreateRtaoContext();
        void ReleaseRtaoContext();

    public:
        RtaoDenoiserPass(nvrhi::IDevice* device);
        ~RtaoDenoiserPass();

        void Render(
            nvrhi::ICommandList* commandList,
            const GFSDK_RTAO_DenoiseAOParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* inputDepth,
            nvrhi::ITexture* inputNormals,
            nvrhi::ITexture* inputDistance,
            nvrhi::ITexture* inputAO,
            nvrhi::ITexture* outputTexture);
    };
}