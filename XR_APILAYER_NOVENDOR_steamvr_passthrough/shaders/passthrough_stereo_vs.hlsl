
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float2> g_disparityTexture : register(t0);


float gaussian(float2 value)
{
    return exp(-0.5 * dot(value /= (g_disparityFilterWidth * 2 * 0.25), value)) / 
        (2 * PI * pow(g_disparityFilterWidth * 2 * 0.25, 2));
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


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    float2 disparityUVs = inPosition.xy * (g_disparityUVBounds.zw - g_disparityUVBounds.xy) + g_disparityUVBounds.xy;
    uint3 uvPos = uint3(floor(disparityUVs * g_disparityTextureSize), 0);
    
    float2 dispConf = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs, 0);

	// Disparity at the max projection distance
    float minDisparity = max(g_minDisparity, g_disparityToDepth[2][3] /
        (g_projectionDistance * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]));
    
    float defaultDisparity = g_disparityToDepth[2][3] /
    (min(2.0, g_projectionDistance) * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]); 
    
    uint maxFilterWidth = max(g_disparityFilterWidth, (int)ceil(g_cutoutFilterWidth));
    
    float disparity = clamp(dispConf.x, minDisparity, g_maxDisparity);
    float confidence = dispConf.y;
    
    output.projectionConfidence = confidence;
    output.cameraBlendConfidence = confidence;
    
    float2 disparityOffset = 0.0;
    
    if (dispConf.x > g_maxDisparity || dispConf.x < g_minDisparity)
    {
        disparity = defaultDisparity;
        output.projectionConfidence.x = -10000;
        output.cameraBlendConfidence.x = -10000;
    }
    // Prevent filtering if it would sample across the image edge
    else if (uvPos.x < maxFilterWidth || uvPos.x >= g_disparityTextureSize.x - maxFilterWidth || 
             uvPos.y < maxFilterWidth || uvPos.y >= g_disparityTextureSize.y - maxFilterWidth)
    {
        output.projectionConfidence = 0;
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
    
    float4 prevOutCoords = mul((g_cameraViewIndex == 0) ? g_worldToPrevCameraFrameProjectionLeft : g_worldToPrevCameraFrameProjectionRight, worldSpacePoint);
    
    output.prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldSpacePoint);
    output.prevHMDFrameScreenPos = mul(g_prevHMDFrame_WorldToHMDProjection, worldSpacePoint);
    
    output.prevCameraFrameVelocity = outCoords.xyz / outCoords.w - prevOutCoords.xyz / prevOutCoords.w;
#endif
    
#ifdef VULKAN
	output.position.y *= -1.0;
#endif
    
    output.crossCameraReprojectedPos = 0;
    output.cameraDepth = 0;

	return output;
}
