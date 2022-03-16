
#ifndef SKY_CB_H
#define SKY_CB_H

struct SkyConstants
{
    float4x4    matClipToTranslatedWorld;

    float4      directionToSun;

    float2      padding;
    float       lightIntensity;
    float       angularSizeOfLight;
};

#endif // SKY_CB_H