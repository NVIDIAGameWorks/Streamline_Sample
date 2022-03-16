
#ifndef VIEW_CB_H
#define VIEW_CB_H

struct PlanarViewConstants
{
    float4x4    matWorldToView;
    float4x4    matViewToClip;
    float4x4    matWorldToClip;
    float4x4    matClipToView;
    float4x4    matViewToWorld;
    float4x4    matClipToWorld;

    float2      viewportOrigin;
    float2      viewportSize;

    float2      viewportSizeInv;
    float2      pixelOffset;

    float2      clipToWindowScale;
    float2      clipToWindowBias;

    float2      windowToClipScale;
    float2      windowToClipBias;

    float4      cameraDirectionOrPosition;
};

#endif // VIEW_CB_H