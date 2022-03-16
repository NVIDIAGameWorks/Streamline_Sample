
#ifndef DEPTH_CB_H
#define DEPTH_CB_H

struct ToneMappingConstants
{
    uint2 viewOrigin;
    uint2 viewSize;

    float logLuminanceScale;
    float logLuminanceBias;
    float histogramLowPercentile;
    float histogramHighPercentile;

    float eyeAdaptationSpeedUp;
    float eyeAdaptationSpeedDown;
    float minAdaptedLuminance;
    float maxAdaptedLuminance;

    float frameTime;
    float exposureScale;
    float whitePointInvSquared;
    uint sourceSlice;

    float2 colorLUTTextureSize;
    float2 colorLUTTextureSizeInv;
};

#endif // DEPTH_CB_H