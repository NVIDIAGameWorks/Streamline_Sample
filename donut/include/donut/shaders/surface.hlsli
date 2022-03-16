
struct SurfaceParams
{
    float4 clipPos;
    float3 worldPos;
    float3 geometryNormal;
    float3 normal;
    float3 diffuseColor;
    float3 specularColor;
    float3 emissiveColor;
    float opacity;
    float roughness;
};

SurfaceParams DefaultSurfaceParams()
{
    SurfaceParams result;
    result.clipPos = float4(0, 0, 0, 1);
    result.worldPos = 0;
    result.geometryNormal = 0;
    result.normal = 0;
    result.diffuseColor = 0;
    result.specularColor = 0;
    result.emissiveColor = 0;
    result.opacity = 1;
    result.roughness = 0;
    return result;
}
