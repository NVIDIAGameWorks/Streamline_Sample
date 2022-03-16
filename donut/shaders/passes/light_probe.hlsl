#pragma pack_matrix(row_major)

// GS to replicate a quad into 6 cube faces

struct GSInput
{
    float4 position : SV_Position;
    float2 uv : UV;
};

struct GSOutput
{
    float4 position : SV_Position;
    float2 uv : UV;
    uint arrayIndex : SV_RenderTargetArrayIndex;
};

[maxvertexcount(3)]
[instance(6)]
void cubemap_gs(
    triangle GSInput Input[3],
    uint instanceID : SV_GSInstanceID,
    inout TriangleStream<GSOutput> Output)
{
    GSOutput OutputVertex;
    OutputVertex.arrayIndex = instanceID;

    OutputVertex.position = Input[0].position;
    OutputVertex.uv = Input[0].uv;
    Output.Append(OutputVertex);

    OutputVertex.position = Input[1].position;
    OutputVertex.uv = Input[1].uv;
    Output.Append(OutputVertex);

    OutputVertex.position = Input[2].position;
    OutputVertex.uv = Input[2].uv;
    Output.Append(OutputVertex);
}

// Helpers

static const float PI = 3.1415926535;

float radicalInverse(uint i)
{
    i = (i & 0x55555555) << 1 | (i & 0xAAAAAAAA) >> 1;
    i = (i & 0x33333333) << 2 | (i & 0xCCCCCCCC) >> 2;
    i = (i & 0x0F0F0F0F) << 4 | (i & 0xF0F0F0F0) >> 4;
    i = (i & 0x00FF00FF) << 8 | (i & 0xFF00FF00) >> 8;
    i = (i << 16) | (i >> 16);
    return float(i) * 2.3283064365386963e-10f;
}

float2 getHammersley(uint i, uint N)
{
    return float2(float(i) / float(N), radicalInverse(i));
}

float3 uvToDirection(float2 uv, uint face)
{
    float3 direction = float3(uv.x * 2 - 1, 1 - uv.y * 2, 1);

    switch (face)
    {
    case 0: direction.xyz = float3(direction.z, direction.y, -direction.x); break;
    case 1: direction.xyz = float3(-direction.z, direction.y, direction.x); break;
    case 2: direction.xyz = float3(direction.x, direction.z, -direction.y); break;
    case 3: direction.xyz = float3(direction.x, -direction.z, direction.y); break;
    case 4: direction.xyz = float3(direction.x, direction.y, direction.z);  break;
    case 5: direction.xyz = float3(-direction.x, direction.y, -direction.z); break;
    }

    return normalize(direction);
}

// Basis utils

struct Basis
{
    float3 up;
    float3 right;
    float3 forward;
};

Basis GenerateBasis(float3 N)
{
    Basis result;
    result.forward = N;
    result.up = float3(0, 1, 0);

    if (abs(N.y) > abs(N.x) && abs(N.y) > abs(N.z))
    {
        float3 adjacentUp = (N.y < 0) ? float3(N.x, N.y, 0) : float3(-N.x, -N.y, 0);
        float blendFactor = saturate(length(N.xz) * 1.5);
        result.up = lerp(result.up, adjacentUp, blendFactor);
    }

    result.right = normalize(cross(result.forward, result.up));
    result.up = normalize(cross(result.right, result.forward));

    return result;
}


void RandomizeBasis(float2 pos, inout Basis basis)
{
    float angle = frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) * 3.1415;
    float s = sin(angle);
    float c = cos(angle);

    float3 up = basis.up * c + basis.right * s;
    float3 right = basis.right * c - basis.up * s;

    basis.up = up;
    basis.right = right;
}


// Importance sampling

float3 importanceSampleCosDir(float2 u, Basis basis)
{
    float u1 = u.x;
    float u2 = u.y;

    float r = sqrt(u1);
    float phi = u2 * PI * 2;

    float3 L = float3(r * cos(phi),
        r * sin(phi),
        sqrt(max(0.0f, 1.0f - u1)));

    return normalize(basis.up * L.y + basis.right * L.x + basis.forward * L.z);
}

float3 importanceSampleGGX(float2 u, float roughness, Basis basis)
{
    float a = roughness * roughness;

    float phi = PI * 2 * u.x;
    float cosTheta = sqrt((1 - u.y) / (1 + (a * a - 1) * u.y));
    float sinTheta = sqrt(1 - cosTheta * cosTheta);

    // Tangent space H
    float3 tH;
    tH.x = sinTheta * cos(phi);
    tH.y = sinTheta * sin(phi);
    tH.z = cosTheta;

    // World space H
    return normalize(basis.right * tH.x + basis.up * tH.y + basis.forward * tH.z);
}

