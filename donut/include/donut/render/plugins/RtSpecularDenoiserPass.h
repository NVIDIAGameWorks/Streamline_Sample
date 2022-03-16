#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>
#include <GFSDK_RTReflections.h>

namespace donut::engine
{
    class FramebufferFactory;
    class ICompositeView;
}

class GFSDK_RTReflectionFilter_Context_D3D12;
struct GFSDK_RTReflectionFilter_Parameters;
struct ID3D12DescriptorHeap;

namespace donut::render
{
    class RtSpecularDenoiserPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        GFSDK_RTReflectionFilter_Context_D3D12* m_RtReflectionFilterContextD3D12 = nullptr;
        uint32_t m_BaseSrvDescriptor;
        uint32_t m_BaseRtvDescriptor;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_SrvHeap;
        nvrhi::RefCountPtr<ID3D12DescriptorHeap> m_RtvHeap;

        void CreateRtReflectionFilterContext();
        void ReleaseRtReflectionFilterContext();

    public:
        RtSpecularDenoiserPass(nvrhi::IDevice* device);
        ~RtSpecularDenoiserPass();

        // The Camera field of params can be left uninitialised because we are able to
        // fill it in with appropriate information from the compositeView.
        void Render(
            nvrhi::ICommandList* commandList,
            const GFSDK_RTReflectionFilter_Parameters& params,
            const engine::ICompositeView& compositeView,
            const engine::ICompositeView& compositeViewPrevious,
            nvrhi::ITexture* inputDepth,
            nvrhi::ITexture* inputNormals,
            nvrhi::ITexture* inputDistance,
            GFSDK_RTReflectionFilter_Channel distanceChannel,
            nvrhi::ITexture* inputRoughness,
            GFSDK_RTReflectionFilter_Channel roughnessChannel,
            nvrhi::ITexture* inputSpecular,
            nvrhi::ITexture* outputTexture,
            GFSDK_RTReflectionFilter_RenderMask renderMask = GFSDK_RT_REFLECTION_FILTER_DENOISE,
            nvrhi::ITexture* inputMotionVectors = nullptr,   // Required if temporal filtering is used
            nvrhi::ITexture* inputPreviousNormals = nullptr, // Required if temporal filtering is used
            nvrhi::ITexture* inputPreviousDepth = nullptr    // Required if temporal filtering is used
            );
    };
}