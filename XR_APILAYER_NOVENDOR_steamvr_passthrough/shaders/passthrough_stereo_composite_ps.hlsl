
#include "common_ps.hlsl"
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float4 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
    float4 crossClipSpaceCoords : TEXCOORD5;
    float2 projectionValidity2 : TEXCOORD6;
    float cameraBlend : TEXCOORD7;
};


#ifdef VULKAN

SamplerState g_samplerState : register(s5);
Texture2D g_cameraFrameTexture : register(t5);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t6);

#else

SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

#endif


[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    float alpha = 1.0;
	
    if (g_doCutout)
    {
        clip(input.projectionValidity >= 0.5 ? -1 : 1);
        alpha = 1 - saturate(input.projectionValidity * 2);
    }
    
    if (g_bUseDepthCutoffRange)
    {
        float depth = (input.screenCoords.z / input.screenCoords.w);// * (g_depthRange.y - g_depthRange.x) + g_depthRange.x;
        clip(depth - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depth);
    }

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = (input.clipSpaceCoords.xy / input.clipSpaceCoords.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    float2 crossUvs = (input.crossClipSpaceCoords.xy / input.crossClipSpaceCoords.w) * float2(0.5, 0.5) + float2(0.5, 0.5);

    if (g_bClampCameraFrame)
    {
        clip(input.clipSpaceCoords.z);
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
    
    float3 crossRGBColor = g_cameraFrameTexture.Sample(g_samplerState, crossUvs).xyz;
    
    if (g_sharpness != 0.0)
    {
        float3 textureSize;
        g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
        rgbColor *= 1 + g_sharpness * 4;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
        
        crossRGBColor *= 1 + g_sharpness * 4;
        crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        crossRGBColor -= g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
    }
    
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
    //float blendfactor = 1 - abs(input.cameraBlend * 2 - 1);
    
    float combineFactor = abs(input.cameraBlend * 2 - 1) * input.projectionValidity2.x * input.projectionValidity2.y;
    
    // Blend together both cameras based on which ones are valid and have the closest pixels.
    rgbColor = lerp(rgbColor, crossRGBColor, lerp(input.cameraBlend, pixelDistanceBlend, combineFactor));
    
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
        float depth = saturate((input.screenCoords.z / input.screenCoords.w) / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        rgbColor = float3(depth, depth, depth);
        if (g_bDebugValidStereo && input.projectionValidity < 0.0)
        {
            rgbColor = float3(0.5, 0, 0);
        }
    }
    else if (g_bDebugValidStereo)
    {
        //if (input.projectionValidity < 0.0)
        //{
        //    rgbColor.x += 0.5;
        //}
        //else if (input.projectionValidity > 0.0)
        //{
        //    rgbColor.y += input.projectionValidity * 0.25;
        //}
        //else
        //{
        //    rgbColor.z += 0.25;
        //}
        rgbColor = float3(0, lerp(input.cameraBlend, pixelDistanceBlend, combineFactor), 0);
    }
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    return float4(rgbColor, g_opacity * alpha);
}
