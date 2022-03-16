#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::engine
{
    class FramebufferFactory;
    class ICompositeView;
    class Light;
    class PlanarView;
}

namespace donut::render
{
    enum class VolumeLightingPhaseFunctionType
    {
        ISOTROPIC,
        RAYLEIGH,
        HENYEYGREENSTEIN,
        MIE_HAZY,
        MIE_MURKY
    };

    struct VolumeLightingPhaseTerm
    {
        VolumeLightingPhaseFunctionType phaseFunction;
        dm::float3 density;
        float eccentricity;
    };

    struct VolumeLightingMediumParameters
    {
        float logScale = -4.f;
        dm::float3 absorption = dm::float3(0.596f, 1.324f, 3.310f);

        static constexpr uint32_t MAX_PHASE_TERMS = 4;
        uint32_t numPhaseTerms = 1;

        VolumeLightingPhaseTerm phaseTerms[MAX_PHASE_TERMS] = { {
            VolumeLightingPhaseFunctionType::HENYEYGREENSTEIN,
            dm::float3(1.f),
            0.85f
        } };
    };

    struct VolumeLightingLightParameters
    {
        float intensity = 200.f;
        uint32_t maxCascades = 3;
    };

    class VolumeLightingPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

        void* m_VolumeLightingContext;
        bool m_NeedClearState = true;

        // Render targets for the library on DX12 - because it doesn't create any...

        nvrhi::TextureHandle m_VlPhaseLUT;
        nvrhi::TextureHandle m_VlLightLUT_P[2];
        nvrhi::TextureHandle m_VlLightLUT_S1[2];
        nvrhi::TextureHandle m_VlLightLUT_S2[2];

        nvrhi::TextureHandle m_VlAccumulation;
        nvrhi::TextureHandle m_VlResolvedAccumulation;
        nvrhi::TextureHandle m_VlFilteredAccumulation[2];
        nvrhi::TextureHandle m_VlDepth;
        nvrhi::TextureHandle m_VlResolvedDepth;
        nvrhi::TextureHandle m_VlFilteredDepth[2];

        void CreateLibraryResources(uint32_t width, uint32_t height);

    public:
        VolumeLightingPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView
        );
        ~VolumeLightingPass();

        void BeginAccumulation(
            nvrhi::ICommandList* commandList,
            const engine::PlanarView& view,
            const VolumeLightingMediumParameters& mediumParams);

        void RenderVolume(
            nvrhi::ICommandList* commandList,
            std::shared_ptr<engine::Light> light,
            const VolumeLightingLightParameters& lightParams);

        void EndAccumulation(
            nvrhi::ICommandList* commandList);

        void ApplyLighting(
            nvrhi::ICommandList* commandList,
            const engine::PlanarView& view);

        void RenderSingleLight(
            nvrhi::ICommandList* commandList,
            const engine::ICompositeView& compositeView,
            std::shared_ptr<engine::Light> light,
            const VolumeLightingMediumParameters& mediumParams,
            const VolumeLightingLightParameters& lightParams);
    };
}