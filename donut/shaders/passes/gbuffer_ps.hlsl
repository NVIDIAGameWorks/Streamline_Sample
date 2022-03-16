#pragma pack_matrix(row_major)

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/motion_vectors.hlsli>
#include <donut/shaders/gbuffer_cb.h>

cbuffer c_GBuffer : register(b1 DESCRIPTOR_SET_1)
{
    GBufferFillConstants c_GBuffer;
};

void main(
    in float4 i_position : SV_Position,
	in SceneVertex i_vtx,
#if MOTION_VECTORS
    in float3 i_prevWorldPos : PREV_WORLD_POS,
#endif
#if SINGLE_PASS_STEREO
    in uint i_viewport : SV_ViewportArrayIndex,
#endif
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2
#if MOTION_VECTORS
    , out float2 o_motion : SV_Target3
#endif
)
{
    SurfaceParams surface = EvaluateSceneMaterial(i_vtx);

    clip(surface.opacity - 0.5);

    o_channel0.xyz = surface.diffuseColor;
    o_channel0.w = surface.opacity;
    if (any(surface.emissiveColor > 0))
    {
        o_channel1.xyz = surface.emissiveColor;
        o_channel1.w = 1;
    }
    else
    {
        o_channel1.xyz = surface.specularColor;
        o_channel1.w = 0;
    }
    o_channel2.xyz = surface.normal;
    o_channel2.w = surface.roughness;

#if MOTION_VECTORS
#if SINGLE_PASS_STEREO
    if(i_viewport != 0)
        o_motion = GetMotionVector(i_position.xy, i_prevWorldPos, c_GBuffer.rightView, c_GBuffer.rightViewPrev);
    else
        o_motion = GetMotionVector(i_position.xy, i_prevWorldPos, c_GBuffer.leftView, c_GBuffer.leftViewPrev);
#else
    o_motion = GetMotionVector(i_position.xy, i_prevWorldPos, c_GBuffer.leftView, c_GBuffer.leftViewPrev);
#endif
#endif
}
