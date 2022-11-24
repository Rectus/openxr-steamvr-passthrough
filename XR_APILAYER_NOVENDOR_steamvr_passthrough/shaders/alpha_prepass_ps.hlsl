

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 uvCoords : TEXCOORD0;
};

cbuffer psPassConstantBuffer : register(b0)
{
	float g_opacity;
	float g_brightness;
	float g_contrast;
	float g_saturation;
};

cbuffer psViewConstantBuffer : register(b1)
{
	float2 g_uvOffset;
	float2 g_uvPrepassFactor;
	float2 g_uvPrepassOffset;
	uint g_arrayIndex;
};

float4 main(VS_OUTPUT input) : SV_TARGET
{
	return float4(0, 0, 0, 1.0 - g_opacity);

}