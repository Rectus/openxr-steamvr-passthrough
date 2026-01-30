
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"


float4 main(VS_OUTPUT input) : SV_TARGET
{
    if(!g_bIsAppAlphaInverted)
    {
        clip(input.projectionConfidence.x);
    }
    
    if (g_bUseDepthCutoffRange && !g_bUseFullscreenQuad)
    {
        float depth = g_bHasReversedDepth ? (1.0 - input.screenPos.z) : input.screenPos.z;
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }
    
    float outAlpha = g_bIsAppAlphaInverted ? 1.0 : 1.0 - g_opacity;
	
    return float4(0, 0, 0, outAlpha);
}
