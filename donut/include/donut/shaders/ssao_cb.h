
#ifndef SSAO_CB_H
#define SSAO_CB_H

struct SsaoConstants
{
    float4x4    matClipToView;
    float4x4    matWorldToView;

    float2      randomOffset;
    float2      windowToClipScale;

    float2      windowToClipBias;
    float2      clipToWindowScale;

    float2      clipToWindowBias;
    float2      radiusWorldToClip;

    float       amount;
    float       invBackgroundViewDepth;
    float       radiusWorld;
    float       surfaceBias;

    float3      padding;
    float       powerExponent;
};

#endif // SSAO_CB_H