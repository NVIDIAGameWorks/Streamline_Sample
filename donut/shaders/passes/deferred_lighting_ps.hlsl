
#pragma pack_matrix(row_major)

#include <donut/shaders/surface.hlsli>
#include <donut/shaders/lighting.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/deferred_lighting_cb.h>

cbuffer c_Deferred : register(b0 DESCRIPTOR_SET_1)
{
    DeferredLightingConstants g_Deferred;
};

Texture2DArray t_ShadowMapArray : register(t0 DESCRIPTOR_SET_1);
TextureCubeArray t_DiffuseLightProbe : register(t1 DESCRIPTOR_SET_2);
TextureCubeArray t_SpecularLightProbe : register(t2 DESCRIPTOR_SET_2);
Texture2D t_EnvironmentBrdf : register(t3 DESCRIPTOR_SET_2);

SamplerState s_ShadowSampler : register(s0 DESCRIPTOR_SET_1);
SamplerComparisonState s_ShadowSamplerComparison : register(s1 DESCRIPTOR_SET_1);
SamplerState s_LightProbeSampler : register(s2 DESCRIPTOR_SET_2);
SamplerState s_BrdfSampler : register(s3 DESCRIPTOR_SET_2);

Texture2D t_GBufferDepth : register(t8);
Texture2D t_GBuffer0 : register(t9);
Texture2D t_GBuffer1 : register(t10);
Texture2D t_GBuffer2 : register(t11);
Texture2D t_IndirectDiffuse : register(t12);
Texture2D t_IndirectSpecular : register(t13);

SurfaceParams GetSurfaceParams(int2 pixelPosition, float2 uv)
{
    SurfaceParams surface = DefaultSurfaceParams();

    float gbufferDepth = t_GBufferDepth[pixelPosition].x;
    surface.clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, gbufferDepth, 1);

    float4 channel0 = t_GBuffer0[pixelPosition];
    float4 channel1 = t_GBuffer1[pixelPosition];
    float4 channel2 = t_GBuffer2[pixelPosition];

    surface.diffuseColor = channel0.xyz;
    surface.opacity = channel0.w;
    if (channel1.w == 0)
        surface.specularColor = channel1.xyz;
    else
        surface.emissiveColor = channel1.xyz;
    surface.normal = channel2.xyz;
    surface.roughness = channel2.w;
    surface.geometryNormal = surface.normal;

    return surface;
}

float3 GetIncidentVector(float4 directionOrPosition, float3 surfacePos)
{
    if (directionOrPosition.w > 0)
        return normalize(surfacePos.xyz - directionOrPosition.xyz);
    else
        return directionOrPosition.xyz;
}

