
#include "common_ps.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    clip(input.projectionValidity);
	
    return float4(0, 0, 0, 1.0 - g_opacity);
}