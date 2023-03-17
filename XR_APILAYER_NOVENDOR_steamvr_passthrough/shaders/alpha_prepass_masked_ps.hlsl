
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
	float4 g_uvPrepassBounds;
	uint g_arrayIndex;
};

cbuffer psMaskedConstantBuffer : register(b1)
{
	float3 g_maskedKey;
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
	bool g_bMaskedInvert;
};

SamplerState g_samplerState : register(s4);
Texture2DArray g_texture : register(t4);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t5);

#else

cbuffer psViewConstantBuffer : register(b1)
{
	float4 g_uvBounds;
    float4 g_uvPrepassBounds;
	uint g_arrayIndex;
};

cbuffer psMaskedConstantBuffer : register(b2)
{
	float3 g_maskedKey;
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
	bool g_bMaskedInvert;
};

SamplerState g_samplerState : register(s0);
Texture2DArray g_texture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

#endif



float main(VS_OUTPUT input) : SV_TARGET
{
	float4 color;
	
    bool bInvertOutput = g_bMaskedInvert;

	if (g_bMaskedUseCamera)
	{
		float2 outUvs = input.clipSpaceCoords.xy / input.clipSpaceCoords.z;
		outUvs = outUvs * float2(0.5, 0.5) + float2(0.5, 0.5);
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
		
		color = g_texture.Sample(g_samplerState, float3(outUvs.xy, 0));
        bInvertOutput = !bInvertOutput;
    }
	else
	{
		float2 outUvs = input.screenCoords.xy / input.screenCoords.z;
		outUvs = outUvs * float2(0.5, -0.5) + float2(0.5, 0.5);

        color = g_texture.Sample(g_samplerState, 
			float3((outUvs * (g_uvPrepassBounds.zw - g_uvPrepassBounds.xy) + g_uvPrepassBounds.xy), float(g_arrayIndex)));
    }

	float3 difference = LinearRGBtoLAB_D65(color.xyz) - LinearRGBtoLAB_D65(g_maskedKey);

	float2 distChromaSqr = pow(difference.yz, 2);
	float fracChromaSqr = pow(g_maskedFracChroma, 2);

	float distChroma = smoothstep(fracChromaSqr, fracChromaSqr + pow(g_maskedSmooth, 2), (distChromaSqr.x + distChromaSqr.y));
	float distLuma = smoothstep(g_maskedFracLuma, g_maskedFracLuma + g_maskedSmooth, abs(difference.x));
	
    float outAlpha = bInvertOutput ? 1.0 - max(distChroma, distLuma) : max(distChroma, distLuma);
	
    return outAlpha;

}