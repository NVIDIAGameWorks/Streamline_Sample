#pragma pack_matrix(row_major)

#include <donut/shaders/sky_cb.h>
#include <donut/shaders/atmospheric.hlsli>

cbuffer c_Sky : register(b0)
{
    SkyConstants g_Sky;
};

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0
)
{
    float4 clipPos;
    clipPos.x = i_uv.x * 2 - 1;
    clipPos.y = 1 - i_uv.y * 2;
    clipPos.z = 0.5;
    clipPos.w = 1;
    float4 translatedWorldPos = mul(clipPos, g_Sky.matClipToTranslatedWorld);
    float3 direction = normalize(translatedWorldPos.xyz / translatedWorldPos.w);

    AtmosphereColorsType atmosphereColors = CalculateAtmosphericScattering(direction.xzy, g_Sky.directionToSun.xzy, g_Sky.lightIntensity, g_Sky.angularSizeOfLight);
    float3 color = atmosphereColors.RayleighColor + atmosphereColors.MieColor;
    o_color = float4(color, 1);
}
