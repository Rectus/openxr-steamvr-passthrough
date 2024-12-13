

#include "common_ps.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


//[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
	if(g_doCutout)
    {
        clip(input.projectionValidity);
    }
    return float4(0, 0, 0, 1.0);
}