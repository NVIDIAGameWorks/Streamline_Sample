#pragma once

#include <memory>
#include <map>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
}

namespace donut::render
{
    struct ToneMappingParameters
    {
        float histogramLowPercentile = 0.8f;
        float histogramHighPercentile = 0.95f;
        float eyeAdaptationSpeedUp = 1.f;
        float eyeAdaptationSpeedDown = 0.5f;
        float minAdaptedLuminance = 0.02f;
        float maxAdaptedLuminance = 0.5f;
        float exposureBias = -0.5f;
        float whitePoint = 3.f;
        bool enableColorLUT = true;
    };

    class ToneMappingPass
    {
    private:

        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_HistogramComputeShader;
        nvrhi::ShaderHandle m_ExposureComputeShader;
        uint32_t m_HistogramBins;

        nvrhi::BufferHandle m_ToneMappingCB;
        nvrhi::BufferHandle m_HistogramBuffer;
        nvrhi::TextureHandle m_ExposureTexture;
        nvrhi::ResourceStates::Enum m_HistogramBufferState;
        nvrhi::ResourceStates::Enum m_ExposureTextureState;
        float m_FrameTime = 0.f;

        nvrhi::TextureHandle m_ColorLUT;
        float m_ColorLUTSize = 0.f;

        nvrhi::BindingLayoutHandle m_HistogramBindingLayout;
        nvrhi::ComputePipelineHandle m_HistogramPso;

        nvrhi::BindingLayoutHandle m_ExposureBindingLayout;
        nvrhi::BindingSetHandle m_ExposureBindingSet;
        nvrhi::ComputePipelineHandle m_ExposurePso;

        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::GraphicsPipelineHandle m_RenderPso;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_HistogramBindingSets;
        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_RenderBindingSets;

    public:
        ToneMappingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView,
            uint32_t histogramBins,
            bool isTextureArray,
            nvrhi::ITexture* exposureTextureOverride = nullptr,
            nvrhi::ResourceStates::Enum exposureTextureStateOverride = nvrhi::ResourceStates::COMMON,
            nvrhi::ITexture* colorLUT = nullptr);

        void Render(
            nvrhi::ICommandList* commandList,
            const ToneMappingParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture);

        void SimpleRender(
            nvrhi::ICommandList* commandList,
            const ToneMappingParameters& params,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture);

        nvrhi::TextureHandle GetExposureTexture();
        nvrhi::ResourceStates::Enum GetExposureTextureState();

        void AdvanceFrame(float frameTime);

        void ResetExposure(nvrhi::ICommandList* commandList, float initialExposure = 0.f);
        void ResetHistogram(nvrhi::ICommandList* commandList);
        void AddFrameToHistogram(nvrhi::ICommandList* commandList, const engine::ICompositeView& compositeView, nvrhi::ITexture* sourceTexture);
        void ComputeExposure(nvrhi::ICommandList* commandList, const ToneMappingParameters& params);

        void BeginTrackingState(nvrhi::ICommandList* commandList);
        void SaveCurrentState(nvrhi::ICommandList* commandList);
    };
}