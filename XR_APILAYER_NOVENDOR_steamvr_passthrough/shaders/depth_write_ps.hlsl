

#include "common_ps.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


//[earlydepthstencil]
float main(VS_OUTPUT input, out float depth : SV_Depth) : SV_TARGET
{
	if(g_doCutout)
    {
        clip(input.projectionValidity);
    }
	
	depth = input.position.z;
	
    return g_doCutout ? input.projectionValidity : 0;
}