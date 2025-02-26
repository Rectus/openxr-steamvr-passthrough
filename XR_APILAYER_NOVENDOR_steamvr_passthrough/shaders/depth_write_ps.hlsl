

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"



float4 main(VS_OUTPUT input) : SV_Target
{
	float outBlendValidity = input.projectionConfidence;
	float outTempValidity = input.projectionConfidence;
	
	// Written channels are selected with the pipeline.
	return float4(outTempValidity, outTempValidity, outBlendValidity, outBlendValidity);
}