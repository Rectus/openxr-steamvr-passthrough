

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"



float4 main(VS_OUTPUT input) : SV_Target
{
	float outProjectionConfidence = input.projectionConfidence.x;
	// Remap values to half to allow negative ones for history differentiation.
	float outBlendValidity = saturate(input.cameraBlendConfidence.x) * 0.5 + 0.5;
	
	
	// Written channels are selected with the pipeline.
	return float4(outProjectionConfidence, outProjectionConfidence, outBlendValidity, outBlendValidity);
}