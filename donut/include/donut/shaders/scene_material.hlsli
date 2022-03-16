#pragma pack_matrix(row_major)

#include "forward_vertex.hlsli"
#include "material_cb.h"
#include "surface.hlsli"

// Bindings - can be overriden before including this file if necessary

#ifndef MATERIAL_CB_SLOT 
#define MATERIAL_CB_SLOT b0
#endif

#ifndef MATERIAL_DIFFUSE_SLOT 
#define MATERIAL_DIFFUSE_SLOT t0
#endif

#ifndef MATERIAL_SPECULAR_SLOT 
#define MATERIAL_SPECULAR_SLOT t1
#endif

#ifndef MATERIAL_NORMALS_SLOT 
#define MATERIAL_NORMALS_SLOT t2
#endif

#ifndef MATERIAL_EMISSIVE_SLOT 
#define MATERIAL_EMISSIVE_SLOT t3
#endif

#ifndef MATERIAL_SAMPLER_SLOT 
#define MATERIAL_SAMPLER_SLOT s0
#endif

cbuffer c_Material : register(MATERIAL_CB_SLOT)
{
    MaterialConstants g_Material;
};

Texture2D t_Diffuse : register(MATERIAL_DIFFUSE_SLOT);
Texture2D t_Specular : register(MATERIAL_SPECULAR_SLOT);
Texture2D t_Normals : register(MATERIAL_NORMALS_SLOT);
Texture2D t_Emissive : register(MATERIAL_EMISSIVE_SLOT);

SamplerState s_MaterialSampler : register(MATERIAL_SAMPLER_SLOT);

/** Convert RGB to normal
*/
float3 RgbToNormal(float3 rgb, out float len)
{
    float3 n = rgb * 2 - 1;

    len = length(n);

    if (len > 0)
        return n / len;
    else
        return 0;
}

/** Convert RG to normal
*/
float3 RgToNormal(float2 rg)
{
    float3 n;
    n.xy = rg * 2 - 1;

    // Saturate because error from BC5 can break the sqrt
    n.z = saturate(dot(rg, rg)); // z = r*r + g*g
    n.z = sqrt(1 - n.z);
    return normalize(n);
}

// ================================================================================================
// Converts a Beckmann roughness parameter to a Phong specular power
// ================================================================================================
float RoughnessToSpecPower(in float m) {
    return 2.0f / (m * m) - 2.0f;
}

// ================================================================================================
// Converts a Blinn-Phong specular power to a Beckmann roughness parameter
// ================================================================================================
float SpecPowerToRoughness(in float s) {
    return sqrt(2.0f / (s + 2.0f));
}

float AdjustRoughnessToksvig(float roughness, float normalMapLen)
{
    float toksvigAggressiveness = 2;
    float shininess = RoughnessToSpecPower(roughness) * toksvigAggressiveness;
    float ft = normalMapLen / lerp(shininess, 1.0f, normalMapLen);
    ft = max(ft, 0.01f);
    return SpecPowerToRoughness(ft * shininess / toksvigAggressiveness);
}

void DecodeSpecularTexture(inout SurfaceParams result, float4 specularTextureValue)
{
    if (g_Material.specularTextureType == SpecularType_Scalar)
    {
        result.specularColor *= specularTextureValue.rrr;

        result.diffuseColor *= (1 - result.specularColor);
    }
    else if (g_Material.specularTextureType == SpecularType_Color)
    {
        result.specularColor *= specularTextureValue.rgb;

        if (specularTextureValue.a > 0)
            result.roughness = 1 - specularTextureValue.a * (1 - result.roughness);

        result.diffuseColor *= (1 - result.specularColor);
    }
    else if (g_Material.specularTextureType == SpecularType_MetalRough)
    {
        float occlusion = specularTextureValue.r;
        float roughness = specularTextureValue.g;
        float metallic = specularTextureValue.b;

        float3 baseColor = result.diffuseColor;
        result.diffuseColor = baseColor * (1 - metallic);
        result.specularColor = lerp(0.04, baseColor, metallic);
        result.roughness = 1 - (1 - roughness) * (1 - result.roughness);
    }
}

