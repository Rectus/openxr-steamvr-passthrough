
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
    float projectionValidity2 : TEXCOORD6;
    float2 cameraDepth : TEXCOORD7;
};


#ifdef VULKAN

SamplerState g_samplerState : register(s5);
Texture2D g_cameraFrameTexture : register(t5);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t6);

#else

SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);

Texture2D<half4> g_prevCameraFilter : register(t2);
RWTexture2D<half4> g_cameraFilter : register(u1);

#endif



float sinc(float x)
{
    return sin(x * 3.1415926535897932384626433) / (x * 3.1415926535897932384626433);
}

float lanczosWeight(float distance, float n)
{
    return (distance == 0) ? 1 : (distance * distance < n * n ? sinc(distance) * sinc(distance / n) : 0);
}

float4 lanczos2_prev(float2 uvs, float2 res)
{
    float2 center = uvs - (((uvs * res) % 1) - 0.5) / res;
    float2 offset = (uvs - center) * res;
    
    float4 output = 0;
    float totalWeight = 0;
    
    for (int y = -2; y < 2; y++)
    {
        for (int x = -2; x < 2; x++)
        {
            float weight = lanczosWeight(x - offset.x, 2) * lanczosWeight(y - offset.y, 2);
            
            output += g_prevCameraFilter.Load(int3(floor(uvs * res) + int2(x, y), 0)) * weight;
            //output += g_prevCameraFilter.SampleLevel(g_samplerState, center + float2(x, y) / res, 0) * weight;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}


// Based on the code in https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 and http://vec3.ca/bicubic-filtering-in-fewer-taps/
float4 catmull_rom_9tap(in Texture2D<half4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}

// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float4 bicubic_b_spline_4tap(in Texture2D<half4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    float2 f = samplePos - texPos1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;
 
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);
    
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
 
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);
 
    float2 t0 = (texPos1 - 1 + f0) / texSize;
    float2 t1 = (texPos1 + 1 + f1) / texSize;

    float4 result = 0;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}




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
    
    float3 minColor = rgbColor;
    float3 maxColor = rgbColor;
    
    float3 crossRGBColor = g_cameraFrameTexture.Sample(g_samplerState, crossUvs).xyz;
    
    float3 crossMinColor = crossRGBColor;
    float3 crossMaxColor = crossRGBColor;
    
    float sharpness = g_sharpness + 0.5;

    rgbColor *= (1 + sharpness * 4);
    crossRGBColor *= (1 + sharpness * 4);
        
    float3 textureSize;
    g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
    
    float3 sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz;
    rgbColor -= sample * sharpness;
    minColor = min(minColor, sample);
    maxColor = max(maxColor, sample);
        
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(-1, 0) / textureSize.xy).xyz;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(1, 0) / textureSize.xy).xyz;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, -1) / textureSize.xy).xyz;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);
    
    sample = g_cameraFrameTexture.Sample(g_samplerState, crossUvs + float2(0, 1) / textureSize.xy).xyz;
    crossRGBColor -= sample * sharpness;
    crossMinColor = min(crossMinColor, sample);
    crossMaxColor = max(crossMaxColor, sample);

    
    //float3 crossRGBColor = min(maxColor, max(crossRGBColor, minColor));
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
    
    float cameraBlend = saturate(0.5 - input.projectionValidity * 0.5 + input.projectionValidity2 * 0.5);
    float cameraSelect = 1 - step(input.projectionValidity2, input.projectionValidity);
    
    float pixelDistanceBlend = distanceFactor + (1 - crossDistanceFactor);
    //float blendfactor = 1 - abs(input.cameraBlend * 2 - 1);
    
    float depthFactor = saturate(1 - (abs(input.cameraDepth.x - input.cameraDepth.y) * 1000));
    
    float combineFactor = g_cutoutCombineFactor * depthFactor * input.projectionValidity * input.projectionValidity2;
    
    // Blend together both cameras based on which ones are valid and have the closest pixels.
    float finalBlendFactor = lerp(cameraSelect, pixelDistanceBlend, combineFactor);
    
    rgbColor = lerp(rgbColor, lerp(crossRGBColor, crossRGBColorClamped, combineFactor), finalBlendFactor);
    minColor = lerp(minColor, lerp(crossMinColor, minColor, combineFactor), finalBlendFactor);
    maxColor = lerp(maxColor, lerp(crossMaxColor, maxColor, combineFactor), finalBlendFactor);
    
    float3 outputTextureSize;
    g_prevCameraFilter.GetDimensions(0, outputTextureSize.x, outputTextureSize.y, outputTextureSize.z);
    
    float2 prevScreenUvs = (input.prevClipSpaceCoords.xy / input.prevClipSpaceCoords.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    prevScreenUvs.y = 1 - prevScreenUvs.y;
    
    float2 newScreenUvs = (input.screenCoords.xy / input.screenCoords.w) * float2(0.5, 0.5) + float2(0.5, 0.5);
    newScreenUvs.y = 1 - newScreenUvs.y;
     
    float2 prevTexCoords = prevScreenUvs * outputTextureSize.xy;
    uint2 prevPixel = floor(prevTexCoords);
    uint2 prevPixel2 = round(prevTexCoords);
    
    float4 filtered;
    
    [branch]
    if (g_temporalFilteringSampling == 0)
    {
        filtered = g_prevCameraFilter.Load(uint3(prevPixel, 0));
    }
    else if (g_temporalFilteringSampling == 1)
    {
        filtered = g_prevCameraFilter.SampleLevel(g_samplerState, prevScreenUvs, 0);
    }
    else if (g_temporalFilteringSampling == 2)
    {
        filtered = bicubic_b_spline_4tap(g_prevCameraFilter, g_samplerState, prevScreenUvs, outputTextureSize.xy);
    }
    else if (g_temporalFilteringSampling == 3)
    {
        filtered = catmull_rom_9tap(g_prevCameraFilter, g_samplerState, prevScreenUvs, outputTextureSize.xy);
    }
    else
    {
        filtered = lanczos2_prev(prevScreenUvs, outputTextureSize.xy);
    }

    // Clamp history color to AABB of neighborhood color values.

    float3 filteredClipped = min(maxColor, max(filtered.xyz, minColor));
    float isClipped = any(filtered.xyz - filteredClipped) ? 1 : 0;
    
    filtered.xyz = isClipped != 0 ? lerp(filtered.xyz, filteredClipped, filtered.a) : filtered.xyz;

    float invAlphaFactor = 0.9;

    
    float prevDistanceFactor = clamp((abs(prevTexCoords.x - prevPixel.x - 0.5) + abs(prevTexCoords.y - prevPixel.y - 0.5)), 0.05, 1);
    
    float vLenSq = dot(input.velocity, input.velocity);
    float factor = saturate(1 - vLenSq * 500);
    float confidence = distanceFactor + (1 - prevDistanceFactor);

    float finalFactor = saturate(factor * confidence);
    rgbColor = lerp(rgbColor, filtered.xyz, finalFactor);
    
    float clipHistory = filtered.a == 0 ? isClipped : lerp(isClipped, filtered.a, finalFactor);
    
    g_cameraFilter[floor(newScreenUvs * outputTextureSize.xy)] = float4(g_bIsFirstRenderOfCameraFrame ? rgbColor : filtered.xyz, input.projectionValidity >= 0 ? clipHistory : 1);

    
    
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
        rgbColor = float3(0, lerp(cameraBlend, pixelDistanceBlend, combineFactor), 0);
    }
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity * alpha : rgbColor;
	
    return float4(rgbColor, g_opacity * alpha);
}
