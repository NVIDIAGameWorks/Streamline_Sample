#pragma pack_matrix(row_major)

#include <donut/shaders/forward_vertex.hlsli>

struct VertexShaderOutput
{
    float4 position : SV_Position;
    SceneVertex vtx;
#if MOTION_VECTORS
    float3 prevWorldPos : PREV_WORLD_POS;
#endif
    float4 positionRight : NV_X_RIGHT;
    uint4 viewportMask : NV_VIEWPORT_MASK;
};

struct GeometryShaderOutput
{
    float4 position : SV_Position;
    SceneVertex vtx;
#if MOTION_VECTORS
    float3 prevWorldPos : PREV_WORLD_POS;
#endif
    uint viewport : SV_ViewportArrayIndex;
    float4 positionRight : NV_X_RIGHT;
    uint4 viewportMask : NV_VIEWPORT_MASK;
};

[maxvertexcount(1)]
void main(
	in triangle VertexShaderOutput i_struct[3],
    inout TriangleStream<GeometryShaderOutput> o_stream
)
{
    GeometryShaderOutput OUT;
    OUT.position = i_struct[0].position;
    OUT.vtx = i_struct[0].vtx;
#if MOTION_VECTORS
    OUT.prevWorldPos = i_struct[0].prevWorldPos;
#endif
    OUT.positionRight = i_struct[0].positionRight;
    OUT.viewportMask = i_struct[0].viewportMask;
    OUT.viewport = 0;
    o_stream.Append(OUT);
}
