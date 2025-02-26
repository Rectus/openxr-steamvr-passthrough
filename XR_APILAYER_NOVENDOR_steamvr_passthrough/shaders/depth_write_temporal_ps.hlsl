

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float> g_prevDepthMap : register(t0);
Texture2D<float4> g_prevCameraValidation : register(t1);

float4 main(VS_OUTPUT input, out float outDepth : SV_Depth ) : SV_Target
{
	float outProjectionValidity = input.projectionConfidence.x;
	float outBlendValidity = input.cameraBlendConfidence.x;
	
	outDepth = input.position.z;
	
	if(input.projectionConfidence.x < 0.5)
    {
		float2 prevScreenUvs = (input.prevHMDFrameCameraReprojectedPos.xy / input.prevHMDFrameCameraReprojectedPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
		prevScreenUvs.y = 1 - prevScreenUvs.y;
		
		float prevDepth = g_prevDepthMap.SampleLevel(g_samplerState, prevScreenUvs, 0);
		float4 prevValid4 = g_prevCameraValidation.SampleLevel(g_samplerState, prevScreenUvs, 0);
		
		float prevValidity = g_doCutout ? prevValid4.z : prevValid4.w;
		
		if(prevValidity >= input.projectionConfidence.x && abs(prevDepth - input.position.z) < 0.5 && prevDepth > input.position.z)
        {
			outDepth = prevDepth;
			outBlendValidity = g_doCutout ? prevValid4.x : prevValid4.y;
			outProjectionValidity = prevValidity;
        }
    }

	// Written channels are selected with the pipeline.
	return float4(outProjectionValidity, outProjectionValidity, outBlendValidity, outBlendValidity);
}