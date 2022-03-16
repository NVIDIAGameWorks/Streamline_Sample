#pragma pack_matrix(row_major)

#include <donut/shaders/ssao_cb.h>

cbuffer c_SSAO : register(b0)
{
    SsaoConstants g_SSAO;
};


#if SAMPLE_COUNT==1
Texture2D<float> t_DepthBuffer : register(t0);
Texture2D<float4> t_NormalBuffer : register(t1);
#define LOAD(t, px) t[px]
#else
Texture2DMS<float> t_DepthBuffer : register(t0);
Texture2DMS<float4> t_NormalBuffer : register(t1);
#define LOAD(t, px) t.Load(px, 0)
#endif

static const float2 g_SamplePositions[] = {
    float2(-0.016009523, -0.10995169),
    float2(-0.159746436, 0.047527402),
    float2(0.09339819, 0.201641995),
    float2(0.232600698, 0.151846663),
    float2(-0.220531935, -0.24995355),
    float2(-0.251498143, 0.29661971),
    float2(0.376870668, .23558303),
    float2(0.201175979, 0.457742532),
    float2(-0.535502966, -0.147913991),
    float2(-0.076133435, 0.606350138),
    float2(0.666537538, 0.013120791),
    float2(-0.118107615, -0.712499494),
    float2(-0.740973793, 0.236423582),
    float2(0.365057451, .749117816),
    float2(0.734614792, 0.500464349),
    float2(-0.638657704, -0.695766948)
};

float2 GetRotationBasis(float2 pos)
{
    float angle = frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) * 3.1415;
    return float2(sin(angle), cos(angle));
}

float ComputeAOSample(float3 P, float3 N, float3 S, float InvR2)
{
    float3 V = S - P;
    float VdotV = dot(V, V);
    float NdotV = dot(N, V) * rsqrt(VdotV);
    float lambertian = saturate(NdotV - g_SSAO.surfaceBias);
    float falloff = saturate(1 - VdotV * InvR2);
    return saturate(1.0 - lambertian * falloff * g_SSAO.amount);
}

float2 ClipToWindow(float2 clipPos)
{
    return clipPos.xy * g_SSAO.clipToWindowScale.xy + g_SSAO.clipToWindowBias.xy;
}

float2 WindowToClip(float2 windowPos)
{
    return windowPos.xy * g_SSAO.windowToClipScale.xy + g_SSAO.windowToClipBias.xy;
}

float4 NormalizePoint(float4 pt)
{
    return float4(pt.xyz / pt.w, 1);
}

float3 ComputeAO(float2 windowPos, float2 delta)
{
    float4 clipPos;
    clipPos.xy = WindowToClip(windowPos);
    clipPos.z = LOAD(t_DepthBuffer, int2(windowPos.xy)).x;
    clipPos.w = 1;

    if (any(abs(clipPos.xy) > clipPos.w))
        return 1;

    float4 viewPos = NormalizePoint(mul(clipPos, g_SSAO.matClipToView));

    float3 normal = LOAD(t_NormalBuffer, int2(windowPos.xy)).xyz;

    if (all(normal == 0))
        return 1;

    normal = mul(float4(normal, 0), g_SSAO.matWorldToView).xyz;
    
    float radiusWorld = g_SSAO.radiusWorld * max(1.0, viewPos.z * g_SSAO.invBackgroundViewDepth);
    float2 radiusClip = g_SSAO.radiusWorldToClip.xy * (radiusWorld / viewPos.z);
    float invRadiusWorld2 = rcp(radiusWorld * radiusWorld);

    int numSamples = 16;
    float numValidSamples = 0;
    float result = 0;

    for (int nSample = 0; nSample < numSamples; nSample++)
    {
        float2 sampleOffset = g_SamplePositions[nSample];
        sampleOffset = float2(sampleOffset.x * delta.y - sampleOffset.y * delta.x, sampleOffset.x * delta.x + sampleOffset.y * delta.y);

        float4 sampleClipPos = float4(clipPos.xy + sampleOffset * radiusClip, 0, 1);
        if (all(abs(sampleClipPos.xy) < 1))
        {
            float2 sampleWindowPos = ClipToWindow(sampleClipPos.xy);
            sampleClipPos.z = LOAD(t_DepthBuffer, int2(sampleWindowPos.xy)).x;

            if (sampleClipPos.z != 0 && sampleClipPos.z != 1)
            {
                float4 sampleViewPos = NormalizePoint(mul(sampleClipPos, g_SSAO.matClipToView));

                float AO = ComputeAOSample(viewPos.xyz, normal, sampleViewPos.xyz, invRadiusWorld2);

                result += AO;
                numValidSamples += 1;
            }
        }
    }

    if (numValidSamples > 0)
        result = pow(saturate(result * rcp(numValidSamples)), g_SSAO.powerExponent);
    else
        result = 1;

    return result.xxx;
}

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0)
{
    float2 delta = GetRotationBasis(i_position.xy + g_SSAO.randomOffset);
    float3 ao = ComputeAO(i_position.xy, delta);
    o_color = float4(ao.xyz, 1);
}