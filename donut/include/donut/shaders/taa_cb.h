
#ifndef TAA_CB_H
#define TAA_CB_H

struct TemporalAntiAliasingConstants
{
    float4x4 reprojectionMatrix;

    float2 previousViewOrigin;
    float2 previousViewSize;

    float2 viewOrigin;
    float2 viewSize;

    float2 sourceTextureSizeInv;
    float clampingFactor;
    float newFrameWeight;

    uint3 padding;
    uint stencilMask;
};

#endif // TAA_CB_H