
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float2> g_disparityTexture : register(t0);

Texture2D<float2> g_prevDisparityFilter : register(t1);
RWTexture2D<float2> g_disparityFilter : register(u2);

float gaussian(float2 value)
{
    return exp(-0.5 * dot(value /= (g_disparityFilterWidth * 2 * 0.25), value)) / 
        (2 * PI * pow(g_disparityFilterWidth * 2 * 0.25, 2));
}

float sinc(float x)
{
    return sin(x * 3.1415926535897932384626433) / (x * 3.1415926535897932384626433);
}

float lanczosWeight(float distance, float n)
{
    return (distance == 0) ? 1 : (distance * distance < n * n ? sinc(distance) * sinc(distance / n) : 0);
}

float2 lanczos2(in Texture2D<float2> tex, float2 uvs, float2 res)
{
    float2 center = uvs - (((uvs * res) % 1) - 0.5) / res;
    float2 offset = (uvs - center) * res;
    
    float2 output = 0;
    float totalWeight = 0;
    
    for (int y = -2; y < 2; y++)
    {
        for (int x = -2; x < 2; x++)
        {
            float weight = lanczosWeight(x - offset.x, 2) * lanczosWeight(y - offset.y, 2);
            
            output += tex.Load(int3(floor(uvs * res) + int2(x, y), 0)) * weight;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}

// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float2 bicubic_b_spline_4tap(in Texture2D<float2> tex, in SamplerState linearSampler, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 texSize = float2(texW, texH);
    
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

    float2 result = 0;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}


// Based on the code in https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 and http://vec3.ca/bicubic-filtering-in-fewer-taps/
float2 catmull_rom_9tap(in Texture2D<float2> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
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

    float2 result = 0;
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


float4 DisparityToWorldCoords(float disparity, float2 clipCoords)
{
    float2 texturePos = clipCoords * g_disparityTextureSize * float2(0.5, 1) * g_disparityDownscaleFactor;
    
	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
    float scaledDisp = disparity * 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, scaledDisp, 1.0));
    viewSpaceCoords.y = 1 - viewSpaceCoords.y;
    viewSpaceCoords.z *= -1;
    viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    return mul((g_disparityUVBounds.x < 0.5) ? g_depthFrameViewToWorldLeft : g_depthFrameViewToWorldRight, viewSpaceCoords);
}


