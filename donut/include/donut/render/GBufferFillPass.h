#pragma once

#include <donut/engine/View.h>
#include <donut/engine/SceneTypes.h>
#include <donut/render/GeometryPasses.h>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class MaterialBindingCache;
    struct Material;
}

namespace donut::render
{
    class GBufferFillPass : public IGeometryPass
    {
    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::InputLayoutHandle m_InputLayout;
        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_PixelShaderAlphaTested;
        nvrhi::ShaderHandle m_GeometryShader;
        nvrhi::BindingLayoutHandle m_ViewBindingLayout;
        nvrhi::BufferHandle m_GBufferCB;
        nvrhi::GraphicsPipelineHandle m_PsoOpaque;
        nvrhi::GraphicsPipelineHandle m_PsoAlphaTested;
        nvrhi::BindingSetHandle m_ViewBindings;
        engine::ViewType::Enum m_SupportedViewTypes;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::MaterialBindingCache> m_MaterialBindings;

    public:
        struct CreateParameters
        {
            std::shared_ptr<engine::MaterialBindingCache> materialBindings;
            bool enableSinglePassStereo = false;
            bool enableSinglePassCubemap = false;
            bool enableDepthWrite = true;
            bool enableMotionVectors = false;
            bool trackLiveness = true;
            uint32_t stencilWriteMask = 0;
        };

    protected:
        virtual nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreateGeometryShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params);
        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params, engine::MaterialDomain domain);
        virtual nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params);
        virtual void CreateViewBindings(nvrhi::BindingLayoutHandle& layout, nvrhi::BindingSetHandle& set, const CreateParameters& params);
        virtual std::shared_ptr<engine::MaterialBindingCache> CreateMaterialBindingCache(engine::CommonRenderPasses& commonPasses);
        virtual nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(engine::MaterialDomain domain, const engine::IView* sampleView, nvrhi::IFramebuffer* sampleFramebuffer, const CreateParameters& params);
        
    public:
        GBufferFillPass(nvrhi::IDevice* device, std::shared_ptr<engine::CommonRenderPasses> commonPasses);

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

    class MaterialIDPass : public GBufferFillPass
    {
    protected:
        virtual nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const engine::IView* sampleView, const CreateParameters& params, engine::MaterialDomain domain) override;

    public:
        using GBufferFillPass::GBufferFillPass;
    };
}