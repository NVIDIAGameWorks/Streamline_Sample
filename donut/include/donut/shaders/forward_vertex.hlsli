
struct SceneVertex
{
    float3 m_pos : POS;
    float2 m_uv : UV;
    centroid float3 m_normal : NORMAL;
    centroid float3 m_tangent : TANGENT;
    centroid float3 m_bitangent : BITANGENT;
};
