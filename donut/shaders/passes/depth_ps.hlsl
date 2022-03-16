#pragma pack_matrix(row_major)

Texture2D t_Albedo : register(t0 DESCRIPTOR_SET_1);
SamplerState s_MaterialSampler : register(s0 DESCRIPTOR_SET_1);

void main(
    in float4 i_position : SV_Position,
	in float2 i_uv : UV
)
{
    float4 albedo = t_Albedo.Sample(s_MaterialSampler, i_uv);
    clip(albedo.a - 0.5);
}
