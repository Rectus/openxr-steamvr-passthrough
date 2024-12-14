

#include "common_ps.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


float main(VS_OUTPUT input) : SV_TARGET
{
	if(g_doCutout)
    {
        clip(input.projectionValidity);
    }
	
	// Write validity value 0 - 0.5 for primary camera and 0.5 - 1 for secondary.
    return g_doCutout ? 0.5 - saturate(input.projectionValidity) * 0.5 : saturate(input.projectionValidity) * 0.5 + 0.51;
}