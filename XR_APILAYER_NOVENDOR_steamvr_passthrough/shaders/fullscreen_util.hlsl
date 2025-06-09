
#ifndef _FULLSCREEN_UTIL_INCLUDED
#define _FULLSCREEN_UTIL_INCLUDED

#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"



float gaussian(float2 value, float filterWidth)
{
    return exp(-0.5 * dot(value /= (filterWidth * 2.0 * 0.25), value)) / 
        (2.0 * PI * pow(filterWidth * 2.0 * 0.25, 2));
}

// Finds depth map discontinuities using a Sobel filter, and moves each pixel either to the background or foreground
// while generating a smooth contour.
// Optionally uses a gaussian filter to smooth the input depth to a futher distance than bilinear sampling can handle.
float sobel_discontinuity_adjust(in Texture2D<float> depthTex, in SamplerState texSampler, in float depth, in float2 uvs, out bool bWasFiltered)
{
    bWasFiltered = false;
    float outDepth = depth;
    
    uint texW, texH;
    depthTex.GetDimensions(texW, texH);
    float2 invTexSize = 1.0 / float2(texW, texH);
    
    float2 texturePos = saturate(uvs) * float2(texW, texH);
    uint2 pixelPos = floor(texturePos);
    
    float dispU = depthTex.Load(int3(pixelPos + uint2(0, -1), 0));
    float dispD = depthTex.Load(int3(pixelPos + uint2(0, 1), 0));
    float dispL = depthTex.Load(int3(pixelPos + uint2(-1, 0), 0));
    float dispR = depthTex.Load(int3(pixelPos + uint2(1, 0), 0));
            
    float dispUL = depthTex.Load(int3(pixelPos + uint2(-1, -1), 0));
    float dispDL = depthTex.Load(int3(pixelPos + uint2(-1, 1), 0));
    float dispUR = depthTex.Load(int3(pixelPos + uint2(1, -1), 0));
    float dispDR = depthTex.Load(int3(pixelPos + uint2(1, 1), 0));
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    float minDepth = min(depth, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
    float maxDepth = max(depth, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));
    
    float magnitude = length(float2(filterX, filterY));

    if(magnitude > g_depthContourTreshold)
    {
        float totalWeight = 0;
        float smoothedDepth = depth;
        
        [branch]
        if (g_depthContourFilterWidth > 0)
        {
            smoothedDepth = 0;
            
            // Filter with an output pixel-centered gaussian blur to get a smooth contour over the low res depth map pixels.
            for (int x = -g_depthContourFilterWidth; x <= g_depthContourFilterWidth; x++)
            {
                for (int y = -g_depthContourFilterWidth; y <= g_depthContourFilterWidth; y++)
                {
                    float weight = gaussian(float2(x, y), g_depthContourFilterWidth);
                    totalWeight += weight;
                    smoothedDepth += depthTex.SampleLevel(texSampler, uvs + float2(x, y) * invTexSize, 0) * weight;
                }
            }

            smoothedDepth /= totalWeight;
        }
        
        bool inForeground = ((maxDepth - smoothedDepth) > (smoothedDepth - minDepth));

        float offsetFactor = saturate(g_depthContourStrength * 10.0 * magnitude);
        
        bWasFiltered = true;
        outDepth = lerp(depth, inForeground ? minDepth : maxDepth, offsetFactor);
    }
    return outDepth;
}

#endif //_FULLSCREEN_UTIL_INCLUDED
