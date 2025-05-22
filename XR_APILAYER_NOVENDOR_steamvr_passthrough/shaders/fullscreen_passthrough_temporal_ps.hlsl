
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);
Texture2D<float> g_depthMap : register(t3);
Texture2D<half4> g_prevCameraHistory : register(t4);

RWTexture2D<half4> g_cameraHistory : register(u1);

struct PS_Output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};



static const int GAUSSIAN_FILTER_WIDTH = 2;

float gaussian(float2 value)
{
    return exp(-0.5 * dot(value /= ((float)GAUSSIAN_FILTER_WIDTH * 2.0 * 0.25), value)) / 
        (2.0 * PI * pow((float)GAUSSIAN_FILTER_WIDTH * 2.0 * 0.25, 2));
}


float sobel_discontinuity_adjust(in Texture2D<float> tex, in float depth, in float2 uvs, out bool bWasFiltered)
{
    bWasFiltered = false;
    float outDepth = depth;
    
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 invTexSize = 1.0 / float2(texW, texH);
    
    float2 texturePos = saturate(uvs) * float2(texW, texH);
    uint2 pixelPos = floor(texturePos);
    
    float dispU = tex.Load(int3(pixelPos + uint2(0, -1), 0));
    float dispD = tex.Load(int3(pixelPos + uint2(0, 1), 0));
    float dispL = tex.Load(int3(pixelPos + uint2(-1, 0), 0));
    float dispR = tex.Load(int3(pixelPos + uint2(1, 0), 0));
            
    float dispUL = tex.Load(int3(pixelPos + uint2(-1, -1), 0));
    float dispDL = tex.Load(int3(pixelPos + uint2(-1, 1), 0));
    float dispUR = tex.Load(int3(pixelPos + uint2(1, -1), 0));
    float dispDR = tex.Load(int3(pixelPos + uint2(1, 1), 0));
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    float minDepth = min(depth, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
    float maxDepth = max(depth, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));
    
    float magnitude = length(float2(filterX, filterY));

    if(magnitude > 0.15)
    {
        float totalWeight = 0;
        float accDepth = 0;
        
        // Filter with an output pixel-centered gaussian blur to get a smooth contour over the low res depth map pixels.
        [unroll]
        for (int x = -GAUSSIAN_FILTER_WIDTH; x <= GAUSSIAN_FILTER_WIDTH; x++)
        {
            [unroll]
            for (int y = -GAUSSIAN_FILTER_WIDTH; y <= GAUSSIAN_FILTER_WIDTH; y++)
            {
                float weight = gaussian(float2(x, y));
                totalWeight += weight;
                accDepth += tex.SampleLevel(g_samplerState, uvs + float2(x, y) * invTexSize, 0) * weight;
            }
        }

        float adjustedDepth = accDepth / totalWeight;
        
        bool inForeground = ((maxDepth - adjustedDepth) > (adjustedDepth - minDepth));

        float offsetFactor = saturate(10.0 * magnitude);
        
        bWasFiltered = true;
        outDepth = lerp(depth, inForeground ? minDepth : maxDepth, offsetFactor);
    }
    return outDepth;
}


PS_Output main(VS_OUTPUT input)
{
    float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
    
    float depth = g_depthMap.Sample(g_samplerState, screenUvs);
    float4 cameraValidation = g_cameraValidation.Sample(g_samplerState, screenUvs);
    
    float projectionConfidence = cameraValidation.x;
    
    if (g_bUseDepthCutoffRange)
    {
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }  

    bool bIsDiscontinuityFiltered = false;
    bool bIsCrossDiscontinuityFiltered = false;
    
    depth = sobel_discontinuity_adjust(g_depthMap, depth, screenUvs, bIsDiscontinuityFiltered);
    
    float4 clipSpacePos = float4(input.screenPos.xy, depth, 1.0);  
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    
    float4 prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldProjectionPos);	

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = Remap(cameraClipSpacePos.xy / cameraClipSpacePos.w, -1.0, 1.0, 0.0, 1.0);

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
    }
    else
    {
        // Remap and clamp to frame UV bounds.
        outUvs = Remap(outUvs, float2(0.0, 1.0), float2(1.0, 0.0), g_uvBounds.xy, g_uvBounds.zw);
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
    }
      
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).rgb;
    float3 minColor = rgbColor;
    float3 maxColor = rgbColor;
    
    float sharpness = g_sharpness + 0.5;

    rgbColor *= (1 + sharpness * 4);
        
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
    
    float2 camTexCoords = outUvs * cameraFrameRes;
    uint2 camPixel = floor(camTexCoords);
    
    // How far the current pixel is to the sampled one
    float pixelDistanceFactor = abs(camTexCoords.x - camPixel.x - 0.5) + abs(camTexCoords.y - camPixel.y - 0.5);

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

    float finalHistoryFactor = clamp(min(projectionConfidence * 5.0, 1.0 - prevPixelDistanceFactor * 0.5), 0, g_temporalFilteringFactor);
    
    if (projectionConfidence < 0.1 || bIsDiscontinuityFiltered || bIsCrossDiscontinuityFiltered) 
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
        float debugDepth = pow(abs(depth), g_depthRange.y * 5.0);
        rgbColor = float3(debugDepth, debugDepth, debugDepth);
    }
    if (g_debugOverlay == 1) // Confidence
    {
        if (projectionConfidence < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else if (projectionConfidence > 0.0)
        {
            rgbColor.g += projectionConfidence.x * 0.25;
        }
        else
        {
            rgbColor.b += 0.5;
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
    output.depth = depth;
    
    return output;
}
