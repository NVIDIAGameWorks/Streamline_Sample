/*
 * Simple Image Adjustments
 *
 * Applies HDR->LDR conversion (exposure + tonemapping + sRGB conversion),
 * gamma correction, and black/white levels
 */

#include <donut/shaders/ngx_adjust_cb.h>

Texture2D t_Source : register(t0);
Texture2D<float> t_Exposure : register(t1);

cbuffer c_Adjustment : register(b0)
{
    NGXAdjustmentConstants g_Adjustment;
};

float ToLDR(float channel)
{
    return channel / (1 + channel);
}

float ToSRGB(float channel) {
    if (channel >= 0.0031308)
    {
        return 1.055f * pow(abs(channel), 1.f / 2.4f) - 0.055f;
    }
    else
    {
        return channel * 12.92f;
    }
}

float3 ApplyLevels(float3 color, float blackLevel, float whiteLevel) {
    return (color - blackLevel) / (whiteLevel - blackLevel);
}

float3 ApplyGamma(float3 color, float gamma) {
    return pow(max(0, color), 2.2f / gamma);
}

void main(
	in float4 pos : SV_Position,
	in float2 uv : UV,
	out float4 o_rgba : SV_Target)
{
    float exposure = t_Exposure[uint2(0, 0)];

    float4 srcColor = t_Source[pos.xy];

    o_rgba.rgb = srcColor.rgb;

    if (g_Adjustment.isHDR)
    {
        o_rgba.rgb *= exposure * g_Adjustment.postExposureScale;
        o_rgba.r = ToSRGB(ToLDR(o_rgba.r));
        o_rgba.g = ToSRGB(ToLDR(o_rgba.g));
        o_rgba.b = ToSRGB(ToLDR(o_rgba.b));
    }

    o_rgba.rgb = ApplyGamma(o_rgba.rgb, g_Adjustment.postGamma);
    o_rgba.rgb = ApplyLevels(o_rgba.rgb, g_Adjustment.postBlackLevel, g_Adjustment.postWhiteLevel);
    
    o_rgba.a = srcColor.a;
}