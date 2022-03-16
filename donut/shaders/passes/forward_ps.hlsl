#pragma pack_matrix(row_major)

#include <donut/shaders/forward_cb.h>
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/lighting.hlsli>
#include <donut/shaders/shadows.hlsli>

cbuffer c_ForwardView : register(b1 DESCRIPTOR_SET_1)
{
    ForwardShadingViewConstants g_ForwardView;
};

cbuffer c_ForwardLight : register(b2 DESCRIPTOR_SET_1)
{
    ForwardShadingLightConstants g_ForwardLight;
};

Texture2DArray t_ShadowMapArray : register(t4 DESCRIPTOR_SET_2);
TextureCubeArray t_DiffuseLightProbe : register(t5 DESCRIPTOR_SET_2);
TextureCubeArray t_SpecularLightProbe : register(t6 DESCRIPTOR_SET_2);
Texture2D t_EnvironmentBrdf : register(t7 DESCRIPTOR_SET_2);

SamplerState s_ShadowSampler : register(s1 DESCRIPTOR_SET_1);
SamplerState s_LightProbeSampler : register(s2 DESCRIPTOR_SET_2);
SamplerState s_BrdfSampler : register(s3 DESCRIPTOR_SET_2);

float3 GetIncidentVector(float4 directionOrPosition, float3 surfacePos)
{
    if (directionOrPosition.w > 0)
        return normalize(surfacePos.xyz - directionOrPosition.xyz);
    else
        return directionOrPosition.xyz;
}

void main(
    in float4 i_position : SV_Position,
	in SceneVertex i_vtx,
#if SINGLE_PASS_STEREO
    in uint i_viewport : SV_ViewportArrayIndex,
#endif
    out float4 o_color : SV_Target0
)
{
    SurfaceParams surface = EvaluateSceneMaterial(i_vtx);

#if SINGLE_PASS_STEREO
    float4 cameraDirectionOrPosition = (i_viewport == 0) ? g_ForwardView.leftView.cameraDirectionOrPosition : g_ForwardView.rightView.cameraDirectionOrPosition;
#else
    float4 cameraDirectionOrPosition = g_ForwardView.leftView.cameraDirectionOrPosition;
#endif
    float3 viewIncident = GetIncidentVector(cameraDirectionOrPosition, surface.worldPos);

    float3 diffuseTerm = 0;
    float3 specularTerm = 0;

    [loop]
    for(uint nLight = 0; nLight < g_ForwardLight.numLights; nLight++)
    {
        LightConstants light = g_ForwardLight.lights[nLight];

        float2 shadow = 0;
        for (int cascade = 0; cascade < 4; cascade++)
        {
            if (light.shadowCascades[cascade] >= 0)
            {
                float2 cascadeShadow = EvaluateShadowGather16(t_ShadowMapArray, s_ShadowSampler, g_ForwardLight.shadows[light.shadowCascades[cascade]], surface.worldPos, g_ForwardLight.shadowMapTextureSize);

                shadow = saturate(shadow + cascadeShadow * (1.0001 - shadow.y));

                if (shadow.y == 1)
                    break;
            }
            else
                break;
        }

        shadow.x += (1 - shadow.y) * light.outOfBoundsShadow;

        float objectShadow = 1;

        for (int object = 0; object < 4; object++)
        {
            if (light.perObjectShadows[object] >= 0)
            {
                float2 thisObjectShadow = EvaluateShadowGather16(t_ShadowMapArray, s_ShadowSampler, g_ForwardLight.shadows[light.perObjectShadows[object]], surface.worldPos, g_ForwardLight.shadowMapTextureSize);

                objectShadow *= saturate(thisObjectShadow.x + (1 - thisObjectShadow.y));
            }
        }

        shadow.x *= objectShadow;

        float diffuseRadiance, specularRadiance;
        ShadeSurface(light, surface, viewIncident, diffuseRadiance, specularRadiance);

        diffuseTerm += (shadow.x * diffuseRadiance) * light.color;
        specularTerm += (shadow.x * specularRadiance) * light.color * surface.specularColor;
    }

    if(g_ForwardLight.numLightProbes > 0)
    {
        float3 N = surface.normal;
        float3 R = reflect(viewIncident, N);
        float NdotV = saturate(-dot(N, viewIncident));
        float2 environmentBrdf = t_EnvironmentBrdf.SampleLevel(s_BrdfSampler, float2(NdotV, surface.roughness), 0).xy;

        float lightProbeWeight = 0;
        float3 lightProbeDiffuse = 0;
        float3 lightProbeSpecular = 0;

        [loop]
        for (uint nProbe = 0; nProbe < g_ForwardLight.numLightProbes; nProbe++)
        {
            LightProbeConstants lightProbe = g_ForwardLight.lightProbes[nProbe];

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
        float3 ambientColor = lerp(g_ForwardLight.ambientColorBottom.rgb, g_ForwardLight.ambientColorTop.rgb, surface.normal.y * 0.5 + 0.5);

        diffuseTerm += ambientColor;
        specularTerm += ambientColor * surface.specularColor;
    }
    
    o_color.rgb = diffuseTerm * surface.diffuseColor 
        + specularTerm
        + surface.emissiveColor;

    o_color.a = surface.opacity;
}
