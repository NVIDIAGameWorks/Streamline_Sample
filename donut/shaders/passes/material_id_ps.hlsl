
#include <donut/shaders/scene_material.hlsli>

void main(
    in float4 i_position : SV_Position,
	in SceneVertex i_vtx,
    out uint o_channel0 : SV_Target0
)
{
    o_channel0 = uint(g_Material.materialID);
}

void main_alpha_tested(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    out uint o_channel0 : SV_Target0
)
{
    SurfaceParams surface = EvaluateSceneMaterial(i_vtx);

    clip(surface.opacity - 0.5);

    o_channel0 = uint(g_Material.materialID);
}
