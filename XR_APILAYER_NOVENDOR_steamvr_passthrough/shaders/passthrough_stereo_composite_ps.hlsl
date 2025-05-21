
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);



[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    float alpha = 1.0;
	
    if (g_doCutout)
    {
        clip(input.cameraBlendConfidence.x);
        alpha = saturate(input.projectionConfidence.x);
    }
    
    if (g_bUseDepthCutoffRange)
    {
        float depth = (input.screenPos.z / input.screenPos.w);// * (g_depthRange.y - g_depthRange.x) + g_depthRange.x;
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = (input.cameraReprojectedPos.xy / input.cameraReprojectedPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    float2 crossUvs = (input.crossCameraReprojectedPos.xy / input.crossCameraReprojectedPos.w) * float2(0.5, 0.5) + float2(0.5, 0.5);

    if (g_bClampCameraFrame)
    {
        clip(input.cameraReprojectedPos.z);
        clip(outUvs);
        clip(1 - outUvs);
    }
    
    float2 correction = 0;
    
    if (g_bUseFisheyeCorrection)
    {
        // Remap and clamp to frame UV bounds.
        outUvs = outUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
        
        crossUvs = crossUvs * (g_crossUVBounds.zw - g_crossUVBounds.xy) + g_crossUVBounds.xy;
        crossUvs = clamp(crossUvs, g_crossUVBounds.xy, g_crossUVBounds.zw);
        
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, crossUvs);
        crossUvs += correction;
    }
    else
    {
        outUvs.y = 1 - outUvs.y;
        
        // Remap and clamp to frame UV bounds.
        outUvs = outUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        
        crossUvs = crossUvs * (g_crossUVBounds.zw - g_crossUVBounds.xy) + g_crossUVBounds.xy;
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
    
    float depthFactor =  saturate(1 - (abs(input.cameraDepth.x - input.cameraDepth.y) * 1000));
    
    float combineFactor = g_cutoutCombineFactor * depthFactor * input.projectionConfidence.x * input.projectionConfidence.y;

   
    float finalFactor = lerp(input.cameraBlendConfidence.x, pixelDistanceBlend, combineFactor);
    
    // Blend together both cameras based on which ones are valid and have the closest pixels.
    rgbColor = lerp(rgbColor, lerp(crossRGBColor, crossRGBColorClamped, combineFactor), finalFactor);
    
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
        float depth = saturate((input.screenPos.z / input.screenPos.w) / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        rgbColor = float3(depth, depth, depth);
    }
    if (g_debugOverlay == 1) // Confidence
    {
        if (input.projectionConfidence.x < 0.0 && input.projectionConfidence.y < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else
        {
            if (input.projectionConfidence.x > 0.0)
            {
                rgbColor.g += input.projectionConfidence.x * 0.25;
            }
            if (input.projectionConfidence.y > 0.0)
            {
                rgbColor.b += input.projectionConfidence.y * 0.25;
            }
        }
    }
    else if (g_debugOverlay == 2) // Camera selection
    {
        rgbColor.g += finalFactor;
    }

    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    return float4(rgbColor, g_opacity * alpha);
}
