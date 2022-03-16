
#ifndef VOXELIZATION_CB_H
#define VOXELIZATION_CB_H

#include <donut/shaders/light_cb.h>

#define FORWARD_MAX_LIGHTS 16
#define FORWARD_MAX_SHADOWS 16

struct VoxelizationViewConstants
{
    float4x4    matWorldToClip;
};

struct VoxelizationLightConstants
{
    float2      shadowMapTextureSize;
    float2      shadowMapTextureSizeInv;
    float4      ambientColorTop;
    float4      ambientColorBottom;

    uint3       padding;
    uint        numLights;

    LightConstants lights[FORWARD_MAX_LIGHTS];
    ShadowConstants shadows[FORWARD_MAX_SHADOWS];
};

#endif // VOXELIZATION_CB_H