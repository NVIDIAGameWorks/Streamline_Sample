/*
* Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <donut/shaders/forward_vertex.hlsli>

struct VSOutput
{
    float4 posClip : SV_Position;
    SceneVertex vtx;
};

struct GSOutput
{
    VSOutput Passthrough;
    uint ViewportMask : SV_ViewportArrayIndex;
};

#define USE_CULLING 1

int GetVertexPlaneMask(float3 v)
{ 
    return int(v.x < v.y) | 
        (int(v.x < -v.y) << 1) | 
        (int(v.x <  v.z) << 2) | 
        (int(v.x < -v.z) << 3) | 
        (int(v.z <  v.y) << 4) | 
        (int(v.z < -v.y) << 5) |
        (int(v.x <    1) << 8) |
        (int(v.x >   -1) << 9) |
        (int(v.y <    1) << 10) |
        (int(v.y >   -1) << 11) |
        (int(v.z <    1) << 12) |
        (int(v.z >   -1) << 13);
}

[maxvertexcount(1)]
void main(
    triangle VSOutput Input[3],
    inout TriangleStream<GSOutput> Output)
{
    GSOutput OutputVertex;

    OutputVertex.Passthrough = Input[0];

#if USE_CULLING
    int pm0 = GetVertexPlaneMask(Input[0].posClip.xyz);
    int pm1 = GetVertexPlaneMask(Input[1].posClip.xyz);
    int pm2 = GetVertexPlaneMask(Input[2].posClip.xyz);
    int prim_plane_mask_0 = pm0 & pm1 & pm2;
    int prim_plane_mask_1 = ~pm0 & ~pm1 & ~pm2;
    int combined_mask = prim_plane_mask_0 | (prim_plane_mask_1 << 16);

    int face_mask = 0;
    if((combined_mask & 0x00010f) == 0) face_mask |= 0x01;
    if((combined_mask & 0x0f0200) == 0) face_mask |= 0x02;
    if((combined_mask & 0x110422) == 0) face_mask |= 0x04;
    if((combined_mask & 0x220811) == 0) face_mask |= 0x08;
    if((combined_mask & 0x041038) == 0) face_mask |= 0x10;
    if((combined_mask & 0x382004) == 0) face_mask |= 0x20;

    OutputVertex.ViewportMask = face_mask;
#else
    OutputVertex.ViewportMask = 0x3f;
#endif

    Output.Append(OutputVertex);
}
