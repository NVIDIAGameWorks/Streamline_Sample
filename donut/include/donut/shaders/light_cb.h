
#ifndef LIGHT_CB_H
#define LIGHT_CB_H

// Light types matching the LightType enum in SceneTypes.h
static const uint LightType_None = 0;
static const uint LightType_Directional = 1;
static const uint LightType_Spot = 2;
static const uint LightType_Point = 3;

struct ShadowConstants
{
    float4x4 matWorldToUvzwShadow;

    float2 shadowFadeScale;
    float2 shadowFadeBias;

    float2 shadowMapCenterUV;
    float shadowFalloffDistance;
    int shadowMapArrayIndex;

    float2 shadowMapSizeTexels;
    float2 shadowMapSizeTexelsInv;
};

struct LightConstants
{
    float3 direction;
    uint lightType;

    float3 position;
    float radius;

    float3 color;
    float radiance;

    float angularSizeOrInvRange;   // angular size for directional lights, 1/range for spot and point lights
    float innerAngle;
    float outerAngle;
    float outOfBoundsShadow;

    int4 shadowCascades;
    int4 perObjectShadows;
};

struct LightProbeConstants
{
    float diffuseScale;
    float specularScale;
    float mipLevels;
    float padding1;

    uint diffuseArrayIndex;
    uint specularArrayIndex;
    uint2 padding2;

    float4 frustumPlanes[6];
};

#endif // LIGHT_CB_H