
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"

#ifdef VULKAN

SamplerState g_samplerState : register(s6);
Texture2D<float> g_blendMask : register(t6);

#else

SamplerState g_samplerState : register(s0);
Texture2D<float> g_blendMask : register(t2);

#endif


float4 main(VS_OUTPUT input) : SV_TARGET
{   
    float2 screenUvs = input.screenPos.xy;
    screenUvs = screenUvs * float2(0.5, -0.5) + float2(0.5, 0.5);
    screenUvs = screenUvs * (g_uvPrepassBounds.zw - g_uvPrepassBounds.xy) + g_uvPrepassBounds.xy;
    screenUvs = clamp(screenUvs, g_uvPrepassBounds.xy, g_uvPrepassBounds.zw);

    float alpha = g_blendMask.Sample(g_samplerState, screenUvs);

    return float4(0, 0, 0, alpha);
}