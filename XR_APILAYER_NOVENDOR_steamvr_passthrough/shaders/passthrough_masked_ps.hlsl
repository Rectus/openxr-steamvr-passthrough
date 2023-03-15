
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

	float4 g_uvBounds;
	float2 g_uvPrepassFactor;
	float2 g_uvPrepassOffset;
	uint g_arrayIndex;
};

cbuffer psMaskedConstantBuffer : register(b1)
{
	float3 g_maskedKey;
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
};

SamplerState g_samplerState : register(s2);
Texture2D g_cameraFrameTexture : register(t2);
SamplerState g_blendSamplerState : register(s3);
Texture2D g_blendMask : register(t3);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t5);

#else

cbuffer psViewConstantBuffer : register(b1)
{
	float4 g_uvBounds;
	float2 g_uvPrepassFactor;
	float2 g_uvPrepassOffset;
	uint g_arrayIndex;
};

cbuffer psMaskedConstantBuffer : register(b2)
{
	float3 g_maskedKey;
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
};

SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D g_blendMask : register(t2);

#endif




float4 main(VS_OUTPUT input) : SV_TARGET
{
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

	float4 cameraColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs.xy);

	float2 screenUvs = input.screenCoords.xy / input.screenCoords.z;
	screenUvs = screenUvs * float2(0.5, -0.5) + float2(0.5, 0.5);

	float alpha = g_blendMask.Sample(g_samplerState, screenUvs).x;
	alpha = saturate((1.0 - alpha) * g_opacity);

	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(cameraColor.xyz);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		cameraColor = float4(LABtoLinearRGB_D65(float3(LBis, ab.xy)).xyz, cameraColor.a);
	}
	
    if (g_bDebugDepth)
    {
        float depth = saturate(input.screenCoords.z / (g_depthRange.y - g_depthRange.x) - g_depthRange.x);
        cameraColor = float4(depth, depth, depth, 1);
        if (g_bDebugValidStereo && input.projectionValidity < 0.0)
        {
            cameraColor = float4(0.5, 0, 0, 1);
        }
    }
    else if (g_bDebugValidStereo)
    {
        if (input.projectionValidity < 0.0)
        {
            cameraColor.x += 0.5;
        }
    }

	return float4(cameraColor.xyz, alpha);
}