#pragma once

#include <donut/engine/View.h>
#include <donut/engine/SceneTypes.h>
#include <donut/render/GeometryPasses.h>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
    class Light;
    class CommonRenderPasses;
    class FramebufferFactory;
    class MaterialBindingCache;
    struct Material;
    struct LightProbe;
}

namespace std
{
    template<>
    struct hash<std::pair<nvrhi::ITexture*, nvrhi::ITexture*>>
    {
        size_t operator()(const std::pair<nvrhi::ITexture*, nvrhi::ITexture*>& v) const noexcept
        {
            auto h = hash<nvrhi::ITexture*>();
            return h(v.first) ^ (h(v.second) << 8);
        }
    };
}

namespace donut::render
{
    class ForwardShadingPass : public IGeometryPass
    {
    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::InputLayoutHandle m_InputLayout;
        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_GeometryShader;
        nvrhi::SamplerHandle m_ShadowSampler;
        nvrhi::BindingLayoutHandle m_ViewBindingLayout;
        nvrhi::BindingSetHandle m_ViewBindingSet;
        nvrhi::BindingLayoutHandle m_LightBindingLayout;
        engine::ViewType::Enum m_SupportedViewTypes;
        nvrhi::BufferHandle m_ForwardViewCB;
        nvrhi::BufferHandle m_ForwardLightCB;
        nvrhi::GraphicsPipelineHandle m_PsoOpaque;
        nvrhi::GraphicsPipelineHandle m_PsoAlphaTested;
        nvrhi::GraphicsPipelineHandle m_PsoTransparent;
        bool m_TrackLiveness;

        std::unordered_map<std::pair<nvrhi::ITexture*, nvrhi::ITexture*>, nvrhi::BindingSetHandle> m_LightBindingSets;
        nvrhi::BindingSetHandle m_CurrentLightBindingSet;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::MaterialBindingCache> m_MaterialBindings;

    public:
        struct CreateParameters
        {
            std::shared_ptr<engine::MaterialBindingCache> materialBindings;
            bool enableSinglePassStereo = false;
            bool enableSinglePassCubemap = false;
            bool trackLiveness = true;
        };

    protected:
        virtual nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreateGeometryShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params);
        virtual nvrhi::BindingLayoutHandle CreateViewBindingLayout();
        virtual nvrhi::BindingSetHandle CreateViewBindingSet();
        virtual nvrhi::BindingLayoutHandle CreateLightBindingLayout();
        virtual nvrhi::BindingSetHandle CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf);
        virtual std::shared_ptr<engine::MaterialBindingCache> CreateMaterialBindingCache(engine::CommonRenderPasses& commonPasses);
        virtual nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(engine::MaterialDomain domain, const engine::IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params);
        
    public:
        ForwardShadingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses);

        virtual void Init(
            engine::ShaderFactory& shaderFactory,
            engine::FramebufferFactory& framebufferFactory,
            const engine::ICompositeView& compositeView,
            const CreateParameters& params);

        void ResetBindingCache();

        virtual void PrepareLights(nvrhi::ICommandList* commandList,
            const std::vector<std::shared_ptr<engine::Light>>& lights,
            dm::float3 ambientColorTop,
            dm::float3 ambientColorBottom,
            const std::vector<std::shared_ptr<engine::LightProbe>>& lightProbes);

        // IGeometryPass implementation

        virtual engine::ViewType::Enum GetSupportedViewTypes() const override;
        virtual void PrepareForView(nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) override;
        virtual bool SetupMaterial(const engine::Material* material, nvrhi::GraphicsState& state) override;
        virtual void SetupInputBuffers(const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) override;
    };

}
