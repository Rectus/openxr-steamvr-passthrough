
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

Texture2D<half4> g_prevCameraHistory : register(t2);
RWTexture2D<half4> g_cameraHistory : register(u1);




//[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    float alpha = 1.0;
	
    if (g_doCutout)
    {
        alpha = saturate(input.cameraBlendConfidence.x);
        clip(input.cameraBlendConfidence.x);
    }
    
    if (g_bUseDepthCutoffRange)
    {
        float depth = g_bHasReversedDepth ? (1.0 - input.screenPos.z) : input.screenPos.z;
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = (input.cameraReprojectedPos.xy / input.cameraReprojectedPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
	
    if (g_bClampCameraFrame)
    {
        clip(input.cameraReprojectedPos.z);
        clip(outUvs);
        clip(1 - outUvs);
    }
    
	// Remap and clamp to frame UV bounds.
	outUvs = outUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
	outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);

    float2 correction = 0;
    
    if (g_bUseFisheyeCorrection)
    {
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
    }
    else
    {
        outUvs.y = 1 - outUvs.y;
    }
	
    uint texW, texH;
    g_cameraFrameTexture.GetDimensions(texW, texH);
    int2 cameraFrameRes = uint2(texW, texH);

    g_prevCameraHistory.GetDimensions(texW, texH);
    int2 outputFrameRes = uint2(texW, texH);
    
    
    float3 rgbColor = 0; 
    float3 minColor = 1;
    float3 maxColor = 0;
    
    float sharpness = g_sharpness + 0.5;
    
    float3 sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;

    minColor = sample;
    maxColor = sample;
        
    float dist = 1;
        
    rgbColor = sample * (1 + sharpness * 4);
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-dist, 0) / cameraFrameRes).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(dist, 0) / cameraFrameRes).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -dist) / cameraFrameRes).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, dist) / cameraFrameRes).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
      
    
    float2 prevScreenUvs = (input.prevCameraFrameScreenPos.xy / input.prevCameraFrameScreenPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    prevScreenUvs.y = 1 - prevScreenUvs.y;
    
    float2 newScreenUvs = (input.screenPos.xy / input.screenPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    newScreenUvs.y = 1 - newScreenUvs.y;
     
    float2 prevTexCoords = prevScreenUvs * outputFrameRes;
    uint2 prevPixel = floor(prevTexCoords);
    
    float4 filtered;
    
    [branch]
    if (g_temporalFilteringSampling == 0)
    {
        filtered = g_prevCameraHistory.Load(uint3(prevPixel, 0));
    }
    else if (g_temporalFilteringSampling == 1)
    {
        filtered = g_prevCameraHistory.SampleLevel(g_samplerState, prevScreenUvs, 0);
    }
    else if (g_temporalFilteringSampling == 2)
    {
        filtered = bicubic_b_spline_4tap(g_prevCameraHistory, g_samplerState, prevScreenUvs, outputFrameRes);
    }
    else if (g_temporalFilteringSampling == 3)
    {
        filtered = catmull_rom_9tap(g_prevCameraHistory, g_samplerState, prevScreenUvs, outputFrameRes);
    }
    else
    {
        filtered = lanczos2(g_prevCameraHistory, prevScreenUvs, outputFrameRes);
    }
    
    //filtered.a = g_prevCameraHistory.Load(uint3(prevPixel, 0)).a;
    
    
    // Clip history color to AABB of neighborhood color values + some configurable leeway.
    
    float3 filteredClipped = min(maxColor * (1.0 + g_temporalFilteringColorRangeCutoff), max(filtered.xyz, minColor * (1.0 - g_temporalFilteringColorRangeCutoff)));
    
    // Flicker reduction attempt based on: Callum Glover - Temporal Anti Aliasing Implementation and Extensions
    // https://static1.squarespace.com/static/5a3beb72692ebe77330b5118/t/5c9d4f5be2c483f0c4108eca/1553813352302/report.pdf
    
    float isClipped = any(filtered.xyz - filteredClipped) ? 1 : 0;
    
    filtered.xyz = isClipped != 0 ? lerp(filtered.xyz, filteredClipped, filtered.a) : filtered.xyz;
    
    float2 camTexCoords = outUvs * cameraFrameRes;
    uint2 camPixel = floor(camTexCoords);
    
    // How far the current pixel is to the sampled one
    float confidenceInv = abs(camTexCoords.x - camPixel.x - 0.5) + abs(camTexCoords.y - camPixel.y - 0.5);
    
    float prevConfidenceInv = clamp((abs(prevTexCoords.x - prevPixel.x - 0.5) + abs(prevTexCoords.y - prevPixel.y - 0.5)),
0.05, 1);  
    
    float vLenSq = dot(input.prevCameraFrameVelocity, input.prevCameraFrameVelocity);
    float factor = saturate(min(g_temporalFilteringFactor, 1 - vLenSq * 500));
    float confidence = confidenceInv + (1 - prevConfidenceInv);

    float finalFactor = clamp(factor * confidence, 0, g_temporalFilteringFactor);
    
    rgbColor = lerp(rgbColor, filtered.xyz, finalFactor);
    
    float clipHistory = (filtered.a == 0) ? isClipped : lerp(isClipped, filtered.a, finalFactor);
    
    if(g_bIsFirstRenderOfCameraFrame)
    {
        g_cameraHistory[floor(newScreenUvs * outputFrameRes)] = float4(rgbColor, input.projectionConfidence.x >= 0 ? clipHistory : 1);
    }
    
	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(rgbColor);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		rgbColor = LABtoLinearRGB_D65(float3(LBis, ab.xy));
	}

    if (g_bDebugDepth)
    {
        float depth = (g_bHasReversedDepth ? ((g_depthRange.y - g_depthRange.x) - input.screenPos.z) : input.screenPos.z) / (g_depthRange.y - g_depthRange.x) - g_depthRange.x;
        rgbColor = float3(depth, depth, depth);
    }
    if (g_debugOverlay == 1) // Confidence
    {
        if (input.projectionConfidence.x < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else if (input.projectionConfidence.x > 0.0)
        {
            rgbColor.g += input.projectionConfidence.x * 0.25;
        }
        else
        {
            rgbColor.b += 0.25;
        }
    }
    else if (g_debugOverlay == 2) // Camera selection
    {
        if (!g_doCutout)
        {
            rgbColor.g += 1.0;
        }
    }
    else if (g_debugOverlay == 3) // Temporal blend
    {
        rgbColor.g += finalFactor;
    }
    else if (g_debugOverlay == 4) // Temporal clipping
    {
        rgbColor.b += clipHistory;
    }
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
    
    return float4(rgbColor, g_opacity * alpha);
}
