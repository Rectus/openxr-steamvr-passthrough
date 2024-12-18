
#include "common_vs.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float4 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
    float4 crossClipSpaceCoords : TEXCOORD5;
    float2 projectionValidity2 : TEXCOORD6;
    float cameraBlend : TEXCOORD7;
};

SamplerState g_samplerState : register(s0);
Texture2D<half> g_depthMap : register(t0);
Texture2D<float4> g_cameraInvalidation : register(t1);

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


// Returns a vector toward a depth discontinuity if the sampled pixel is on the deep side.
float2 sobel_discontinuity_direction(in float depth, in float2 uvs)
{
    uint texW, texH;
    g_depthMap.GetDimensions(texW, texH);
    float2 invTexSize = 1 / float2(texW, texH);
    
    float2 fac = 1.5 * invTexSize;
    
    float dispU = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(0, -1) * fac, 0), depth);
    float dispD = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(0, 1) * fac, 0), depth);
    float dispL = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(-1, 0) * fac, 0), depth);
    float dispR = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(1, 0) * fac, 0), depth);
            
    float dispUL = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(-1, -1) * fac, 0), depth);
    float dispDL = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(-1, 1) * fac, 0), depth);
    float dispUR = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(1, -1) * fac, 0), depth);
    float dispDR = min(g_depthMap.SampleLevel(g_samplerState, uvs + float2(1, 1) * fac, 0), depth);
    
    float filterX = dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR; 
    float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
    
    return clamp(float2(filterX, -filterY) * 20, -2, 2) * invTexSize;
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    //float depth = bicubic_b_spline_4tap(g_depthMap, g_samplerState, inPosition.xy);
    float depth = g_depthMap.SampleLevel(g_samplerState, inPosition.xy, 0);
    //uint texW, texH;
    //g_depthMap.GetDimensions(texW, texH);
    //float depth = g_depthMap.Load(int3(inPosition.xy * float2(texW, texH), 0));
    
    //g_cameraInvalidation.GetDimensions(texW, texH);
    //float cameraBlend = g_cameraInvalidation.Load(int3(inPosition.xy * float2(texW, texH), 0));
    float4 cameraValidation = g_cameraInvalidation.SampleLevel(g_samplerState, inPosition.xy, 0);
    //float4 cameraValidation = bicubic_b_spline_4tap(g_cameraInvalidation, g_samplerState, inPosition.xy);
    
    float4 clipSpacePos = float4((inPosition.xy * float2(2.0, -2.0) + float2(-1, 1)), depth, 1.0);   
    
    // Move background vertices underneath foreground vertices to prevent interpolation at discontinuities.
    clipSpacePos.xy += sobel_discontinuity_direction(depth, inPosition.xy);
    
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    
    // Hack to get consistent homogenous coordinates, shouldn't matter for rendering but makes debugging the depth map easier.
    clipSpacePos = mul(g_worldToHMDProjection, worldProjectionPos / worldProjectionPos.w);

    
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    float4 cameraCrossClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionRight : g_worldToCameraFrameProjectionLeft, worldProjectionPos);
    
    output.clipSpaceCoords = cameraClipSpacePos;
    output.crossClipSpaceCoords = cameraCrossClipSpacePos;
    output.prevClipSpaceCoords = mul(g_prevWorldToHMDProjection, worldProjectionPos);	
    output.position = clipSpacePos;   
    output.screenCoords = clipSpacePos; 
    output.screenCoords.z *= output.screenCoords.w; //Linearize depth
	output.projectionValidity = cameraValidation.y;
    output.projectionValidity2 = float2(cameraValidation.yz);
    output.cameraBlend = cameraValidation.x;
	
#ifndef VULKAN  
    float4 prevOutCoords = mul((g_cameraViewIndex == 0) ? g_worldToPrevCameraFrameProjectionLeft : g_worldToPrevCameraFrameProjectionRight, worldProjectionPos);
    
    output.velocity = cameraClipSpacePos.xyz / cameraClipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;
#endif

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}