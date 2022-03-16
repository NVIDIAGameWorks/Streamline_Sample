
#pragma pack_matrix(row_major)
#include <donut/shaders/depth_cb.h>

cbuffer c_Depth : register(b0)
{
    DepthPassConstants g_Depth;
};

void main(
	in float3 i_pos : POSITION,
    in float2 i_uv : UV,
    in float3x4 i_instanceMatrix : TRANSFORM,
	in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out float2 o_uv : UV)
{
    float3x4 instanceMatrix = i_instanceMatrix;

	float4 worldPos = float4(mul(instanceMatrix, float4(i_pos, 1.0)), 1.0);
	o_position = mul(worldPos, g_Depth.matWorldToClip);

    o_uv = i_uv;
}
