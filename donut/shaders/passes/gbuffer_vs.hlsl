#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/forward_vertex.hlsli>

cbuffer c_GBuffer : register(b0 DESCRIPTOR_SET_1)
{
    GBufferFillConstants c_GBuffer;
};

void main(
    in SceneVertex i_vtx,
    in float4 i_instanceMatrix0 : TRANSFORM0,
    in float4 i_instanceMatrix1 : TRANSFORM1,
    in float4 i_instanceMatrix2 : TRANSFORM2,
#if MOTION_VECTORS
    in float4 i_prevInstanceMatrix0 : PREV_TRANSFORM0,
    in float4 i_prevInstanceMatrix1 : PREV_TRANSFORM1,
    in float4 i_prevInstanceMatrix2 : PREV_TRANSFORM2,
#endif
    in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx
#if MOTION_VECTORS
    , out float3 o_prevWorldPos : PREV_WORLD_POS
#endif
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
#if MOTION_VECTORS
    float3x4 prevInstanceMatrix = float3x4(i_prevInstanceMatrix0, i_prevInstanceMatrix1, i_prevInstanceMatrix2);
    o_prevWorldPos = mul(prevInstanceMatrix, float4(i_vtx.m_pos, 1.0)).xyz;
#endif

    float4 worldPos = float4(o_vtx.m_pos, 1.0);
    float4 viewPos = mul(worldPos, c_GBuffer.leftView.matWorldToView);
    o_position = mul(viewPos, c_GBuffer.leftView.matViewToClip);
#if SINGLE_PASS_STEREO
    float4 viewPosRight = mul(worldPos, c_GBuffer.rightView.matWorldToView);
    o_positionRight = mul(viewPosRight, c_GBuffer.rightView.matViewToClip);
    o_viewportMask = 0x00020001;
#endif
}
