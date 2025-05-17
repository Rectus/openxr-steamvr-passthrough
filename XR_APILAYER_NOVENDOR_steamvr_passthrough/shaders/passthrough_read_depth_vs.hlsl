
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"

SamplerState g_samplerState : register(s0);
Texture2D<float> g_depthMap : register(t0);
Texture2D<float> g_crossDepthMap : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);


float2 sobel_discontinuity_contour(in Texture2D<float> tex, inout float depth, in float2 uvs, in float confidence)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 invTexSize = 1.0 / float2(texW, texH);
    
    uint2 pixelPos = saturate(uvs) * float2(texW, texH);
    
    float dispU = tex.Load(int3(pixelPos + uint2(0, -1), 0));
    float dispD = tex.Load(int3(pixelPos + uint2(0, 1), 0));
    float dispL = tex.Load(int3(pixelPos + uint2(-1, 0), 0));
    float dispR = tex.Load(int3(pixelPos + uint2(1, 0), 0));
            
    float dispUL = tex.Load(int3(pixelPos + uint2(-1, -1), 0));
    float dispDL = tex.Load(int3(pixelPos + uint2(-1, 1), 0));
    float dispUR = tex.Load(int3(pixelPos + uint2(1, -1), 0));
    float dispDR = tex.Load(int3(pixelPos + uint2(1, 1), 0));
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    float minDepth = min(depth, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
    float maxDepth = max(depth, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));
    
    if((maxDepth - minDepth) > g_depthContourTreshold * 0.01)
    {
        float contourFactor = saturate(g_depthContourStrength * 10.0 * length(float2(filterX, filterY)));
        
        bool inForeground = ((maxDepth - depth) > (depth - minDepth));
        
        depth = lerp(depth, inForeground ? minDepth : depth, contourFactor);

        float2 maxOffset = 1.0 * invTexSize;
        
        float2 offset = clamp((inForeground ? float2(filterX, filterY) : float2(-filterX, -filterY)) * maxOffset * g_depthContourStrength, -maxOffset, maxOffset);
        
        return lerp(float2(0, 0), offset, contourFactor);
    }
    return float2(0, 0);
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    float depth = LoadTextureNearestClamped(g_depthMap, inPosition.xy);

    float4 cameraValidation = LoadTextureNearestClamped(g_cameraValidation, inPosition.xy);
    
    float2 projectionConfidence = cameraValidation.xy;
    float2 cameraBlendValidity = cameraValidation.zw;
    
    float crossDepth = depth;
    float activeDepth = depth;
    
    float2 vertexOffset = float2(0, 0);
    
    [branch]
    if(g_bBlendDepthMaps)
    {
        crossDepth = LoadTextureNearestClamped(g_crossDepthMap, inPosition.xy);
        
        bool selectMainCamera = cameraBlendValidity.x >= cameraBlendValidity.y;      
        bool blendCameras = cameraBlendValidity.x > 0.1 && cameraBlendValidity.y > 0.1;
        
        float cameraBlendFactor = blendCameras ? (1 - saturate(cameraBlendValidity.x + 1 - cameraBlendValidity.y)) : (selectMainCamera ? 0.0 : 1.0);
                  
        cameraBlendValidity = float2(cameraBlendFactor, 1 - cameraBlendFactor);
        
        [branch]
        if (projectionConfidence.x < 0.9 || projectionConfidence.y < 0.9)
        {
            // Smooth out mesh contours at discontinuities.
            vertexOffset = lerp(sobel_discontinuity_contour(g_depthMap, depth, inPosition.xy, projectionConfidence.x),
                sobel_discontinuity_contour(g_crossDepthMap, crossDepth, inPosition.xy, projectionConfidence.y),
                cameraBlendFactor);
        }
        
        activeDepth = lerp(depth, crossDepth, cameraBlendFactor);  
    }
    else if (projectionConfidence.x < 0.9)
    {
        vertexOffset = sobel_discontinuity_contour(g_depthMap, activeDepth, inPosition.xy, projectionConfidence.x);
    }
    
    inPosition.xy += vertexOffset;
    float4 clipSpacePos = float4((inPosition.xy * float2(2.0, -2.0) + float2(-1, 1)), activeDepth, 1.0);   
    
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    
    // Hack to get consistent homogenous coordinates, shouldn't matter for rendering but makes debugging the depth map easier.
    clipSpacePos = mul(g_worldToHMDProjection, worldProjectionPos / worldProjectionPos.w);

    
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    float4 cameraCrossClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionRight : g_worldToCameraFrameProjectionLeft, worldProjectionPos);
    
    output.position = clipSpacePos;
    output.screenPos = clipSpacePos;
    output.screenPos.z *= output.screenPos.w; //Linearize depth
    
    output.projectionConfidence = projectionConfidence;
    output.cameraBlendConfidence = cameraBlendValidity;
    
    output.cameraReprojectedPos = cameraClipSpacePos;
    output.crossCameraReprojectedPos = cameraCrossClipSpacePos;
    output.prevHMDFrameScreenPos = mul(g_prevHMDFrame_WorldToHMDProjection, worldProjectionPos);	
    output.prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldProjectionPos);	
     
    output.cameraDepth = float2(depth, crossDepth);
	
#ifndef VULKAN  
    float4 prevOutCoords = mul((g_cameraViewIndex == 0) ? g_worldToPrevCameraFrameProjectionLeft : g_worldToPrevCameraFrameProjectionRight, worldProjectionPos);
    
    output.prevCameraFrameVelocity = cameraClipSpacePos.xyz / cameraClipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;
#endif

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}