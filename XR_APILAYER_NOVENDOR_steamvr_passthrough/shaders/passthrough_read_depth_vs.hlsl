
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"

SamplerState g_samplerState : register(s0);
Texture2D<float> g_depthMap : register(t0);
Texture2D<float> g_crossDepthMap : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);


// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float bicubic_b_spline_4tap(in Texture2D<float> tex, in SamplerState linearSampler, in float2 uv)
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

    float result = 0;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}

// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float4 bicubic_b_spline_4tap(in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv)
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

    float4 result = 0;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}


float2 sobel_discontinuity_offset(in Texture2D<float> tex, in float depth, in float2 uvs, in float confidence)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 invTexSize = 1 / float2(texW, texH);
    
    float2 fac = g_depthFoldFilterWidth * 5 * invTexSize;
    
    float dispU = tex.SampleLevel(g_samplerState, uvs + float2(0, -1) * fac, 0);
    float dispD = tex.SampleLevel(g_samplerState, uvs + float2(0, 1) * fac, 0);
    float dispL = tex.SampleLevel(g_samplerState, uvs + float2(-1, 0) * fac, 0);
    float dispR = tex.SampleLevel(g_samplerState, uvs + float2(1, 0) * fac, 0);
            
    float dispUL = tex.SampleLevel(g_samplerState, uvs + float2(-1, -1) * fac, 0);
    float dispDL = tex.SampleLevel(g_samplerState, uvs + float2(-1, 1) * fac, 0);
    float dispUR = tex.SampleLevel(g_samplerState, uvs + float2(1, -1) * fac, 0);
    float dispDR = tex.SampleLevel(g_samplerState, uvs + float2(1, 1) * fac, 0);
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    float minDepth = min(depth, min(dispU, min(dispD, min(dispL, min(dispR, min(dispUL, min(dispDL, min(dispUR, dispDR))))))));
    float maxDepth = max(depth, max(dispU, max(dispD, max(dispL, max(dispR, max(dispUL, max(dispDL, max(dispUR, dispDR))))))));
    
    //return clamp(float2(filterX, filterY) * max(0, minDepth - depth + g_depthFoldMaxDistance) * g_depthFoldStrength * 100, -g_depthFoldMaxDistance * 20, g_depthFoldMaxDistance * 20) * invTexSize  * saturate(0.5 - confidence);
    return float2(filterX, -filterY) * max(-g_depthFoldMaxDistance * 0.1, minDepth - depth - g_depthFoldMaxDistance * 0.1) * g_depthFoldStrength * 50 * invTexSize  * saturate(0.5 - confidence);
}