float GetRandom(float2 pos)
{
    int x = int(pos.x) & 3;
    int y = int(pos.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0
)
{
    SurfaceParams surface = GetSurfaceParams(int2(i_position.xy), i_uv);

    float4 viewPos = mul(surface.clipPos, g_Deferred.view.matClipToView);
    viewPos.xyz /= viewPos.w;
    viewPos.w = 1;

    float4 worldPos = mul(viewPos, g_Deferred.view.matViewToWorld);
    surface.worldPos = worldPos.xyz;

    float3 viewIncident = GetIncidentVector(g_Deferred.view.cameraDirectionOrPosition, surface.worldPos);

    float3 diffuseTerm = 0;
    float3 specularTerm = 0;
    float angle = GetRandom(i_position.xy + g_Deferred.randomOffset);
    float2 sincos = float2(sin(angle), cos(angle));

    [loop]
    for (uint nLight = 0; nLight < g_Deferred.numLights; nLight++)
    {
        LightConstants light = g_Deferred.lights[nLight];

        float2 shadow = 0;

        [loop]
        for (int cascade = 0; cascade < 4; cascade++)
        {
            if (light.shadowCascades[cascade] >= 0)
            {
                float2 cascadeShadow = EvaluateShadowPoisson(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.shadowCascades[cascade]], surface.worldPos, sincos, 3.0);
                //float2 cascadeShadow = EvaluateShadowGather16(t_ShadowMapArray, s_ShadowSampler, g_Deferred.shadows[light.shadowCascades[cascade]], surface.worldPos, g_Deferred.shadowMapTextureSize);
                //float2 cascadeShadow = EvaluateShadowPCF(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.shadowCascades[cascade]], surface.worldPos);

                shadow = saturate(shadow + cascadeShadow * (1.0001 - shadow.y));

                if (shadow.y == 1)
                    break;
            }
            else
                break;
        }

        shadow.x += (1 - shadow.y) * light.outOfBoundsShadow;

        float objectShadow = 1;

        [loop]
        for (int object = 0; object < 4; object++)
        {
            if (light.perObjectShadows[object] >= 0)
            {
                float2 thisObjectShadow = EvaluateShadowPoisson(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.perObjectShadows[object]], surface.worldPos, sincos, 3.0);
                //float2 thisObjectShadow = EvaluateShadowGather16(t_ShadowMapArray, s_ShadowSampler, g_Deferred.shadows[light.perObjectShadows[object]], surface.worldPos, g_Deferred.shadowMapTextureSize);
                //float2 thisObjectShadow = EvaluateShadowPCF(t_ShadowMapArray, s_ShadowSamplerComparison, g_Deferred.shadows[light.perObjectShadows[object]], surface.worldPos);

                objectShadow *= saturate(thisObjectShadow.x + (1 - thisObjectShadow.y));
            }
        }

        shadow.x *= objectShadow;

        float diffuseRadiance, specularRadiance;
        ShadeSurface(light, surface, viewIncident, diffuseRadiance, specularRadiance);

        diffuseTerm += (shadow.x * diffuseRadiance) * light.color;
        specularTerm += (shadow.x * specularRadiance) * light.color * surface.specularColor;
    }

    if (g_Deferred.numLightProbes > 0)
    {
        float3 N = surface.normal;
        float3 R = reflect(viewIncident, N);
        float NdotV = saturate(-dot(N, viewIncident));
        float2 environmentBrdf = t_EnvironmentBrdf.SampleLevel(s_BrdfSampler, float2(NdotV, surface.roughness), 0).xy;

        float lightProbeWeight = 0;
        float3 lightProbeDiffuse = 0;
        float3 lightProbeSpecular = 0;

        [loop]
        for (uint nProbe = 0; nProbe < g_Deferred.numLightProbes; nProbe++)
        {
            LightProbeConstants lightProbe = g_Deferred.lightProbes[nProbe];

            float weight = GetLightProbeWeight(lightProbe, surface.worldPos);

            if (weight == 0)
                continue;

            float specularMipLevel = sqrt(saturate(surface.roughness)) * (lightProbe.mipLevels - 1);
            float3 diffuseProbe = t_DiffuseLightProbe.SampleLevel(s_LightProbeSampler, float4(N.x, N.y, -N.z, lightProbe.diffuseArrayIndex), 0).rgb;
            float3 specularProbe = t_SpecularLightProbe.SampleLevel(s_LightProbeSampler, float4(R.x, R.y, -R.z, lightProbe.specularArrayIndex), specularMipLevel).rgb;

            lightProbeDiffuse += (weight * lightProbe.diffuseScale) * diffuseProbe;
            lightProbeSpecular += (weight * lightProbe.specularScale) * specularProbe;
            lightProbeWeight += weight;
        }

        if (lightProbeWeight > 1)
        {
            float invWeight = rcp(lightProbeWeight);
            lightProbeDiffuse *= invWeight;
            lightProbeSpecular *= invWeight;
        }

        diffuseTerm += lightProbeDiffuse;
        specularTerm += lightProbeSpecular * (surface.specularColor * environmentBrdf.x + environmentBrdf.y);
    }

    {
        float3 ambientColor = lerp(g_Deferred.ambientColorBottom.rgb, g_Deferred.ambientColorTop.rgb, surface.normal.y * 0.5 + 0.5);

        diffuseTerm += ambientColor;
        specularTerm += ambientColor * surface.specularColor;
    }

    if (g_Deferred.indirectDiffuseScale > 0)
    {
        float4 indirectDiffuse = t_IndirectDiffuse[int2(i_position.xy)];
        diffuseTerm += indirectDiffuse.rgb * g_Deferred.indirectDiffuseScale;
    }

    if (g_Deferred.indirectSpecularScale > 0)
    {
        float4 indirectSpecular = t_IndirectSpecular[int2(i_position.xy)];
        specularTerm += indirectSpecular.rgb * g_Deferred.indirectSpecularScale;
    }

    o_color.rgb = diffuseTerm * surface.diffuseColor
        + specularTerm
        + surface.emissiveColor;

    o_color.a = 0;
}
