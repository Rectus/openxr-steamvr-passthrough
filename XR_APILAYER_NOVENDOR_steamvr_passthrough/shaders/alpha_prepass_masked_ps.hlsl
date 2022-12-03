
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float2 uvCoords : TEXCOORD0;
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

cbuffer psMaskedConstantBuffer : register(b2)
{
	float3 g_maskedKey;
	float g_maskedFrac;
	float g_maskedSmooth;
};

SamplerState g_SamplerState : register(s0);
Texture2DArray g_Texture : register(t0);

float4 main(VS_OUTPUT input) : SV_TARGET
{
	float4 color = g_Texture.Sample(g_SamplerState, float3((input.uvCoords * g_uvPrepassFactor + g_uvPrepassOffset).xy, float(g_arrayIndex)));

	float3 distSqr = pow(LinearRGBtoLAB_D65(color.xyz) - LinearRGBtoLAB_D65(g_maskedKey), 2);

	float maskedSqr = pow(g_maskedFrac * 100.0, 2);
	float smoothSqr = pow(g_maskedSmooth * 100.0, 2);
	color.a = smoothstep(maskedSqr, maskedSqr + smoothSqr, (distSqr.x + distSqr.y + distSqr.z));

	return color;
}