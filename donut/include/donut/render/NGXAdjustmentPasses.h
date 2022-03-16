#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
} // namespace donut::engine

namespace donut::render
{
    /*
     * Simple Image Adjustments
     *
     * Applies HDR->LDR conversion (exposure + tonemapping + sRGB conversion),
     * gamma correction, and black/white levels
     */
    class NGXAdjustmentPasses
    {
    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_PixelShader;

        nvrhi::BufferHandle m_ToneMappingCB;

        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::GraphicsPipelineHandle m_RenderPso;

        nvrhi::TextureHandle m_ExposureTexture;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

    public:
        NGXAdjustmentPasses(nvrhi::IDevice* device, std::shared_ptr<engine::ShaderFactory> shaderFactory, std::shared_ptr<engine::CommonRenderPasses> commonPasses, std::shared_ptr<engine::FramebufferFactory> framebufferFactory, const engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* exposureTexture,
            nvrhi::ITexture* sourceTexture,
            const engine::ICompositeView& compositeView,
            bool isHDR = true,
            float postExposureScale = 1.0,
            float postBlackLevel = 0.0,
            float postWhiteLevel = 1.0,
            float postGamma = 2.2);
    };
} // namespace donut::render
