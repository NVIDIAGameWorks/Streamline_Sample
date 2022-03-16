
#include <algorithm>
#include <sstream>

#include <donut/render/SkyPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/sky_cb.h>

using namespace donut::engine;
using namespace donut::render;

SkyPass::SkyPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView)
    : m_CommonPasses(commonPasses)
    , m_FramebufferFactory(framebufferFactory)
{
    m_PixelShader = shaderFactory->CreateShader(ShaderLocation::FRAMEWORK, "passes/sky_ps.hlsl", "main", nullptr, nvrhi::ShaderType::SHADER_PIXEL);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(SkyConstants);
    constantBufferDesc.debugName = "DeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    m_SkyCB = device->createBuffer(constantBufferDesc);

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.PS = {
            { 0, nvrhi::ResourceType::VolatileConstantBuffer }
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.PS = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_SkyCB)
        };
        m_RenderBindingSet = device->createBindingSet(bindingSetDesc, m_RenderBindingLayout);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_STRIP;
        pipelineDesc.VS = sampleView->IsReverseDepth() ? m_CommonPasses->m_FullscreenVS : m_CommonPasses->m_FullscreenAtOneVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout };

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;
        pipelineDesc.renderState.depthStencilState.depthEnable = true;
        pipelineDesc.renderState.depthStencilState.depthFunc = sampleView->IsReverseDepth() 
            ? nvrhi::DepthStencilState::COMPARISON_GREATER_EQUAL 
            : nvrhi::DepthStencilState::COMPARISON_LESS_EQUAL;
        pipelineDesc.renderState.depthStencilState.depthWriteMask = nvrhi::DepthStencilState::DEPTH_WRITE_MASK_ZERO;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_RenderPso = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);
    }
}

void SkyPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    DirectionalLight& light)
{
    commandList->beginMarker("Sky");

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        nvrhi::GraphicsState state;
        state.pipeline = m_RenderPso;
        state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
        state.bindings = { m_RenderBindingSet };
        state.viewport = view->GetViewportState();

        SkyConstants skyConstants = {};
        skyConstants.matClipToTranslatedWorld = view->GetInverseViewProjectionMatrix() * affineToHomogeneous(translation(-view->GetViewOrigin()));
        skyConstants.directionToSun = float4(normalize(-light.direction), 0.f);
        skyConstants.angularSizeOfLight = dm::radians(clamp(light.angularSize, 0.1f, 90.f));
        skyConstants.lightIntensity = light.irradiance;
        commandList->writeBuffer(m_SkyCB, &skyConstants, sizeof(skyConstants));

        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.instanceCount = 1;
        args.vertexCount = 4;
        commandList->draw(args);
    }

    commandList->endMarker();
}

namespace donut::render
{
    //-----------------------------------------------------------------------------
    // Mie + Raileigh atmospheric scattering code 
    // based on Sean O'Neil Accurate Atmospheric Scattering 
    // from GPU Gems 2 
    //-----------------------------------------------------------------------------

    float scale(float fCos, float fScaleDepth)
    {
        float x = 1.0f - fCos;
        return float(fScaleDepth * exp(-0.00287f + x * (0.459f + x * (3.83f + x * (-6.80f + x * 5.25f)))));
    }

    float atmospheric_depth(float3 position, float3 dir)
    {
        float a = dot(dir, dir);
        float b = 2 * dot(dir, position);
        float c = dot(position, position) - 1.0f;
        float det = b * b - 4 * a*c;
        float detSqrt = float(sqrt(det));
        float q = (-b - detSqrt) / 2;
        float t1 = c / q;
        return t1;
    }

