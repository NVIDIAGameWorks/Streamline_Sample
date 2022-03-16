#pragma once

#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
    class ShadowMap;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
}

namespace donut::render
{
    class BloomPass
    {
    private:
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

        nvrhi::DeviceHandle m_Device;

        struct PerViewData
        {
            nvrhi::GraphicsPipelineHandle bloomBlurPso;
            nvrhi::GraphicsPipelineHandle bloomApplyPso;

            nvrhi::TextureHandle textureDownscale1;
            nvrhi::FramebufferHandle framebufferDownscale1;
            nvrhi::TextureHandle textureDownscale2;
            nvrhi::FramebufferHandle framebufferDownscale2;

            nvrhi::TextureHandle texturePass1Blur;
            nvrhi::FramebufferHandle framebufferPass1Blur;
            nvrhi::TextureHandle texturePass2Blur;
            nvrhi::FramebufferHandle framebufferPass2Blur;

            nvrhi::BindingSetHandle bloomBlurBindingSetPass1;
            nvrhi::BindingSetHandle bloomBlurBindingSetPass2;
            nvrhi::BindingSetHandle bloomBlurBindingSetPass3;
            nvrhi::BindingSetHandle blitFromDownscale1BindingSet;
            nvrhi::BindingSetHandle compositeBlitBindingSet;
        };

        std::vector<PerViewData> m_PerViewData;
        nvrhi::BufferHandle m_BloomHBlurCB;
        nvrhi::BufferHandle m_BloomVBlurCB;
        nvrhi::ShaderHandle m_BloomBlurPixelShader;
        nvrhi::BindingLayoutHandle m_BloomBlurBindingLayout;
        nvrhi::BindingLayoutHandle m_BloomApplyBindingLayout;

        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BlitBindingCache;

    public:
        BloomPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceDestTexture,
            float sigmaInPixels);
    };
}
