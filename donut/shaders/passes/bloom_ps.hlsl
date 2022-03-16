#pragma pack_matrix(row_major)

#include <donut/shaders/bloom_cb.h>

cbuffer c_Bloom : register(b0)
{
    BloomConstants g_Bloom;
};

SamplerState s_Sampler : register(s0);
Texture2D<float4> t_Src : register(t0);

float square(float x) { return x * x; }

void main(
    in float4 pos : SV_Position,
    in float2 uv : UV,
    out float4 o_rgba : SV_Target
)
{
    float3 result = t_Src[pos.xy].rgb;

    for (float x = 1; x < g_Bloom.numSamples; x += 2)
    {
        float w1 = exp(square(x) * g_Bloom.argumentScale);
        float w2 = exp(square(x + 1) * g_Bloom.argumentScale);

        float w12 = w1 + w2;
        float p = w2 / w12;
        float2 offset = g_Bloom.pixstep * (x + p);

        result += t_Src.SampleLevel(s_Sampler, uv + offset, 0).rgb * w12;
        result += t_Src.SampleLevel(s_Sampler, uv - offset, 0).rgb * w12;
    }

    result *= g_Bloom.normalizationScale;

    o_rgba = float4(result, 1.0);
}
