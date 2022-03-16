
#ifndef DEFERRED_LIGHTING_CB_H
#define DEFERRED_LIGHTING_CB_H

#include "light_cb.h"
#include "view_cb.h"

#define DEFERRED_MAX_LIGHTS 16
#define DEFERRED_MAX_SHADOWS 16
#define DEFERRED_MAX_LIGHT_PROBES 16

struct DeferredLightingConstants
{
    PlanarViewConstants view;

    float2      shadowMapTextureSize;
    int         gbufferArraySlice;
    int         padding;

    float4      ambientColorTop;
    float4      ambientColorBottom;

    uint        numLights;
    uint        numLightProbes;
    float       indirectDiffuseScale;
    float       indirectSpecularScale;

    float2      randomOffset;
    float2      padding2;

    float4      noisePattern[4];

    LightConstants lights[DEFERRED_MAX_LIGHTS];
    ShadowConstants shadows[DEFERRED_MAX_SHADOWS];
    LightProbeConstants lightProbes[DEFERRED_MAX_LIGHT_PROBES];
};

#endif // DEFERRED_LIGHTING_CB_H