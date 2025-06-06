

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float> g_prevDepthMap : register(t0);
Texture2D<half4> g_prevCameraValidation : register(t1);


float sobel_discontinuity_adjust(in Texture2D<float> depthTex, in SamplerState texSampler, in float depth, in float2 uvs, float2 texSize)
{
    float outDepth = depth;
    
    uint2 pixelPos = floor(saturate(uvs) * texSize);
    
    float dispU = depthTex.Load(int3(pixelPos + uint2(0, -1), 0));
    float dispD = depthTex.Load(int3(pixelPos + uint2(0, 1), 0));
    float dispL = depthTex.Load(int3(pixelPos + uint2(-1, 0), 0));
    float dispR = depthTex.Load(int3(pixelPos + uint2(1, 0), 0));
            
    float dispUL = depthTex.Load(int3(pixelPos + uint2(-1, -1), 0));
    float dispDL = depthTex.Load(int3(pixelPos + uint2(-1, 1), 0));
    float dispUR = depthTex.Load(int3(pixelPos + uint2(1, -1), 0));
    float dispDR = depthTex.Load(int3(pixelPos + uint2(1, 1), 0));
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    float minDepth = min(depth, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
    float maxDepth = max(depth, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));
    
    float magnitude = length(float2(filterX, filterY));

    if(magnitude > g_depthContourTreshold)
    {
        bool inForeground = ((maxDepth - depth) > (depth - minDepth));

        float offsetFactor = saturate(g_depthContourStrength * 10.0 * magnitude);
        
        outDepth = lerp(depth, inForeground ? minDepth : maxDepth, offsetFactor);
    }
    return outDepth;
}


float4 main(VS_OUTPUT input, out float outDepth : SV_Depth ) : SV_Target
{
	float outProjectionConfidence = input.projectionConfidence.x;
	
	float outBlendValidity = input.cameraBlendConfidence.x;
	
	outDepth = input.position.z;
	
	if(input.projectionConfidence.x < 0.5 || input.cameraBlendConfidence.x < 0.5)
    {
		float2 prevScreenUvs = Remap(input.prevHMDFrameScreenPos.xy / input.prevHMDFrameScreenPos.w, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
		
        float borderRejectSize = 0.01;
        
		bool bPrevUVsValid = 
            prevScreenUvs.x > borderRejectSize && 
            prevScreenUvs.y > borderRejectSize && 
            prevScreenUvs.x < 1.0 - borderRejectSize && 
            prevScreenUvs.y < 1.0 - borderRejectSize;
		
		float2 historyTextureSize;
		g_prevDepthMap.GetDimensions(historyTextureSize.x, historyTextureSize.y);
		
		float prevDepth = bicubic_b_spline_4tap(g_prevDepthMap, g_samplerState, prevScreenUvs, historyTextureSize);
		float4 prevValid4 = bicubic_b_spline_4tap(g_prevCameraValidation, g_samplerState, prevScreenUvs, historyTextureSize);
        
        // Create sharp edges so that the discontinuity adjust in the main pass works properly.
        prevDepth = sobel_discontinuity_adjust(g_prevDepthMap, g_samplerState, prevDepth, prevScreenUvs, historyTextureSize);
        
		float prevProjectionConfidence = g_doCutout ? prevValid4.y : prevValid4.x;
		float prevBlendConfidence = g_doCutout ? prevValid4.w : prevValid4.z;
		
		float depthDiff = abs((prevDepth - input.position.z) * (g_depthRange.y - g_depthRange.x));
		
		if(bPrevUVsValid && prevProjectionConfidence >= outProjectionConfidence && prevDepth > 0 && depthDiff <= g_depthTemporalFilterDistance)
        {
			float lerpFactor = g_depthTemporalFilterFactor;
			outDepth = lerp(outDepth, prevDepth, lerpFactor);
			outProjectionConfidence = lerp(outProjectionConfidence, prevProjectionConfidence, lerpFactor);
			outBlendValidity = lerp(input.cameraBlendConfidence.x, prevBlendConfidence, lerpFactor);
        }
    }

	// Written channels are selected with the pipeline.
	return float4(outProjectionConfidence, outProjectionConfidence, outBlendValidity, outBlendValidity);
}
