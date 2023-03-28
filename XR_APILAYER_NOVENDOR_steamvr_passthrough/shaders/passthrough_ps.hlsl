
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};

cbuffer psPassConstantBuffer : register(b0)
{
	float2 g_depthRange;
	float g_opacity;
	float g_brightness;
	float g_contrast;
	float g_saturation;
    float g_cutoutFactor;
    float g_cutoutOffset;
	bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bDebugValidStereo;
    bool g_bUseFisheyeCorrection;
};

#ifdef VULKAN

[[vk::push_constant]]
cbuffer psViewConstantBuffer
{
	float4x4 g_cameraProjectionToWorld;
	//float4x4 g_worldToCameraProjection;
	float4x4 g_worldToHMDProjection;
	float4 g_vsUVBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
	uint g_viewIndex;

	float4 g_uvBounds;
	float4 g_uvPrepassBounds;
	uint g_arrayIndex;
	bool g_doCutout;
};

SamplerState g_samplerState : register(s2);
Texture2D g_cameraFrameTexture : register(t2);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t3);

#else

cbuffer psViewConstantBuffer : register(b1)
{
	float4 g_uvBounds;
	float4 g_uvPrepassBounds;
	uint g_arrayIndex;
    bool g_doCutout;
};

SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

#endif



[earlydepthstencil]
float4 main(VS_OUTPUT input) : SV_TARGET
{
	float alpha = 1;
	
    if (g_doCutout)
    {
        clip(input.projectionValidity);
    }

    alpha = saturate(input.projectionValidity);
	
	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = (input.clipSpaceCoords.xy / input.clipSpaceCoords.z) * float2(0.5, 0.5) + float2(0.5, 0.5);
	
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
	
    float4 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs);

	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(rgbColor.xyz);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		rgbColor.xyz = LABtoLinearRGB_D65(float3(LBis, ab.xy)).xyz;
	}

    if (g_bDebugDepth)
    {
        float depth = saturate(input.screenCoords.z / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        rgbColor = float4(depth, depth, depth, 1);
        if (g_bDebugValidStereo && input.projectionValidity < 0.0)
        {
            rgbColor = float4(0.5, 0, 0, 1);
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
            rgbColor.y += input.projectionValidity;
        }
    }
	
    return float4(rgbColor.xyz, g_opacity * alpha);
}
