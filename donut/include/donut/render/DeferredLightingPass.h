#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>
#include <donut/core/math/math.h>

namespace donut::engine
{
    class ShaderFactory;
    class Light;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
    class IView;
    struct LightProbe;
}

namespace donut::render
{
    class DeferredLightingPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::SamplerHandle m_ShadowSampler;
        nvrhi::SamplerHandle m_ShadowSamplerComparison;
        nvrhi::BufferHandle m_DeferredLightingCB;
        nvrhi::GraphicsPipelineHandle m_Pso;
        nvrhi::BindingLayoutHandle m_GBufferBindingLayout;
        nvrhi::BindingLayoutHandle m_ShadowMapBindingLayout;
        nvrhi::BindingLayoutHandle m_LightProbeBindingLayout;

        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_ShadowMapBindingSets;
        std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_LightProbeBindingSets;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

    protected:

        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView);
        virtual nvrhi::BindingLayoutHandle CreateGBufferBindingLayout();

    public:
        DeferredLightingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses);

        virtual void Init(
            engine::ShaderFactory& shaderFactory,
            engine::FramebufferFactory& framebufferFactory,
            const engine::ICompositeView& compositeView);

        nvrhi::BindingSetHandle CreateGBufferBindingSet(
            nvrhi::TextureSubresourceSet subresources,
            nvrhi::ITexture* depth,
            nvrhi::ITexture* albedo,
            nvrhi::ITexture* specular,
            nvrhi::ITexture* normals,
            nvrhi::ITexture* indirectDiffuse = nullptr,
            nvrhi::ITexture* indirectSpecular = nullptr);

        void Render(
            nvrhi::ICommandList* commandList,
            engine::FramebufferFactory& framebufferFactory,
            const engine::ICompositeView& compositeView,
            const std::vector<std::shared_ptr<engine::Light>>& lights,
            nvrhi::IBindingSet* gbufferBindingSet,
            dm::float3 ambientColorTop,
            dm::float3 ambientColorBottom,
            const std::vector<std::shared_ptr<engine::LightProbe>>& lightProbes,
            dm::float2 randomOffset = dm::float2::zero());

        void ResetBindingCache();
    };
}