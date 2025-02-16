

#include "common_ps.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


float4 main(VS_OUTPUT input) : SV_TARGET
{
	// Written channels are selected with the pipeline.
	return float4(input.projectionValidity, input.projectionValidity, 0, 0);
}