float2 sobel_discontinuity_contour(in Texture2D<float> tex, inout float depth, in float2 uvs, in float confidence)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 invTexSize = 1.0 / float2(texW, texH);
    
    //float2 fac = g_depthFoldFilterWidth * invTexSize;
    
    //float dispU = tex.SampleLevel(g_samplerState, uvs + float2(0, -1) * fac, 0);
    //float dispD = tex.SampleLevel(g_samplerState, uvs + float2(0, 1) * fac, 0);
    //float dispL = tex.SampleLevel(g_samplerState, uvs + float2(-1, 0) * fac, 0);
    //float dispR = tex.SampleLevel(g_samplerState, uvs + float2(1, 0) * fac, 0);
            
    //float dispUL = tex.SampleLevel(g_samplerState, uvs + float2(-1, -1) * fac, 0);
    //float dispDL = tex.SampleLevel(g_samplerState, uvs + float2(-1, 1) * fac, 0);
    //float dispUR = tex.SampleLevel(g_samplerState, uvs + float2(1, -1) * fac, 0);
    //float dispDR = tex.SampleLevel(g_samplerState, uvs + float2(1, 1) * fac, 0);
    
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
    
    if((maxDepth - minDepth) > g_depthFoldMaxDistance * 0.01)
    {
        float contourFactor = saturate(g_depthFoldStrength * 10.0 * length(float2(filterX, filterY)));
        
        bool inForeground = ((maxDepth - depth) > (depth - minDepth));
        
        depth = lerp(depth, inForeground ? minDepth : depth, contourFactor);

        float2 maxOffset = 1.0 * invTexSize;
        
        float2 offset = clamp((inForeground ? float2(filterX, filterY) : float2(-filterX, -filterY)) * maxOffset * g_depthFoldStrength, -maxOffset, maxOffset);// + float2(filterX, filterY) * (depth - minDepth) / (maxDepth - minDepth) * g_depthFoldFilterWidth * 5.0 * maxOffset;
        return lerp(float2(0, 0), offset, contourFactor);
    }
    return float2(0, 0);
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    float depth;
    float4 cameraValidation;
    
    [branch]
    if (g_bUseBicubicFiltering)
    {
        //depth = bicubic_b_spline_4tap(g_depthMap, g_samplerState, inPosition.xy);
        //cameraValidation = bicubic_b_spline_4tap(g_cameraValidation, g_samplerState, inPosition.xy);
        depth = g_depthMap.SampleLevel(g_samplerState, inPosition.xy, 0);
        cameraValidation = g_cameraValidation.SampleLevel(g_samplerState, inPosition.xy, 0);
    }
    else
    {
        //uint texW, texH;
        //g_depthMap.GetDimensions(texW, texH);
        depth = LoadTextureNearestClamped(g_depthMap, inPosition.xy);
        //depth = g_depthMap.Load(int3(saturate(inPosition.xy * float2(texW, texH)), 0));
       
        //g_cameraValidation.GetDimensions(texW, texH);
        cameraValidation = LoadTextureNearestClamped(g_cameraValidation, inPosition.xy);
        //cameraValidation = g_cameraValidation.Load(int3(saturate(inPosition.xy * float2(texW, texH)), 0));
    }
    
    float2 projectionConfidence = cameraValidation.xy;
    float2 cameraBlendValidity = cameraValidation.zw;
    
    float crossDepth = depth;
    float activeDepth = depth;
    
    float2 vertexOffset = float2(0, 0);
    
    [branch]
    if(g_bBlendDepthMaps)
    {
        [branch]
        if(g_bUseBicubicFiltering)
        {
            //crossDepth = bicubic_b_spline_4tap(g_crossDepthMap, g_samplerState, inPosition.xy);
            crossDepth = g_crossDepthMap.SampleLevel(g_samplerState, inPosition.xy, 0);
        }
        else
        {
            crossDepth = LoadTextureNearestClamped(g_crossDepthMap, inPosition.xy);
            //uint texW, texH;
            //g_crossDepthMap.GetDimensions(texW, texH);
            //crossDepth = g_crossDepthMap.Load(int3(saturate(inPosition.xy * float2(texW, texH)), 0));
        }
        
        bool selectMainCamera = cameraBlendValidity.x >= cameraBlendValidity.y;      
        bool blendCameras = cameraBlendValidity.x > 0.1 && cameraBlendValidity.y > 0.1;
        
        float cameraBlendFactor = blendCameras ? (1 - saturate(cameraBlendValidity.x + 1 - cameraBlendValidity.y)) : cameraBlendValidity.x < cameraBlendValidity.y;
        
             
        cameraBlendValidity = float2(cameraBlendFactor , 1 - cameraBlendFactor);
        
        vertexOffset = lerp(sobel_discontinuity_contour(g_depthMap, depth, inPosition.xy, projectionConfidence.x),
            sobel_discontinuity_contour(g_crossDepthMap, crossDepth, inPosition.xy, projectionConfidence.y), cameraBlendFactor); 
        
        activeDepth = lerp(depth, crossDepth, cameraBlendFactor);  
        
        // TODO
        //offset = selectMainCamera 
        //    ? sobel_discontinuity_offset(g_depthMap, depth, inPosition.xy, projectionConfidence.x) 
        //    : sobel_discontinuity_offset(g_crossDepthMap, crossDepth, inPosition.xy, projectionConfidence.y);
        
        //if ((selectMainCamera ? projectionConfidence.x : projectionConfidence.y) < 1.0)
        //{
        //    // Move depth back to prevent interpolation at discontinuities.
        //    //activeDepth = selectMainCamera
        //    //? sobel_discontinuity_correction(g_depthMap, activeDepth, inPosition.xy, projectionConfidence.x)
        //    //: sobel_discontinuity_correction(g_crossDepthMap, activeDepth, inPosition.xy, projectionConfidence.y);
        //}
    }
    else
    {
        if (projectionConfidence.x < 1.0)
        {
            vertexOffset = sobel_discontinuity_contour(g_depthMap, activeDepth, inPosition.xy, projectionConfidence.x);
            // Move depth back to prevent interpolation at discontinuities.
            //activeDepth = sobel_discontinuity_correction(g_depthMap, depth, inPosition.xy, projectionConfidence.x);
        }
    }
    inPosition.xy += vertexOffset;
    float4 clipSpacePos = float4((inPosition.xy * float2(2.0, -2.0) + float2(-1, 1)), activeDepth, 1.0);   
    
    //clipSpacePos.xy += vertexOffset;
    
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    
    // Hack to get consistent homogenous coordinates, shouldn't matter for rendering but makes debugging the depth map easier.
    clipSpacePos = mul(g_worldToHMDProjection, worldProjectionPos / worldProjectionPos.w);

    
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    float4 cameraCrossClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionRight : g_worldToCameraFrameProjectionLeft, worldProjectionPos);
    
    //clipSpacePos.xy += vertexOffset;
    //cameraClipSpacePos.xy += vertexOffset;
    //cameraCrossClipSpacePos.xy += vertexOffset;
    
    output.position = clipSpacePos;
    output.screenPos = clipSpacePos;
    output.screenPos.z *= output.screenPos.w; //Linearize depth
    
    output.projectionConfidence = projectionConfidence;
    //uint texW, texH;
    //g_depthMap.GetDimensions(texW, texH);
    //output.projectionConfidence.y = length(offset * float2(texW, texH));
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