float4 PrevDisparityToWorldCoords(float disparity, float2 clipCoords)
{
    float2 texturePos = clipCoords * g_disparityTextureSize * float2(0.5, 1) * g_disparityDownscaleFactor;
    
	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
    float scaledDisp = disparity * 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, scaledDisp, 1.0));
    viewSpaceCoords.y = 1 - viewSpaceCoords.y;
    viewSpaceCoords.z *= -1;
    viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    return mul((g_disparityUVBounds.x < 0.5) ? g_prevDepthFrameViewToWorldLeft : g_prevDepthFrameViewToWorldRight, viewSpaceCoords);
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    float2 disparityUVs = inPosition.xy * (g_disparityUVBounds.zw - g_disparityUVBounds.xy) + g_disparityUVBounds.xy;
    uint3 uvPos = uint3(floor(disparityUVs * g_disparityTextureSize), 0);
    
    //float2 dispConf = g_disparityTexture.Load(uvPos);

    float2 dispConf = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs, 0); 
    //float2 dispConf = lanczos2(g_disparityTexture, disparityUVs, g_disparityTextureSize);
    //float2 dispConf = catmull_rom_9tap(g_disparityTexture, g_samplerState, disparityUVs, g_disparityTextureSize);
      
    
    float4 disparityWorldCoords = DisparityToWorldCoords(dispConf.x, inPosition.xy);
    float4 prevDisparityCoords = mul((g_disparityUVBounds.x < 0.5) ? g_worldToPrevDepthFrameProjectionLeft : g_worldToPrevDepthFrameProjectionRight, disparityWorldCoords);
    prevDisparityCoords /= prevDisparityCoords.w;
    prevDisparityCoords.xy = (prevDisparityCoords.xy * 0.5 + 0.5);
    
    float2 prevDisparityUVs = prevDisparityCoords.xy * (g_disparityUVBounds.zw - g_disparityUVBounds.xy) + g_disparityUVBounds.xy;
    int3 prevUvPos = int3(floor(prevDisparityUVs * g_disparityTextureSize), 0);
    
    //float2 prevDispConf = g_prevDisparityFilter.Load(prevUvPos);
    float2 prevDispConf = g_prevDisparityFilter.SampleLevel(g_samplerState, prevDisparityUVs, 0);
    //float2 prevDispConf = bicubic_b_spline_4tap(g_prevDisparityFilter, g_samplerState, prevDisparityUVs);
    //float2 prevDispConf = lanczos2(g_prevDisparityFilter, prevDisparityUVs, g_disparityTextureSize);
    //float2 prevDispConf = catmull_rom_9tap(g_prevDisparityFilter, g_samplerState, prevDisparityUVs, g_disparityTextureSize);
        
    float4 prevDisparityWorldCoords = PrevDisparityToWorldCoords(prevDispConf.x, prevDisparityCoords.xy);
     
    prevDisparityWorldCoords /= prevDisparityWorldCoords.w;
    
    bool bUsePrev = true;
    
    if (prevDispConf.y < 0.0 || prevDisparityCoords.x <= 0 || prevDisparityCoords.x >= 1 || prevDisparityCoords.y <= 0 || prevDisparityCoords.y >= 1)
    {
        bUsePrev = false;
    }
    
    // Disparity at the max projection distance
    float minDisparity = max(g_minDisparity, g_disparityToDepth[2][3] /
    (g_projectionDistance * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]));
    
    float disparity = clamp(dispConf.x, minDisparity, g_maxDisparity);
    float confidence = dispConf.y;
    
    
    output.projectionConfidence = confidence;
    output.cameraBlendConfidence = confidence;

    float2 disparityOffset = 0.0;
    
    float defaultDisparity = g_disparityToDepth[2][3] /
    (min(2.0, g_projectionDistance) * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]);  
    
    uint maxFilterWidth = max(g_disparityFilterWidth, (int)ceil(g_cutoutFilterWidth));
    
    if (dispConf.x > g_maxDisparity || dispConf.x < g_minDisparity)
    {
        disparity = defaultDisparity;
        output.projectionConfidence = -10000;
        output.cameraBlendConfidence = -10000;
    }
    // Prevent filtering if it would sample across the image edge
    else if (uvPos.x < maxFilterWidth || uvPos.x >= g_disparityTextureSize.x - maxFilterWidth || 
             uvPos.y < maxFilterWidth || uvPos.y >= g_disparityTextureSize.y - maxFilterWidth)
    {
        disparity = defaultDisparity;
        output.projectionConfidence = -100;
    }
    else
    {
        // Sample neighboring pixels using a modified clamped Sobel filter, and mask out any areas with discontinuities.
        [branch]
        if (g_bFindDiscontinuities)
        {                      
            float2 fac = g_cutoutFilterWidth / g_disparityTextureSize;
            
            float dispU = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(0, -1) * fac, 0).x;
            float dispD = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(0, 1) * fac, 0).x;
            float dispL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, 0) * fac, 0).x;
            float dispR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, 0) * fac, 0).x;
            
            float dispUL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, -1) * fac, 0).x;
            float dispDL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, 1) * fac, 0).x;
            float dispUR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, -1) * fac, 0).x;
            float dispDR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, 1) * fac, 0).x;
            
            // Clamp the max disparity tested to the sampled pixel disparity in order to not filter foreground pixels.
            float filterX = min(disparity, dispUL) + min(disparity, dispL) * 2 + min(disparity, dispDL) - min(disparity, dispUR) - min(disparity, dispR) * 2 - min(disparity, dispDR);
            
            float filterY = min(disparity, dispUL) + min(disparity, dispU) * 2 + min(disparity, dispUR) - min(disparity, dispDL) - min(disparity, dispD) * 2 - min(disparity, dispDR);
            
            float filter = sqrt(pow(filterX, 2) + pow(filterY, 2));
            
            // Filter only the occluded side for camera selection. Assumes left and right cameras.
            float filterCamX = (g_disparityUVBounds.x < 0.5) ? 
                max(0, min(disparity, dispUL) + min(disparity, dispL) * 2 + min(disparity, dispDL) - dispUR - dispR * 2 - dispDR) :
                min(0, dispUL + dispL * 2 + dispDL - min(disparity, dispUR) - min(disparity, dispR) * 2 - min(disparity, dispDR));
            
            float filterCam = sqrt(pow(filterCamX, 2) + pow(filterY, 2));

            // Output optimistic values for camera composition to only filter occlusions
            output.cameraBlendConfidence = (1 + g_cutoutOffset + confidence - 100.0 * g_cutoutFactor * filterCam) * g_cameraBlendWeight;
            
            // Output pessimistic values for depth temporal filter to force invalidation on movement
            output.projectionConfidence = min(confidence, 1 + g_cutoutOffset - 100.0 * g_cutoutFactor * filter);
            
            float dfilterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR;
            float dfilterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
            float minDisp = min(disparity, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
            float maxDisp = max(disparity, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));


            if ((maxDisp - minDisp) > g_depthContourTreshold * (g_maxDisparity - minDisparity) * 0.01)
            {
                float contourFactor = saturate(g_depthContourStrength * 4 * length(float2(dfilterX, dfilterY)));
                
                bool inForeground = ((disparity - minDisp) > (maxDisp - disparity));

                float2 maxOffset = 1.0 / g_disparityTextureSize;
                
                float2 offset = clamp((inForeground ? float2(-dfilterX, dfilterY) : float2(dfilterX, -dfilterY)) * maxOffset * g_depthContourStrength * 2, -maxOffset, maxOffset);

                disparityOffset = lerp(float2(0, 0), offset, contourFactor);
            }
        }
        
        // Filter any uncertain areas with a gaussian blur.
        [branch]
        if (g_disparityFilterWidth > 0 && confidence < 0.5)
        {
            float totalWeight = 0;
            float outDisp = 0;
            float disparityDelta = 0;
        
            for (int x = -g_disparityFilterWidth; x <= g_disparityFilterWidth; x++)
            {
                for (int y = -g_disparityFilterWidth; y <= g_disparityFilterWidth; y++)
                {
                    float2 offset = float2(x, y) / (float) g_disparityTextureSize;
                    float sampleDisp = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + offset, 0).x;
                    float weight = gaussian(float2(x, y));
                    totalWeight += weight;
                    outDisp += clamp(sampleDisp, minDisparity, g_maxDisparity) * weight;
                }
            }

            disparity = outDisp / totalWeight;
        }
    }   
    
    
    float4 worldSpacePoint = DisparityToWorldCoords(disparity, inPosition.xy);
    worldSpacePoint /= worldSpacePoint.w;
    
    uint2 writeUVPos = floor(disparityUVs * g_disparityTextureSize);
    
    if (bUsePrev && length(worldSpacePoint.xyz - prevDisparityWorldCoords.xyz) < g_disparityTemporalFilterDistance)
    {
        float factor = saturate(min(g_disparityTemporalFilterStrength, smoothstep(0.4, 0.6, prevDispConf.y - dispConf.y)));
        worldSpacePoint.xyz = lerp(worldSpacePoint.xyz, prevDisparityWorldCoords.xyz, factor);
        
        if (g_bWriteDisparityFilter)
        {
            g_disparityFilter[writeUVPos] = lerp(float2(disparity, confidence), prevDispConf, factor);
        }
    }
    else if (g_bWriteDisparityFilter)
    {
        g_disparityFilter[writeUVPos] = float2(disparity, confidence);
    }
    
    // Clamp positions to floor height
    if ((worldSpacePoint.y / worldSpacePoint.w) < g_floorHeightOffset)
    {
        float3 ray = normalize(worldSpacePoint.xyz / worldSpacePoint.w - g_projectionOriginWorld);
        
        float num = (dot(float3(0, 1, 0), float3(0, g_floorHeightOffset, 0)) - dot(float3(0, 1, 0), g_projectionOriginWorld));
        float denom = dot(float3(0, 1, 0), ray);

        if (denom < 0)
        {
            worldSpacePoint = float4(g_projectionOriginWorld + ray * num / denom, 1);
        }
    }
    
    output.position = mul(g_worldToHMDProjection, worldSpacePoint);
    output.position.xy += disparityOffset;
    
    output.screenPos = output.position;
    output.screenPos.z *= output.screenPos.w; //Linearize depth
	
#ifndef VULKAN
    float4 outCoords = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldSpacePoint);
	output.cameraReprojectedPos = outCoords;
    
    output.prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldSpacePoint);
    output.prevHMDFrameScreenPos = mul(g_prevHMDFrame_WorldToHMDProjection, worldSpacePoint);
    
    float4 prevOutCoords = mul((g_cameraViewIndex == 0) ? g_worldToPrevCameraFrameProjectionLeft : g_worldToPrevCameraFrameProjectionRight, worldSpacePoint);
    
    output.prevCameraFrameVelocity = outCoords.xyz / outCoords.w - prevOutCoords.xyz / prevOutCoords.w;
#endif
    
    
#ifdef VULKAN
	output.position.y *= -1.0;
#endif
    
    output.crossCameraReprojectedPos = 0;
    output.cameraDepth = 0;

	return output;
}