void DecodeNormalsTexture(inout SurfaceParams result, SceneVertex vtx, float4 normalsTextureValue)
{
    float3 localNormal;
    if (normalsTextureValue.z > 0)
    {
        float normalMapLen = 0;
        localNormal = RgbToNormal(normalsTextureValue.xyz, normalMapLen);

        if (normalMapLen > 0)
        {
            result.roughness = AdjustRoughnessToksvig(result.roughness, normalMapLen);
        }
    }
    else
        localNormal = RgToNormal(normalsTextureValue.xy);
    
    localNormal.y = -localNormal.y;

    result.normal = normalize(vtx.m_tangent * localNormal.x + vtx.m_bitangent * localNormal.y + vtx.m_normal * localNormal.z);
}

SurfaceParams EvaluateSceneMaterial(SceneVertex vtx)
{
    SurfaceParams result = DefaultSurfaceParams();
    result.worldPos = vtx.m_pos;

    float normalLength = length(vtx.m_normal);
    if (normalLength == 0)
    {
        // Handle the case when something's wrong with vertex normals - we don't want NaNs or zeros in the normal channel
        result.geometryNormal = normalize(cross(ddy(vtx.m_pos), ddx(vtx.m_pos)));
    }
    else
    {
        // Just normalize
        result.geometryNormal = vtx.m_normal / normalLength;
    }

    result.normal = result.geometryNormal;
    result.diffuseColor = g_Material.diffuseColor.rgb;
    result.specularColor = g_Material.specularColor;
    result.emissiveColor = g_Material.emissiveColor;
    result.opacity = g_Material.opacity;
    result.roughness = g_Material.roughness;

    if (g_Material.useDiffuseTexture)
    {
        float4 diffuseTextureValue = t_Diffuse.Sample(s_MaterialSampler, vtx.m_uv);
        result.diffuseColor *= diffuseTextureValue.rgb;
        result.opacity *= diffuseTextureValue.a;
    }

    if (g_Material.specularTextureType != SpecularType_None)
    {
        float4 specularTextureValue = t_Specular.Sample(s_MaterialSampler, vtx.m_uv);
        DecodeSpecularTexture(result, specularTextureValue);
    }
    else
    {
        result.diffuseColor *= (1 - result.specularColor);
    }

    if (g_Material.useEmissiveTexture)
    {
        float4 emissiveTextureValue = t_Emissive.Sample(s_MaterialSampler, vtx.m_uv);
        result.emissiveColor *= emissiveTextureValue.rgb;
    }

    if (g_Material.useNormalsTexture)
    {
        float4 normalsTextureValue = t_Normals.Sample(s_MaterialSampler, vtx.m_uv);
        DecodeNormalsTexture(result, vtx, normalsTextureValue);
    }

    return result;
}


SurfaceParams EvaluateSceneMaterialCompute(SceneVertex vtx, float sampleLevel)
{
    SurfaceParams result = DefaultSurfaceParams();
    result.worldPos = vtx.m_pos;

    float normalLength = length(vtx.m_normal);
    if (normalLength == 0)
    {
        result.geometryNormal = 0;
    }
    else
    {
        result.geometryNormal = vtx.m_normal / normalLength;
    }

    result.normal = result.geometryNormal;
    result.diffuseColor = g_Material.diffuseColor.rgb;
    result.specularColor = g_Material.specularColor;
    result.emissiveColor = g_Material.emissiveColor;
    result.opacity = g_Material.opacity;
    result.roughness = g_Material.roughness;

    if (g_Material.useDiffuseTexture)
    {
        float4 diffuseTextureValue = t_Diffuse.SampleLevel(s_MaterialSampler, vtx.m_uv, sampleLevel);
        result.diffuseColor *= diffuseTextureValue.rgb;
        result.opacity *= diffuseTextureValue.a;
    }

    if (g_Material.specularTextureType != SpecularType_None)
    {
        float4 specularTextureValue = t_Specular.SampleLevel(s_MaterialSampler, vtx.m_uv, sampleLevel);
        DecodeSpecularTexture(result, specularTextureValue);
    }
    else
    {
        result.diffuseColor *= (1 - result.specularColor);
    }

    if (g_Material.useEmissiveTexture)
    {
        float4 emissiveTextureValue = t_Emissive.SampleLevel(s_MaterialSampler, vtx.m_uv, sampleLevel);
        result.emissiveColor *= emissiveTextureValue.rgb;
    }

    if (g_Material.useNormalsTexture)
    {
        float4 normalsTextureValue = t_Normals.SampleLevel(s_MaterialSampler, vtx.m_uv, sampleLevel);
        DecodeNormalsTexture(result, vtx, normalsTextureValue);
    }


    return result;
}