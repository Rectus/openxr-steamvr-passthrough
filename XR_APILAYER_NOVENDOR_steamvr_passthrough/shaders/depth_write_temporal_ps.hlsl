

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float> g_prevDepthMap : register(t0);
Texture2D<float4> g_prevCameraValidation : register(t1);

float4 main(VS_OUTPUT input, out float outDepth : SV_Depth ) : SV_Target
{
	float outProjectionConfidence = input.projectionConfidence.x;
	float outBlendValidity = input.cameraBlendConfidence.x;
	
	outDepth = input.position.z;
	
	if(input.projectionConfidence.x < 0.5 || input.cameraBlendConfidence.x < 0.5)
    {
		float2 prevScreenUvs = (input.prevHMDFrameScreenPos.xy / input.prevHMDFrameScreenPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
		prevScreenUvs.y = 1 - prevScreenUvs.y;
		
		prevScreenUvs = clamp(prevScreenUvs, float2(0, 0), float2(1, 1));
		
		float prevDepth = g_prevDepthMap.SampleLevel(g_samplerState, prevScreenUvs, 0);
		float4 prevValid4 = g_prevCameraValidation.SampleLevel(g_samplerState, prevScreenUvs, 0);
		
		float prevProjectionConfidence = g_doCutout ? prevValid4.y : prevValid4.x;
		
		float depthDiff = (prevDepth - input.position.z) / (g_depthRange.y - g_depthRange.x);
		
		if(prevProjectionConfidence >= input.projectionConfidence.x && abs(depthDiff) < g_depthTemporalFilterDistance)// && prevDepth > input.position.z)
        {
			outDepth = lerp(outDepth, prevDepth, min(g_depthTemporalFilterFactor, 0.9999));
			outProjectionConfidence = lerp(outProjectionConfidence, prevProjectionConfidence, g_depthTemporalFilterFactor);;
			outBlendValidity = min(input.cameraBlendConfidence.x, g_doCutout ? prevValid4.w : prevValid4.z);
        }
    }

	// Written channels are selected with the pipeline.
	return float4(outProjectionConfidence, outProjectionConfidence, outBlendValidity, outBlendValidity);
}