
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

SamplerState g_samplerState : register(s7);
Texture2DArray g_texture : register(t7);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t8);

#else

SamplerState g_samplerState : register(s0);
Texture2DArray g_texture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

#endif


[earlydepthstencil]
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
	
    if (!g_bMaskedUseCamera)
    {
        outAlpha *= color.a;
    }
	
    return outAlpha;

}