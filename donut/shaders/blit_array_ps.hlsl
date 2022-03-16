
#include "blit_cb.h"

Texture2DArray tex : register(t0);
SamplerState samp : register(s0);

cbuffer c_Blit : register(b0)
{
    BlitConstants g_Blit;
};

void main(
	in float4 pos : SV_Position,
	in float2 uv : UV,
	out float4 o_rgba : SV_Target)
{
	o_rgba = tex.Sample(samp, float3(uv, g_Blit.sourceSlice));
}
