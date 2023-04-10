
#include "common_ps.hlsl"
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float3 prevClipSpaceCoords : TEXCOORD3;
};


#ifdef VULKAN

SamplerState g_samplerState : register(s5);
Texture2D g_cameraFrameTexture : register(t5);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t6);

#else

SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

RWTexture2D<float4> g_cameraFilter;

#endif



[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    float alpha = saturate(input.projectionValidity);
	
    if (g_doCutout)
    {
        clip(input.projectionValidity);
    }

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = (input.clipSpaceCoords.xy / input.clipSpaceCoords.z) * float2(0.5, 0.5) + float2(0.5, 0.5);
	
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
	
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
#ifndef VULKAN
    
    float2 prevUvs = (input.prevClipSpaceCoords.xy / input.prevClipSpaceCoords.z) * float2(0.5, 0.5) + float2(0.5, 0.5);
    prevUvs = prevUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
    prevUvs = clamp(prevUvs, g_uvBounds.xy, g_uvBounds.zw);

    if (g_bUseFisheyeCorrection)
    {
        prevUvs += correction;
    }
    else
    {
        prevUvs.y = 1 - prevUvs.y;
    }
    uint w, h;
    g_cameraFilter.GetDimensions(w, h);
    
    float2 prevTexCoords = prevUvs * float2(w, h);
    uint2 prevPixel = floor(prevTexCoords);
    
    float4 ul = g_cameraFilter.Load(uint3(prevPixel, 0));
    
    float3 filtered = 0;
    
    if(ul.w > 0)
    {
        float3 ur = g_cameraFilter.Load(uint3(prevPixel + uint2(1, 0), 0)).xyz;
        float3 dl = g_cameraFilter.Load(uint3(prevPixel + uint2(0, 1), 0)).xyz;
        float3 dr = g_cameraFilter.Load(uint3(prevPixel + uint2(1, 1), 0)).xyz;
    
        float3 top = lerp(ul.xyz, ur, frac(prevTexCoords.x));
        float3 bottom = lerp(dl, dr, frac(prevTexCoords.x));
        filtered = lerp(top, bottom, frac(prevTexCoords.y));
    }
    
    float factor = lerp(1, g_sharpness + 1, abs(frac(outUvs.x * w) - 0.5) + abs(frac(outUvs.y * h) - 0.5));
    //float factor = 0.95;
    
    rgbColor = rgbColor * factor + filtered.xyz * (1 - factor);
    
    //if (abs(frac(prevTexCoords.x)) < 0.5 && abs(frac(prevTexCoords.y)) < 0.5)
    {
        g_cameraFilter[outUvs * float2(w, h)] = float4(rgbColor, 1);
    }
    
    
#endif
    
    //if (g_sharpness != 0.0)
    //{
    //    float3 textureSize;
    //    g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
    //    rgbColor *= 1 + g_sharpness * 4;
    //    rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
    //    rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
    //    rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
    //    rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
    //}
    
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
        float depth = saturate(input.screenCoords.z / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        rgbColor = float3(depth, depth, depth);
        if (g_bDebugValidStereo && input.projectionValidity < 0.0)
        {
            rgbColor = float3(0.5, 0, 0);
        }
    }
    else if (g_bDebugValidStereo)
    {
        if (input.projectionValidity < 0.0)
        {
            rgbColor.x += 0.5;
        }
		else
        {
            rgbColor.y += input.projectionValidity * 0.25;
			
            if (input.projectionValidity > 1.0)
            {
                rgbColor.z += input.projectionValidity * 0.25;
            }
        }
    }
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    return float4(rgbColor, g_opacity * alpha);
}
