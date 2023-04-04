
#include "common_ps.hlsl"
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};


#ifdef VULKAN

SamplerState g_samplerState : register(s2);
Texture2D g_cameraFrameTexture : register(t2);
SamplerState g_blendSamplerState : register(s3);
Texture2D g_blendMask : register(t3);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t5);

#else

SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D g_blendMask : register(t2);

#endif


[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
    
    //if (g_doCutout)
    //{
    //    clip(input.projectionValidity);
    //}
    
    float2 screenUvs = input.screenCoords.xy / input.screenCoords.z;
    screenUvs = screenUvs * float2(0.5, -0.5) + float2(0.5, 0.5);
    screenUvs = screenUvs * (g_uvPrepassBounds.zw - g_uvPrepassBounds.xy) + g_uvPrepassBounds.xy;
    screenUvs = clamp(screenUvs, g_uvPrepassBounds.xy, g_uvPrepassBounds.zw);

    float alpha = g_blendMask.Sample(g_samplerState, screenUvs).x;
    alpha = saturate((1.0 - alpha) * g_opacity);
	
    if (g_doCutout)
    {
        clip(alpha <= 0 ? -1 : 1);
    }
    else
    {
        alpha *= saturate(input.projectionValidity);
    }
	
	// Divide to convert back from homogenous coordinates.
	float2 outUvs = input.clipSpaceCoords.xy / input.clipSpaceCoords.z;

	// Convert from clip space coordinates to 0-1.
	outUvs = outUvs * float2(0.5, 0.5) + float2(0.5, 0.5);

	// Remap and clamp to frame UV bounds.
	outUvs = outUvs * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
	outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
		
    if (g_bUseFisheyeCorrection)
    {
        float2 correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
    }
    else
    {
        outUvs.y = 1 - outUvs.y;
    }	

    float3 cameraColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
    if (g_sharpness != 0.0)
    {
        float3 textureSize;
        g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
        cameraColor *= 1 + g_sharpness * 4;
        cameraColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        cameraColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        cameraColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        cameraColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
    }
    
	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(cameraColor.xyz);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		cameraColor = float3(LABtoLinearRGB_D65(float3(LBis, ab.xy)).xyz);
	}
	
    if (g_bDebugDepth)
    {
        float depth = saturate(input.screenCoords.z / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        cameraColor = float3(depth, depth, depth);
        if (g_bDebugValidStereo && input.projectionValidity < 0.0)
        {
            cameraColor = float3(0.5, 0, 0);
        }
    }
    else if (g_bDebugValidStereo)
    {
        if (input.projectionValidity < 0.0)
        {
            cameraColor.x += 0.5;
        }
        else
        {
            cameraColor.y += input.projectionValidity * 0.25;
			
            if (input.projectionValidity > 1.0)
            {
                cameraColor.z += input.projectionValidity * 0.25;
            }
        }
    }

	return float4(cameraColor, alpha);
}