    AtmosphereColors CalculateAtmosphericScattering(float3 EyeVec, float3 VecToLight, float LightIntensity, float MetersAboveGround)
    {
        AtmosphereColors output;
        static const int nSamples = 5;
        static const float fSamples = nSamples;

        //float3 fWavelength = float3(0.65f,0.57f,0.47f);		// wavelength for the red, green, and blue channels
        float3 fInvWavelength = float3(5.60f, 9.47f, 20.49f);	// 1 / pow(wavelength, 4) for the red, green, and blue channels
        float fOuterRadius = 6520000.0f;								// The outer (atmosphere) radius
        float fInnerRadius = 6400000.0f;								// The inner (planetary) radius
        float fKrESun = 0.0075f * LightIntensity;						// Kr * ESun	// initially was 0.0025 * 20.0
        float fKmESun = 0.0001f * LightIntensity;						// Km * ESun	// initially was 0.0010 * 20.0;
        float fKr4PI = 0.0075f*4.0f*3.14f;								// Kr * 4 * PI
        float fKm4PI = 0.0001f*4.0f*3.14f;								// Km * 4 * PI
        float fScale = 1.0f / (6520000.0f - 6400000.0f);					// 1 / (fOuterRadius - fInnerRadius)
        float fScaleDepth = 0.25f;									// The scale depth (i.e. the altitude at which the atmosphere's average density is found)
        float fScaleOverScaleDepth = (1.0f / (6520000.0f - 6400000.0f)) / 0.25f;	// fScale / fScaleDepth
        float G = -0.98f;												// The Mie phase asymmetry factor
        float G2 = (-0.98f)*(-0.98f);

        // Get the ray from the camera to the vertex, and its length (which is the far point of the ray passing through the atmosphere)
        float d = atmospheric_depth(float3(0, fInnerRadius / fOuterRadius, 0), EyeVec);
        float3 Ray = fOuterRadius * EyeVec*d;
        float  Far = length(Ray);
        Ray /= Far;

        // Calculate the ray's starting position, then calculate its scattering offset
        float3 Start = float3(0, fInnerRadius + MetersAboveGround, 0);
        float Height = length(Start);
        float Depth = 1.0f;
        float StartAngle = dot(Ray, Start) / Height;
        float StartOffset = Depth * scale(StartAngle, fScaleDepth);

        // Initialize the scattering loop variables
        float SampleLength = Far / fSamples;
        float ScaledLength = SampleLength * fScale;
        float3 SampleRay = Ray * SampleLength;
        float3 SamplePoint = Start + SampleRay * 0.5f;

        // Now loop through the sample points
        float3 SkyColor = 0.f;
        float3 Attenuation = 0.f;
        for (int i = 0; i < nSamples; i++)
        {
            Height = length(SamplePoint);
            Depth = float(exp(fScaleOverScaleDepth * std::min(0.f, fInnerRadius - Height)));
            float LightAngle = dot(VecToLight, SamplePoint) / Height;
            float CameraAngle = dot(Ray, SamplePoint) / Height;
            float Scatter = (StartOffset + Depth * (scale(LightAngle, fScaleDepth) - scale(CameraAngle, fScaleDepth)));
            float3 LogAttenuate = float3(-Scatter) * (fInvWavelength * fKr4PI + float3(fKm4PI));
            Attenuation.x = expf(LogAttenuate.x);
            Attenuation.y = expf(LogAttenuate.y);
            Attenuation.z = expf(LogAttenuate.z);
            SkyColor += Attenuation * (Depth * ScaledLength);
            SamplePoint += SampleRay;
        }
        float3 MieColor = SkyColor * fKmESun;
        float3 RayleighColor = SkyColor * (fInvWavelength * fKrESun);

        float fcos = -dot(VecToLight, EyeVec) / length(EyeVec);
        float fMiePhase = 1.5f * ((1.0f - G2) / (2.0f + G2)) * (1.0f + fcos * fcos) / float(pow(1.0f + G2 - 2.0f*G*fcos, 1.5f));
        output.RayleighColor = RayleighColor;
        output.MieColor = fMiePhase * MieColor;
        output.Attenuation = Attenuation;
        return output;
    }

    dm::float3 CalculateDirectionToSun(float DayOfYear, float TimeOfDay, float LatitudeDegrees)
    {
        const float AxialTilt = 23.439f;
        const float DaysInYear = 365.25f;
        const float SpringEquinoxDay = 79; // Mar 20

        float altitudeAtNoon = 90 - LatitudeDegrees + AxialTilt * sinf((DayOfYear - SpringEquinoxDay) / DaysInYear * 2 * dm::PI_f);
        altitudeAtNoon = dm::radians(altitudeAtNoon);

        float altitudeOfEarthAxis = 180 - LatitudeDegrees;
        altitudeOfEarthAxis = dm::radians(altitudeOfEarthAxis);

        // X -> North
        // Y -> Zenith
        // Z -> East

        float3 noonVector = float3(cosf(altitudeAtNoon), sinf(altitudeAtNoon), 0);
        float3 earthAxis = float3(cosf(altitudeOfEarthAxis), sinf(altitudeOfEarthAxis), 0);

        float angleFromNoon = (TimeOfDay - 12.0f) / 24.0f * 2.f * dm::PI_f;
        quat dayRotation = rotationQuat(earthAxis, -angleFromNoon);
        quat dayRotationInv = inverse(dayRotation);

        quat directionQuat = normalize(dayRotationInv * makequat(0.f, noonVector) * dayRotation);
        return float3(directionQuat.x, directionQuat.y, directionQuat.z);
    }

    DaylightInformation CalculateDaylightInformation(dm::float3 DirectionToSun, float MetersAboveGround)
    {
        DaylightInformation output;

        output.DirectionToVisibleSun = normalize(float3(DirectionToSun.x, max(0.01f, DirectionToSun.y), DirectionToSun.z));

        const float AngularSizeOfSun = 0.02f;
        float fractionAboveHorizon = DirectionToSun.y / AngularSizeOfSun; // assuming sin(x) == x for small x
        float visibleFractionOfSunArea = 0;

        if (fractionAboveHorizon >= 1) visibleFractionOfSunArea = 1;
        else if (fractionAboveHorizon <= -1) visibleFractionOfSunArea = 0;
        else {
            float beta = float(asin(fractionAboveHorizon));
            float alpha = dm::PI_f + 2 * beta;
            visibleFractionOfSunArea = 0.5f * (alpha - float(sin(alpha))) / dm::PI_f;
        }

        AtmosphereColors atmo = CalculateAtmosphericScattering(output.DirectionToVisibleSun, output.DirectionToVisibleSun, 15, MetersAboveGround);
        output.SunColor = atmo.MieColor * visibleFractionOfSunArea;

        atmo = CalculateAtmosphericScattering(float3(0, 1, 0), DirectionToSun, 15, MetersAboveGround);
        output.AmbientColor = atmo.RayleighColor;

        return output;
    }
}
