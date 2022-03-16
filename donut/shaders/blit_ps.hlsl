Texture2D tex : register(t0);
SamplerState samp : register(s0);

void main(
	in float4 pos : SV_Position,
	in float2 uv : UV,
	out float4 o_rgba : SV_Target)
{
	o_rgba = tex.Sample(samp, uv);
}
