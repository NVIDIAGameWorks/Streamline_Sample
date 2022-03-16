
#ifndef MATERIAL_CB_H
#define MATERIAL_CB_H

static const int SpecularType_None = 0;
static const int SpecularType_Scalar = 1;
static const int SpecularType_Color = 2;
static const int SpecularType_MetalRough = 3;

struct MaterialConstants
{
    float3  diffuseColor;
    int     useDiffuseTexture;

    float3  specularColor;
    int     specularTextureType;

    float3  emissiveColor;
    int     useEmissiveTexture;

    float   roughness;
    float   opacity;
    int     useNormalsTexture;
    int     materialID;
};

#endif // MATERIAL_CB_H