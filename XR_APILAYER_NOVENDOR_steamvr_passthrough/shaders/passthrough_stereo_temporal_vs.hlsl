
#include "common_vs.hlsl"
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float3 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
};

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
    
    float scaledDisp = disparity * (g_viewIndex == 1 ? -1 : 1);
    
	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
    scaledDisp = scaledDisp * 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, scaledDisp, 1.0));
    viewSpaceCoords.y = 1 - viewSpaceCoords.y;
    viewSpaceCoords.z *= -1;
    viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    return mul((g_vsUVBounds.x < 0.5) ? g_disparityViewToWorldLeft : g_disparityViewToWorldRight, viewSpaceCoords);
}


float4 PrevDisparityToWorldCoords(float disparity, float2 clipCoords)
{
    float2 texturePos = clipCoords * g_disparityTextureSize * float2(0.5, 1) * g_disparityDownscaleFactor;
    
    float scaledDisp = disparity * (g_viewIndex == 1 ? -1 : 1);
    
	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
    scaledDisp = scaledDisp * 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, scaledDisp, 1.0));
    viewSpaceCoords.y = 1 - viewSpaceCoords.y;
    viewSpaceCoords.z *= -1;
    viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    return mul((g_vsUVBounds.x < 0.5) ? g_prevDisparityViewToWorldLeft : g_prevDisparityViewToWorldRight, viewSpaceCoords);
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    float2 disparityUVs = inPosition.xy * (g_vsUVBounds.zw - g_vsUVBounds.xy) + g_vsUVBounds.xy;
    uint3 uvPos = uint3(floor(disparityUVs * g_disparityTextureSize), 0);
    
    // Load unfiltered value so that invalid values are not filtered into the texture.
    float2 dispConf = g_disparityTexture.Load(uvPos);

    //float2 dispConf = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs, 0); 
    //float2 dispConf = lanczos2(g_disparityTexture, disparityUVs, g_disparityTextureSize);
    //float2 dispConf = catmull_rom_9tap(g_disparityTexture, g_samplerState, disparityUVs, g_disparityTextureSize);
    
    float disparity;
    float confidence;
    
    
    float4 disparityWorldCoords = DisparityToWorldCoords(dispConf.x, inPosition.xy);
    float4 prevDisparityCoords = mul(g_prevDispWorldToCameraProjection, disparityWorldCoords);
    prevDisparityCoords /= prevDisparityCoords.w;
    prevDisparityCoords.xy = (prevDisparityCoords.xy * 0.5 + 0.5);
    
    //prevDisparityCoords.xy = clamp(prevDisparityCoords.xy, float2(0,0), float2(1,1));
    
    float2 prevDisparityUVs = prevDisparityCoords.xy * (g_vsUVBounds.zw - g_vsUVBounds.xy) + g_vsUVBounds.xy;
    int3 prevUvPos = int3(floor(prevDisparityUVs * g_disparityTextureSize), 0);
    
    //float2 prevDispConf = g_prevDisparityFilter.Load(prevUvPos);
    //float2 prevDispConf = g_prevDisparityFilter.SampleLevel(g_samplerState, prevDisparityUVs, 0);
    //float2 prevDispConf = lanczos2(g_prevDisparityFilter, prevDisparityUVs, g_disparityTextureSize);
    float2 prevDispConf = catmull_rom_9tap(g_prevDisparityFilter, g_samplerState, prevDisparityUVs, g_disparityTextureSize);
        
    //float4 prevDisparityWorldCoords = mul(g_prevCameraProjectionToWorld, DisparityToWorldCoords(prevDispConf.x, inPosition.xy));
    float4 prevDisparityWorldCoords = PrevDisparityToWorldCoords(prevDispConf.x, prevDisparityCoords.xy);
     
    //disparityWorldCoords /= disparityWorldCoords.w;
    prevDisparityWorldCoords /= prevDisparityWorldCoords.w;
    
    bool bUsePrev = true;
    
    //if (prevDispConf.y < 0.5 || (dispConf.y > 0.5 &&
    //    length(disparityWorldCoords.xyz - prevDisparityWorldCoords.xyz) > g_disparityTemporalFilterDistance * 0.1))
    if (prevDispConf.y < 0.1 || prevDisparityCoords.x <= 0 || prevDisparityCoords.x >= 1 || prevDisparityCoords.y <= 0 || prevDisparityCoords.y >= 1)
    {
        disparity = dispConf.x;
        confidence = dispConf.y;
        bUsePrev = false;

    }
    else
    {
        //float depthFactor = saturate(length(disparityWorldCoords.xyz - prevDisparityWorldCoords.xyz));
        float frac = clamp(1 * (prevDispConf.y - dispConf.y), 0, 1);
        disparity = lerp(dispConf.x, prevDispConf.x, frac);
        confidence = lerp(dispConf.y, prevDispConf.y, frac);
        //disparity = prevDispConf.x;
        //confidence = prevDispConf.y;
    }
    
    disparity = dispConf.x;
    confidence = dispConf.y;
    
    
    output.projectionValidity = 1;
    
	// Disparity at the max projection distance
    float minDisparity = max(0, g_disparityToDepth[2][3] /
    (g_projectionDistance * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]));
    
    float maxDisparity = 0.0465;
    
    float defaultDisparity = g_disparityToDepth[2][3] /
    (min(2.0, g_projectionDistance) * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]);

    if(g_viewIndex == 1)
    {
        maxDisparity = -minDisparity;
        minDisparity = -0.0465;
        defaultDisparity *= -1;
    }
    
    
    uint maxFilterWidth = max(g_disparityFilterWidth, (int)ceil(g_cutoutFilterWidth));
    
    if (disparity > maxDisparity || disparity < minDisparity)
    {
        // Hack that causes some artifacting. Ideally patch any holes or discard and render behind instead.
        disparity = defaultDisparity;
        output.projectionValidity = -10000;
    }
    // Prevent filtering if it would sample across the image edge
    else if (uvPos.x < maxFilterWidth || uvPos.x >= g_disparityTextureSize.x - maxFilterWidth || 
             uvPos.y < maxFilterWidth || uvPos.y >= g_disparityTextureSize.y - maxFilterWidth)
    {
        disparity = defaultDisparity;
        output.projectionValidity = -100;
    }
    else if (confidence < 0.5)
    {
        
        // Sample neighboring pixels using clamped Sobel filter, and cut out any areas with discontinuities.
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
            
            float filterX = (g_viewIndex == 0) ? max(0, dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR) :
                min(0, dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR);
            
            float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
            
            float filter = sqrt(pow(filterX, 2) + pow(filterY, 2));

            output.projectionValidity = 1 + g_cutoutOffset - 100 * g_cutoutFactor * filter;
        }
        
        // Filter any uncertain areas with a gaussian blur.
        if (g_disparityFilterWidth > 0)
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
                    //float sampleDisp = g_disparityTexture.Load(uvPos + uint3(x, y, 0)).x;
                    float weight = gaussian(float2(x, y));
                    totalWeight += weight;
                    outDisp += clamp(sampleDisp, minDisparity, maxDisparity) * weight;
                }
            }

            disparity = outDisp / totalWeight;
        }
    }
    
    if (g_bWriteDisparityFilter)
    {
        //if (bUsePrev)
        //{
        //    g_disparityFilter[uvPos.xy] = float2(prevDispConf.x, prevDispConf.y * g_disparityTemporalFilterStrength);
        //}
        //else
        {
            //g_disparityFilter[uvPos.xy] = float2(disparity, confidence * g_disparityTemporalFilterStrength);
        }
        g_disparityFilter[uvPos.xy] = float2(disparity, confidence);
    }
    
    float4 worldSpacePoint = DisparityToWorldCoords(disparity, inPosition.xy);
    worldSpacePoint /= worldSpacePoint.w;
    //if (!g_bIsFirstRender && dispConf.y > 0.5 && prevDispConf.y > 0.5 && output.projectionValidity > 0 && length(worldSpacePoint.xyz - prevDisparityWorldCoords.xyz) < 0.1)
    //{
    //    worldSpacePoint = worldSpacePoint + (worldSpacePoint - prevDisparityWorldCoords) * g_disparityTemporalFilterDistance;

    //}
    
    if (bUsePrev && length(worldSpacePoint.xyz - prevDisparityWorldCoords.xyz) < g_disparityTemporalFilterDistance)
    {
        float factor = saturate(min(g_disparityTemporalFilterStrength, (prevDispConf.y - dispConf.y + 0.5)));
        //worldSpacePoint = prevDisparityWorldCoords;
        worldSpacePoint.xyz = lerp(worldSpacePoint.xyz, prevDisparityWorldCoords.xyz, factor);
    }
    
    // Clamp positions to floor height
    if ((worldSpacePoint.y / worldSpacePoint.w) < g_floorHeightOffset)
    {
        float3 ray = normalize(worldSpacePoint.xyz / worldSpacePoint.w - g_hmdViewWorldPos);
        
        float num = (dot(float3(0, 1, 0), float3(0, g_floorHeightOffset, 0)) - dot(float3(0, 1, 0), g_hmdViewWorldPos));
        float denom = dot(float3(0, 1, 0), ray);

        if (denom < 0)
        {
            worldSpacePoint = float4(g_hmdViewWorldPos + ray * num / denom, 1);
        }
    }
    
    output.position = mul(g_worldToHMDProjection, worldSpacePoint);
    output.screenCoords = output.position.xyw;
	
#ifndef VULKAN
    float4 outCoords = mul(g_worldToCameraProjection, worldSpacePoint);
	output.clipSpaceCoords = outCoords.xyw;
    
    float4 prevOutCoords = mul(g_prevWorldToCameraProjection, worldSpacePoint);
    //output.prevClipSpaceCoords = prevOutCoords.xyw;
    
    float4 prevClipCoords = mul(g_prevWorldToHMDProjection, worldSpacePoint);
    output.prevClipSpaceCoords = prevClipCoords.xyw;
    
    output.velocity = outCoords.xyz / outCoords.w - prevOutCoords.xyz / prevOutCoords.w;
#endif
	
    
    
    
#ifdef VULKAN
	output.position.y *= -1.0;
#endif

	return output;
}
