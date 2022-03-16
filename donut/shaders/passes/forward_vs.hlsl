#pragma pack_matrix(row_major)

#include <donut/shaders/forward_cb.h>
#include <donut/shaders/forward_vertex.hlsli>

cbuffer c_ForwardView : register(b0 DESCRIPTOR_SET_1)
{
    ForwardShadingViewConstants g_ForwardView;
};

void main(
	in SceneVertex i_vtx,
    in float4 i_instanceMatrix0 : TRANSFORM0,
    in float4 i_instanceMatrix1 : TRANSFORM1,
    in float4 i_instanceMatrix2 : TRANSFORM2,
	in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx
#if SINGLE_PASS_STEREO
    , out float4 o_positionRight : NV_X_RIGHT
    , out uint4 o_viewportMask : NV_VIEWPORT_MASK
#endif
)
{
    float3x4 instanceMatrix = float3x4(i_instanceMatrix0, i_instanceMatrix1, i_instanceMatrix2);

    o_vtx = i_vtx;
	o_vtx.m_pos = mul(instanceMatrix, float4(i_vtx.m_pos, 1.0)).xyz;
    o_vtx.m_normal = mul(instanceMatrix, float4(i_vtx.m_normal, 0)).xyz;
    o_vtx.m_tangent = mul(instanceMatrix, float4(i_vtx.m_tangent, 0)).xyz;
    o_vtx.m_bitangent = mul(instanceMatrix, float4(i_vtx.m_bitangent, 0)).xyz;

    float4 worldPos = float4(o_vtx.m_pos, 1.0);
    o_position = mul(worldPos, g_ForwardView.leftView.matWorldToClip);
#if SINGLE_PASS_STEREO
    o_positionRight = mul(worldPos, g_ForwardView.rightView.matWorldToClip);
    o_viewportMask = 0x00020001;
#endif
}
