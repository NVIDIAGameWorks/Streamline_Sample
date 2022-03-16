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
    class DirectionalLight;
}

namespace donut::render
{
    typedef struct
    {
        dm::float3 RayleighColor;
        dm::float3 MieColor;
        dm::float3 Attenuation;
    } AtmosphereColors;

    typedef struct
    {
        dm::float3 DirectionToVisibleSun;
        dm::float3 SunColor;
        dm::float3 AmbientColor;
    } DaylightInformation;

    AtmosphereColors CalculateAtmosphericScattering(dm::float3 EyeVec, dm::float3 VecToLight, float LightIntensity, float MetersAboveGround = 0);
    DaylightInformation CalculateDaylightInformation(dm::float3 DirectionToSun, float MetersAboveGround = 0);
    dm::float3 CalculateDirectionToSun(float DayOfYear, float TimeOfDay, float Latitude);

    class SkyPass
    {
    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_SkyCB;
        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::BindingSetHandle m_RenderBindingSet;
        nvrhi::GraphicsPipelineHandle m_RenderPso;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;

    public:
        SkyPass(
            nvrhi::IDevice* device,
            std::shared_ptr<engine::ShaderFactory> shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            const engine::ICompositeView& compositeView,
            engine::DirectionalLight& light);
    };
}