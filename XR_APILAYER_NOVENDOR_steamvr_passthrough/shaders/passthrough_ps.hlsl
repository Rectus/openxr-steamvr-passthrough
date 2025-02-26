
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


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
        clip(input.projectionConfidence.x);
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
    }
    else
    {
        outUvs.y = 1 - outUvs.y;
        
        // Remap and clamp to frame UV bounds.
        outUvs = outUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
    }
      
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
    if (g_sharpness != 0.0)
    {
        float3 textureSize;
        g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
        rgbColor *= 1 + g_sharpness * 4;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
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
        float depth = saturate((input.screenPos.z / input.screenPos.w) / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        rgbColor = float3(depth, depth, depth);
        if (g_bDebugValidStereo && input.projectionConfidence.x < 0.0)
        {
            rgbColor = float3(0.5, 0, 0);
        }
    }
    else if (g_bDebugValidStereo)
    {
        if (input.projectionConfidence.x < 0.0)
        {
            rgbColor.x += 0.5;
        }
        else if (input.projectionConfidence.x > 0.0)
        {
            rgbColor.y += input.projectionConfidence.x * 0.25;
        }
        else
        {
            rgbColor.z += 0.25;
        }
    }
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    return float4(rgbColor, g_opacity * alpha);
}
