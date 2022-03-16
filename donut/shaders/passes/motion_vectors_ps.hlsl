#pragma pack_matrix(row_major)

#include <donut/shaders/taa_cb.h>

cbuffer c_TemporalAA : register(b0)
{
    TemporalAntiAliasingConstants g_TemporalAA;
};

Texture2D<float> t_GBufferDepth : register(t0);
#if USE_STENCIL
Texture2D<uint2> t_GBufferStencil : register(t1);
#endif

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0
)
{
    o_color = 0;

#if USE_STENCIL
    uint stencil = t_GBufferStencil[i_position.xy].y;
    if ((stencil & g_TemporalAA.stencilMask) == g_TemporalAA.stencilMask)
        discard;
#endif
    float depth = t_GBufferDepth[i_position.xy].x;
    
    float4 clipPos;
    clipPos.x = i_uv.x * 2 - 1;
    clipPos.y = 1 - i_uv.y * 2;
    clipPos.z = depth;
    clipPos.w = 1;

    float4 prevClipPos = mul(clipPos, g_TemporalAA.reprojectionMatrix);

    if (prevClipPos.w <= 0)
        return;
    
    prevClipPos.xyz /= prevClipPos.w;
    float2 prevUV;
    prevUV.x = 0.5 + prevClipPos.x * 0.5;
    prevUV.y = 0.5 - prevClipPos.y * 0.5;

    float2 prevWindowPos = prevUV * g_TemporalAA.previousViewSize + g_TemporalAA.previousViewOrigin;

    o_color.xy = prevWindowPos.xy - i_position.xy;
}
