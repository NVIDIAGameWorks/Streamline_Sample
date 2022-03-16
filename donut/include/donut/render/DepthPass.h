#pragma once

#include <donut/engine/SceneTypes.h>
#include <donut/render/GeometryPasses.h>
#include <memory>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class MaterialBindingCache;
    class ICompositeView;
    class IView;
}

namespace donut::render
{
    class DepthPass : public IGeometryPass
    {
    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::InputLayoutHandle m_InputLayout;
        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BindingLayoutHandle m_ViewBindingLayout;

        nvrhi::GraphicsPipelineHandle m_PsoOpaque;
        nvrhi::GraphicsPipelineHandle m_PsoAlphaTested;
        nvrhi::BufferHandle m_DepthCB;
        nvrhi::BindingSetHandle m_ViewBindingSet;
        bool m_TrackLiveness;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::MaterialBindingCache> m_MaterialBindings;
    
    public:
        struct CreateParameters
        {
            std::shared_ptr<engine::MaterialBindingCache> materialBindings;
            nvrhi::RasterState rasterState;
            bool trackLiveness = true;
        };

    protected:
        virtual nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params);
        virtual void CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params);
        virtual std::shared_ptr<engine::MaterialBindingCache> CreateMaterialBindingCache(engine::CommonRenderPasses& commonPasses);
        virtual nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(engine::MaterialDomain domain, const engine::IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params);

    public:
        DepthPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses);

        virtual void Init(
            engine::ShaderFactory& shaderFactory,
            engine::FramebufferFactory& framebufferFactory,
            const engine::ICompositeView& compositeView,
            const CreateParameters& params);

        void ResetBindingCache();

        // IGeometryPass implementation

        virtual engine::ViewType::Enum GetSupportedViewTypes() const override;
        virtual void PrepareForView(nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) override;
        virtual bool SetupMaterial(const engine::Material* material, nvrhi::GraphicsState& state) override;
        virtual void SetupInputBuffers(const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) override;
    };

}