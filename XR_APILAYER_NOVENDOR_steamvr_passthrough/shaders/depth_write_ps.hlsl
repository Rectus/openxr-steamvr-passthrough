

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


float4 main(VS_OUTPUT input) : SV_Target
{
	float outProjectionConfidence = input.projectionConfidence.x;
	float outBlendValidity = input.cameraBlendConfidence.x;	
	
	// Written channels are selected with the pipeline.
	return float4(outProjectionConfidence, outProjectionConfidence, outBlendValidity, outBlendValidity);
}
