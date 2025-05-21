
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);
Texture2D<float> g_depthMap : register(t3);
Texture2D<float> g_crossDepthMap : register(t4);

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
    float crossDepth = g_crossDepthMap.Sample(g_samplerState, screenUvs);
    float4 cameraValidation = g_cameraValidation.Sample(g_samplerState, screenUvs);
    
    float2 projectionConfidence = cameraValidation.xy;
    float2 cameraBlendValidity = cameraValidation.zw;
    
    float alpha = 1.0;
    
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
    if (cameraBlend < 1.0)
    {
        depth = sobel_discontinuity_adjust(g_depthMap, depth, screenUvs, bIsDiscontinuityFiltered);
    }
    
    [branch]
    if (cameraBlend > 0.0)
    {
        crossDepth = sobel_discontinuity_adjust(g_crossDepthMap, crossDepth, screenUvs, bIsCrossDiscontinuityFiltered);
    }
    
    float4 clipSpacePos = float4(input.screenPos.xy, depth, 1.0);  
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    
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
      
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
    float3 minColor = rgbColor;
    float3 maxColor = rgbColor;
    
    float3 crossRGBColor = g_cameraFrameTexture.Sample(g_samplerState, crossUvs).xyz;
    

    rgbColor *= (1 + g_sharpness * 4);
        
    float3 textureSize;
    g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
    float3 sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz;
    rgbColor -= sample * g_sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz;
    rgbColor -= sample * g_sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz;
    rgbColor -= sample * g_sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz;
    rgbColor -= sample * g_sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    crossRGBColor *= 1 + g_sharpness * 4;
    crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
    crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
    crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
    crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;

    
    float3 crossRGBColorClamped = min(maxColor, max(crossRGBColor, minColor));
    
    uint texW, texH;
    g_cameraFrameTexture.GetDimensions(texW, texH);
    int2 cameraFrameRes = uint2(texW, texH);
    
    float2 camTexCoords = outUvs * cameraFrameRes;
    uint2 camPixel = floor(camTexCoords);
    
    float2 crossCamTexCoords = crossUvs * cameraFrameRes;
    uint2 crossCamPixel = floor(crossCamTexCoords);
    
    // How far the current pixel is to the sampled one
    float distanceFactor = abs(camTexCoords.x - camPixel.x - 0.5) + abs(camTexCoords.y - camPixel.y - 0.5);
    float crossDistanceFactor = abs(crossCamTexCoords.x - crossCamPixel.x - 0.5) + abs(crossCamTexCoords.y - crossCamPixel.y - 0.5);
    
    float pixelDistanceBlend = distanceFactor + (1 - crossDistanceFactor);
    
    float depthFactor = saturate(1 - (abs(depth - crossDepth) * 1000));
    
    float combineFactor = g_cutoutCombineFactor * depthFactor * projectionConfidence.x * projectionConfidence.y;

   
    float finalFactor = lerp(cameraBlend, pixelDistanceBlend, combineFactor);
    
    // Blend together both cameras based on which ones are valid and have the closest pixels.
    rgbColor = lerp(rgbColor, lerp(crossRGBColor, crossRGBColorClamped, combineFactor), finalFactor);

    float finalDepth = lerp(depth, crossDepth, finalFactor);
    
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
        if (projectionConfidence.x < 0.0 && projectionConfidence.y < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else
        {
            if (projectionConfidence.x > 0.0)
            {
                rgbColor.g += projectionConfidence.x * 0.25;
            }
            if (projectionConfidence.y > 0.0)
            {
                rgbColor.b += projectionConfidence.y * 0.25;
            }
        }
    }
    else if (g_debugOverlay == 2) // Camera selection
    {
        rgbColor.g += finalFactor;
    }
    else if (g_debugOverlay == 3)
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
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    output.color = float4(rgbColor, g_opacity * alpha);
    output.depth = finalDepth;
    
    return output;
}
