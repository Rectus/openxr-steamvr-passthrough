
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"
#include "fullscreen_util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);
Texture2D<float> g_depthMap : register(t3);
Texture2D<float> g_crossDepthMap : register(t4);
Texture2D<half4> g_prevCameraHistory : register(t5);

RWTexture2D<half4> g_cameraHistory : register(u1);

struct PS_Output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};




PS_Output main(VS_OUTPUT input)
{
    float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
    
    float depth = g_depthMap.Sample(g_samplerState, screenUvs);
    float crossDepth = g_crossDepthMap.Sample(g_samplerState, screenUvs);
    float4 cameraValidation = g_cameraValidation.Sample(g_samplerState, screenUvs);
    
    float2 projectionConfidence = cameraValidation.xy;
    float2 cameraBlendValidity = cameraValidation.zw;
    
    if (g_bUseDepthCutoffRange)
    {
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }
    

    bool selectMainCamera = cameraBlendValidity.x >= cameraBlendValidity.y;      
    bool blendCameras = cameraBlendValidity.x > 0.1 && cameraBlendValidity.y > 0.1;
        
    float cameraBlend = blendCameras ? (1 - saturate(cameraBlendValidity.x + 1 - cameraBlendValidity.y)) : (selectMainCamera ? 0.0 : 1.0);
    

    bool bIsDiscontinuityFiltered = false;
    bool bIsCrossDiscontinuityFiltered = false;
    
    [branch]
    if (cameraBlend < 1.0 && g_depthContourStrength > 0)
    {
        depth = sobel_discontinuity_adjust(g_depthMap, g_samplerState, depth, screenUvs, bIsDiscontinuityFiltered);
    }
    
    [branch]
    if (cameraBlend > 0.0 && g_depthContourStrength > 0)
    {
        crossDepth = sobel_discontinuity_adjust(g_crossDepthMap, g_samplerState, crossDepth, screenUvs, bIsCrossDiscontinuityFiltered);
    }
    
    float4 clipSpacePos = float4(input.screenPos.xy, depth, 1.0);  
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    
    float4 prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldProjectionPos);	
    
    float4 crossClipSpacePos = float4(input.screenPos.xy, crossDepth, 1.0);
    float4 crossWorldProjectionPos = mul(g_HMDProjectionToWorld, crossClipSpacePos);   
    float4 cameraCrossClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionRight : g_worldToCameraFrameProjectionLeft, crossWorldProjectionPos);
    

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = Remap(cameraClipSpacePos.xy / cameraClipSpacePos.w, -1.0, 1.0, 0.0, 1.0);
    float2 crossUvs = Remap(cameraCrossClipSpacePos.xy / cameraCrossClipSpacePos.w, -1.0, 1.0, 0.0, 1.0);

    if (g_bClampCameraFrame)
    {
        clip(cameraClipSpacePos.z);
        clip(outUvs);
        clip(1 - outUvs);
    }
    
    float2 correction = 0;
    
    if (g_bUseFisheyeCorrection)
    {
        // Remap and clamp to frame UV bounds.
        outUvs = Remap(outUvs, 0.0, 1.0, g_uvBounds.xy, g_uvBounds.zw);
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
        
        crossUvs = Remap(crossUvs, 0.0, 1.0, g_crossUVBounds.xy, g_crossUVBounds.zw);
        crossUvs = clamp(crossUvs, g_crossUVBounds.xy, g_crossUVBounds.zw);
        
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, crossUvs);
        crossUvs += correction;
    }
    else
    {
        // Remap and clamp to frame UV bounds.
        outUvs = Remap(outUvs, float2(0.0, 1.0), float2(1.0, 0.0), g_uvBounds.xy, g_uvBounds.zw);
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        
        crossUvs = Remap(crossUvs, float2(0.0, 1.0), float2(1.0, 0.0), g_crossUVBounds.xy, g_crossUVBounds.zw);
        crossUvs = clamp(crossUvs, g_crossUVBounds.xy, g_crossUVBounds.zw);
    }
      
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).rgb;
    float3 minColor = rgbColor;
    float3 maxColor = rgbColor;
    
    float3 crossRGBColor = g_cameraFrameTexture.Sample(g_samplerState, crossUvs).rgb; 
    float3 crossMinColor = crossRGBColor;
    float3 crossMaxColor = crossRGBColor;
    
    float sharpness = g_sharpness + 0.5;

    rgbColor *= (1 + sharpness * 4);
    crossRGBColor *= (1 + sharpness * 4);
        
    float2 cameraFrameRes;
    g_cameraFrameTexture.GetDimensions(cameraFrameRes.x, cameraFrameRes.y);
    
    float3 sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / cameraFrameRes).rgb;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / cameraFrameRes).rgb;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / cameraFrameRes).rgb;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / cameraFrameRes).rgb;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(-1, 0) / cameraFrameRes).rgb;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(1, 0) / cameraFrameRes).rgb;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, -1) / cameraFrameRes).rgb;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, 1) / cameraFrameRes).rgb;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);

    
    float3 crossRGBColorClamped = min(maxColor, max(crossRGBColor, minColor));
    
    float2 camTexCoords = outUvs * cameraFrameRes;
    uint2 camPixel = floor(camTexCoords);
    
    float2 crossCamTexCoords = crossUvs * cameraFrameRes;
    uint2 crossCamPixel = floor(crossCamTexCoords);
    
    // How far the current pixel is to the sampled one
    float pixelDistanceFactor = abs(camTexCoords.x - camPixel.x - 0.5) + abs(camTexCoords.y - camPixel.y - 0.5);
    float crossPixelDistanceFactor = abs(crossCamTexCoords.x - crossCamPixel.x - 0.5) + abs(crossCamTexCoords.y - crossCamPixel.y - 0.5);
    
    float pixelDistanceBlend = saturate(lerp(0.5, pixelDistanceFactor + 0.5 - crossPixelDistanceFactor, abs(pixelDistanceFactor - crossPixelDistanceFactor)));
    
    float depthFactor = saturate(1 - (abs(depth - crossDepth) * 1000));
    
    float combineFactor = g_cutoutCombineFactor * depthFactor * projectionConfidence.x * projectionConfidence.y;
  
    float finalCameraBlendFactor = lerp(cameraBlend, pixelDistanceBlend, combineFactor);
    
    // Blend together both cameras based on which ones are valid and have the closest pixels.
    rgbColor = lerp(rgbColor, lerp(crossRGBColor, crossRGBColorClamped, combineFactor), finalCameraBlendFactor);

    minColor = lerp(minColor, lerp(crossMinColor, minColor, combineFactor), finalCameraBlendFactor);
    maxColor = lerp(maxColor, lerp(crossMaxColor, maxColor, combineFactor), finalCameraBlendFactor);
    
    float finalDepth = lerp(depth, crossDepth, finalCameraBlendFactor);
    float finalConfidence = lerp(projectionConfidence.x, projectionConfidence.y, finalCameraBlendFactor);
    
    
    float2 historyTextureSize;
    g_prevCameraHistory.GetDimensions(historyTextureSize.x, historyTextureSize.y);
    
    float2 prevScreenUvs = Remap((prevCameraFrameScreenPos.xy / prevCameraFrameScreenPos.w), float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
     
    float2 prevTexCoords = prevScreenUvs * historyTextureSize;
    uint2 prevPixel = floor(prevTexCoords);
    
    float4 historyColor;
    
    [branch]
    if (g_temporalFilteringSampling == 1)
    {
        historyColor = g_prevCameraHistory.SampleLevel(g_samplerState, prevScreenUvs, 0);
    }
    else if (g_temporalFilteringSampling == 2)
    {
        historyColor = bicubic_b_spline_4tap(g_prevCameraHistory, g_samplerState, prevScreenUvs, historyTextureSize);
    }
    else if (g_temporalFilteringSampling == 3)
    {
        historyColor = catmull_rom_9tap(g_prevCameraHistory, g_samplerState, prevScreenUvs, historyTextureSize);
    }
    else if (g_temporalFilteringSampling == 4)
    {
        historyColor = lanczos2(g_prevCameraHistory, prevScreenUvs, historyTextureSize);
    }
    else
    {
        historyColor = g_prevCameraHistory.Load(uint3(prevPixel, 0));
    }
    
    
    float prevPixelDistanceFactor = clamp((abs(prevTexCoords.x - prevPixel.x - 0.5) + abs(prevTexCoords.y - prevPixel.y - 0.5)), 0.05, 1);

    float finalHistoryFactor = clamp(min(finalConfidence * 5.0, 1.0 - prevPixelDistanceFactor * 0.5), 0, g_temporalFilteringFactor);
    
    if (finalConfidence < 0.1 || bIsDiscontinuityFiltered || bIsCrossDiscontinuityFiltered) 
    { 
        finalHistoryFactor = 0; 
    }
    
    // Clip history color to AABB of neighborhood color values + some configurable leeway.  
    float3 historyColorClipped = min(maxColor * (1.0 + g_temporalFilteringColorRangeCutoff), max(historyColor.rgb, minColor * (1.0 - g_temporalFilteringColorRangeCutoff))); 
    
    float clipFactor = any(historyColor.rgb - historyColorClipped) ? 1.0 : 0.0;
    
    if(historyColor.a > 0.0)
    {
        clipFactor = lerp(clipFactor, historyColor.a, finalHistoryFactor);
    }
    
    historyColor.rgb = lerp(historyColor.rgb, historyColorClipped, clipFactor);
    
    rgbColor = lerp(rgbColor, historyColor.rgb, finalHistoryFactor);
    
    
    if(g_bIsFirstRenderOfCameraFrame)
    {     
        g_cameraHistory[floor(screenUvs * historyTextureSize)] = float4(rgbColor, clipFactor);
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
        float debugDepth = pow(abs(finalDepth), g_depthRange.y * 5.0);
        rgbColor = float3(debugDepth, debugDepth, debugDepth);
    }
    if (g_debugOverlay == 1) // Confidence
    {
        if (finalConfidence < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else if (finalConfidence > 0.0)
        {
            rgbColor.g += projectionConfidence.x * 0.25;
        }
        else
        {
            rgbColor.b += 0.5;
        }
    }
    else if (g_debugOverlay == 2) // Camera selection
    {
        if(g_cutoutCombineFactor)
        {
            if(finalCameraBlendFactor < 1.0)
            {
                rgbColor.b += finalCameraBlendFactor;
            }
            else
            {
                rgbColor.g += 1.0;
            }
        }
        else
        {
            rgbColor.g += finalCameraBlendFactor;
        }
    }
    else if (g_debugOverlay == 3) // Temporal blend
    {
        if (finalHistoryFactor >= g_temporalFilteringFactor)
        {
            rgbColor.g += finalHistoryFactor;
        }
        else
        {
            rgbColor.b += finalHistoryFactor;
        }
    }
    else if (g_debugOverlay == 4) // Temporal clipping
    {
        if(clipFactor >= 1.0)
        {
            rgbColor.r += 0.5;
        }
        else
        {
            rgbColor.b += clipFactor;
        }
    }
    else if (g_debugOverlay == 5) // Discontinuity filtering
    {
        if (bIsDiscontinuityFiltered)
        {
            rgbColor.g += 1.0;
        }
        if (bIsCrossDiscontinuityFiltered)
        {
            rgbColor.b += 1.0;
        }
    }
    
    PS_Output output;
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity : rgbColor;
	
    output.color = float4(rgbColor, g_opacity);
    output.depth = finalDepth;
    
    return output;
}
