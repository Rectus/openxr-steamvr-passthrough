
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 uvCoords : TEXCOORD0;
	float2 originalUVCoords : TEXCOORD1;
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
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
};

SamplerState g_SamplerState : register(s0);
Texture2DArray g_Texture : register(t0);

float main(VS_OUTPUT input) : SV_TARGET
{
	float4 color;

	if (g_bMaskedUseCamera)
	{
		float2 outUvs = input.uvCoords.xy / input.uvCoords.z;
		outUvs = outUvs * float2(-0.5, -0.5) + float2(0.5, 0.5);
		outUvs = clamp(outUvs, float2(0.0, 0.0), float2(0.5, 1.0)) + g_uvOffset;

		color = g_Texture.Sample(g_SamplerState, float3(outUvs.xy, 0));
	}
	else
	{
		color = g_Texture.Sample(g_SamplerState, float3((input.originalUVCoords * g_uvPrepassFactor + g_uvPrepassOffset).xy, float(g_arrayIndex)));
	}

	float3 difference = LinearRGBtoLAB_D65(color.xyz) - LinearRGBtoLAB_D65(g_maskedKey);

	float2 distChromaSqr = pow(difference.yz, 2);
	float fracChromaSqr = pow(g_maskedFracChroma, 2);

	float distChroma = smoothstep(fracChromaSqr, fracChromaSqr + pow(g_maskedSmooth, 2), (distChromaSqr.x + distChromaSqr.y));
	float distLuma = smoothstep(g_maskedFracLuma, g_maskedFracLuma + g_maskedSmooth, abs(difference.x));

	return max(distChroma, distLuma);
}