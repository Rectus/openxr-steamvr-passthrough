
#include "common_ps.hlsl"
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


#ifdef VULKAN

SamplerState g_samplerState : register(s5);
Texture2D g_blendMask : register(t5);

#else

SamplerState g_samplerState : register(s0);
Texture2D g_blendMask : register(t2);

#endif


float4 main(VS_OUTPUT input) : SV_TARGET
{   
    float2 screenUvs = input.screenCoords.xy;
    screenUvs = screenUvs * float2(0.5, -0.5) + float2(0.5, 0.5);
    screenUvs = screenUvs * (g_uvPrepassBounds.zw - g_uvPrepassBounds.xy) + g_uvPrepassBounds.xy;
    screenUvs = clamp(screenUvs, g_uvPrepassBounds.xy, g_uvPrepassBounds.zw);

    float alpha = g_blendMask.Sample(g_samplerState, screenUvs).x;

    return float4(0, 0, 0, alpha);
}