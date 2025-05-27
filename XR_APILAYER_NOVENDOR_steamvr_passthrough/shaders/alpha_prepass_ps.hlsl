
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"


float4 main(VS_OUTPUT input) : SV_TARGET
{
    clip(input.projectionConfidence.x);
	
    if (g_bUseDepthCutoffRange)
    {
        clip(input.screenPos.w - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - input.screenPos.w);
    }
	
    return float4(0, 0, 0, 1.0 - g_opacity);
}
