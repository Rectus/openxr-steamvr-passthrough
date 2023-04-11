
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
Texture2D<float2> g_fisheyeCorrectionTexture : register(t3);

#else

SamplerState g_samplerState : register(s0);
Texture2D g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

#endif


inline float sinc(float x)
{
    return sin(x * PI) / (x * PI);
}

inline float lanczosWeight(float distance, float n)
{
    return (distance == 0) ? 1 : (distance * distance < n * n ? sinc(distance) * sinc(distance / n) : 0);
}

#define LANCZOS_TAPS 3

inline float3 lanczos(float2 uvs, float2 res)
{
    float2 offset = (((uvs * res) % 1) - 0.5);
    
    float3 output = 0;
    float totalWeight = 0;
    
    for (int y = -LANCZOS_TAPS; y <= LANCZOS_TAPS; y++)
    {
        for (int x = -LANCZOS_TAPS; x <= LANCZOS_TAPS; x++)
        {
            float weight = lanczosWeight(x - offset.x, LANCZOS_TAPS) * lanczosWeight(y - offset.y, LANCZOS_TAPS);
            
            output += g_cameraFrameTexture.Load(uint3(floor(uvs * res) + int2(x, y), 0)).rgb * weight;
            //output += weight * g_cameraFrameTexture.Sample(g_samplerState, center + float2(x, y) * res).rgb;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}

float blackmanSincWeight(float distance, float n)
{
    return (distance == 0) ? 1 : (distance * distance < n * n ? sinc(distance) * 
        (0.42 + 0.5 * cos(2 * PI * distance / n) + 0.08 * cos(4 * PI * distance / n)) : 0);
}

float3 blackmanSinc(float2 uvs, float2 res)
{
    float2 offset = (((uvs * res) % 1) - 0.5);
    
    float3 output = 0;
    float totalWeight = 0;
    
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            float weight = blackmanSincWeight(x - offset.x, 2) * blackmanSincWeight(y - offset.y, 2);
            
            output += g_cameraFrameTexture.Load(uint3(floor(uvs * res) + int2(x, y), 0)).rgb * weight;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}

inline float3 nonlinearFiltering(float2 uvs, float2 res)
{
    int2 pixelPos = floor(uvs * res);
    
    float3 original = g_cameraFrameTexture.Load(uint3(pixelPos, 0)).rgb;
    //original = pow(original, 2.2);

    float3 low = original * 4;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(-1.5, 0), 0)).rgb * 2;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(1.5, 0), 0)).rgb * 2;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(0, 1.5), 0)).rgb * 2;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(0, -1.5), 0)).rgb * 2;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(-1.5, -1.5), 0)).rgb;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(-1.5, 1.5), 0)).rgb;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(1.5, -1.5), 0)).rgb;
    low += g_cameraFrameTexture.Load(uint3(pixelPos + int2(1.5, 1.5), 0)).rgb;

    float weight = 4 + 2 * 4 + 4;
    
    low = low / weight;
    //low = pow(low / weight, 2.2);
    
    
    
    //float3 high = pow(original - low, 3) * 20;
    float3 high = original - low;
    high = high * high * high * 20;
    
    float intensity = abs(high.r * 0.2126 + high.g * 0.7152 + high.b * 0.0722);
    
    float toneMapping = 1 - exp(-0.5 * intensity);
    high *= (intensity > 0) ? toneMapping / intensity : 1;
    
    //return pow(original + high, 1 / 2.2);
    return original + high;
}


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

    if (g_bUseFisheyeCorrection)
    {
        float2 correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
    }
	else
    {
        outUvs.y = 1 - outUvs.y;
    }
	
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
    if (g_sharpness != 0.0)
    {
        float2 textureSize;
        g_cameraFrameTexture.GetDimensions(textureSize.x, textureSize.y);
        rgbColor *= 1 + g_sharpness * 4;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
        
        //rgbColor = lanczos(outUvs, textureSize);
        //rgbColor = blackmanSinc(outUvs, textureSize);
        //rgbColor = nonlinearFiltering(outUvs, textureSize);

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
