
#ifndef FORWARD_CB_H
#define FORWARD_CB_H

#include "light_cb.h"
#include "view_cb.h"

#define FORWARD_MAX_LIGHTS 16
#define FORWARD_MAX_SHADOWS 16
#define FORWARD_MAX_LIGHT_PROBES 16

struct ForwardShadingViewConstants
{
    PlanarViewConstants leftView;
    PlanarViewConstants rightView;
};

struct ForwardShadingLightConstants
{
    float2      shadowMapTextureSize;
    float2      shadowMapTextureSizeInv;
    float4      ambientColorTop;
    float4      ambientColorBottom;

    uint2       padding;
    uint        numLights;
    uint        numLightProbes;

    LightConstants lights[FORWARD_MAX_LIGHTS];
    ShadowConstants shadows[FORWARD_MAX_SHADOWS];
    LightProbeConstants lightProbes[FORWARD_MAX_LIGHT_PROBES];
};

#endif // FORWARD_CB_H