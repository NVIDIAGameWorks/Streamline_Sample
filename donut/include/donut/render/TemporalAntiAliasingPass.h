#pragma once


#include <donut/core/math/math.h>
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
    struct TemporalAntiAliasingParameters
    {
        float newFrameWeight = 0.1f;
        float clampingFactor = 1.0f;
        bool enableHistoryClamping = true;
    };

    class TemporalAntiAliasingPass
    {
    private:
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

        nvrhi::ShaderHandle m_MotionVectorPS;
        nvrhi::ShaderHandle m_TemporalAntiAliasingCS;
        nvrhi::SamplerHandle m_BilinearSampler;
        nvrhi::BufferHandle m_TemporalAntiAliasingCB;

        nvrhi::BindingLayoutHandle m_MotionVectorsBindingLayout;
        nvrhi::BindingSetHandle m_MotionVectorsBindingSet;
        nvrhi::GraphicsPipelineHandle m_MotionVectorsPso;
        std::unique_ptr<engine::FramebufferFactory> m_MotionVectorsFramebufferFactory;

        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BindingSetHandle m_ResolveBindingSet;
        nvrhi::BindingSetHandle m_ResolveBindingSetPrevious;
        nvrhi::ComputePipelineHandle m_ResolvePso;

        uint32_t m_FrameIndex;
        uint32_t m_StencilMask;
        dm::float2 m_ResolvedColorSize;

    public:
        TemporalAntiAliasingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceDepth,
            nvrhi::ITexture* motionVectors,
            nvrhi::ITexture* unresolvedColor,
            nvrhi::ITexture* resolvedColor,
            nvrhi::ITexture* feedback1,
            nvrhi::ITexture* feedback2,
            uint32_t motionVectorStencilMask = 0,
            bool useCatmullRomFilter = true);

        void RenderMotionVectors(
            nvrhi::ICommandList* commandList,
            const engine::ICompositeView& compositeView,
            const engine::ICompositeView& compositeViewPrevious);

        void TemporalResolve(
            nvrhi::ICommandList* commandList,
            const TemporalAntiAliasingParameters& params,
            bool feedbackIsValid,
            const engine::ICompositeView& compositeView,
            const engine::ICompositeView& compositeViewPrevious);

        void AdvanceFrame();
        dm::float2 GetCurrentPixelOffset();
    };
}