float evalGGX(float roughness, float NdotH)
{
    float a2 = roughness * roughness;
    float d = ((NdotH * a2 - NdotH) * NdotH + 1);
    return a2 / (d * d);
}

float smithGGX(float NdotL, float NdotV, float roughness)
{
    float k = ((roughness + 1) * (roughness + 1)) / 8;
    float g1 = NdotL / (NdotL * (1 - k) + k);
    float g2 = NdotV / (NdotV * (1 - k) + k);
    return g1 * g2;
}

float3 fresnelSchlick(float3 f0, float3 f90, float u)
{
    return f0 + (f90 - f0) * pow(1 - u, 5);
}

// Integration

#include <donut/shaders/light_probe_cb.h>

cbuffer c_LightProbe : register(b0)
{
    LightProbeConstants g_LightProbe;
};

TextureCube t_EnvironmentMap : register(t0);
SamplerState s_EnvironmentMapSampler : register(s0);


void mip_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 direction = uvToDirection(Input.uv, Input.arrayIndex);

    o_color = t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, direction, 0);
}

void diffuse_probe_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 N = uvToDirection(Input.uv, Input.arrayIndex);
    Basis basis = GenerateBasis(N);
    RandomizeBasis(Input.position.xy, basis);

    float4 accumulation = 0;
    float totalWeight = 0;
    
    for (uint i = 0; i < g_LightProbe.sampleCount; i++)
    {
        float2 u = getHammersley(i, g_LightProbe.sampleCount);
        float3 L = importanceSampleCosDir(u, basis);
        float NdotL = dot(N, L);
        if (NdotL > 0)
        {
            accumulation += t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, L, g_LightProbe.lodBias) * NdotL / (2 * PI);
            totalWeight += NdotL;
        }
    }

    o_color = accumulation / totalWeight;
}

void specular_probe_ps(
    in GSOutput Input,
    out float4 o_color : SV_Target0)
{
    float3 N = uvToDirection(Input.uv, Input.arrayIndex);
    Basis basis = GenerateBasis(N);
    RandomizeBasis(Input.position.xy, basis);

    float4 accBrdf = 0;
    float accBrdfWeight = 0;

    for (uint i = 0; i < g_LightProbe.sampleCount; i++)
    {
        float2 u = getHammersley(i, g_LightProbe.sampleCount);
        float3 H = importanceSampleGGX(u, g_LightProbe.roughness, basis);
        float3 L = reflect(-N, H);
        float NdotL = dot(N, L);

        if (NdotL > 0)
        {
            float NdotH = saturate(dot(N, H));
            float LdotH = saturate(dot(L, H));

            // D term GGX
            float pdf = (evalGGX(g_LightProbe.roughness, NdotH) / PI) * NdotH / (4 * LdotH);

            float omegaS = 1 / (g_LightProbe.sampleCount * pdf);
            float omegaP = 4.0 * PI / (6 * g_LightProbe.inputCubeSize * g_LightProbe.inputCubeSize);
            float mipLevel = 0.5 * log2(omegaS / omegaP);

            float4 Li = t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, L, mipLevel + g_LightProbe.lodBias);

            accBrdf += Li * NdotL;
            accBrdfWeight += NdotL;
        }
    }

    o_color = accBrdf / accBrdfWeight;
}

void environment_brdf_ps(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 o_color : SV_Target0)
{
    const float3 N = float3(0, 0, 1);
    Basis basis = GenerateBasis(N);

    // texC.x is NdotV, calculate a valid V assuming constant N
    float cos_theta = uv.x;
    float sin_thets = sqrt(1 - cos_theta * cos_theta);
    const float3 V = float3(sin_thets, 0, cos_theta);

    float roughness = uv.y;

    float NdotV = dot(N, V);
    float2 accumulation = 0;

    const uint sampleCount = 1024;

    for (uint i = 0; i < sampleCount; i++)
    {
        float2 u = getHammersley(i, sampleCount);

        // Specular GGX integration (stored in RG)
        float3 H = importanceSampleGGX(u, roughness, basis);
        float3 L = reflect(-N, H);
        float NdotH = saturate(dot(N, H));
        float LdotH = saturate(dot(L, H));
        float NdotL = saturate(dot(N, L));

        float G = smithGGX(NdotL, NdotV, roughness);
        if (NdotL > 0 && G > 0)
        {
            float GVis = (G * LdotH) / (NdotV * NdotH);
            float Fc = fresnelSchlick(0, 1, LdotH).r;
            accumulation.r += (1 - Fc) * GVis;
            accumulation.g += Fc * GVis;
        }
    }

    o_color = float4(accumulation.xy / float(sampleCount), 0, 0);
}