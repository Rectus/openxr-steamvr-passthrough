
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"

SamplerState g_samplerState : register(s0);
Texture2DArray g_texture : register(t2);

float main(VS_OUTPUT input) : SV_TARGET
{
    float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), g_uvPrepassBounds.xw, g_uvPrepassBounds.zy);
    
    float alpha = g_texture.Sample(g_samplerState, float3(screenUvs, 0)).a;
    return  alpha < g_alphaTestThreshold ? 1.0 - g_opacity : 1.0;
}
