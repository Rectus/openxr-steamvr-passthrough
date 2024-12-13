
#include "common_vs.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float4 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float4 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
};

SamplerState g_samplerState : register(s0);
Texture2D<half> g_depthMap : register(t0);

VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    
    uint texW, texH;
    g_depthMap.GetDimensions(texW, texH);
    int2 depthMapRes = uint2(texW, texH);
    
    float depth = g_depthMap.SampleLevel(g_samplerState, inPosition.xy, 0);
    //float depth = g_depthMap.Load(int3(inPosition.xy * depthMapRes, 0));

    float4 clipSpacePos = float4((inPosition.xy * float2(2.0, -2.0) + float2(-1, 1)), depth, 1.0);
    
    //clipSpacePos *= depth;
    //clipSpacePos *= -(g_depthRange.y - g_depthRange.x) - g_depthRange.x;
    
    float4 clipSpacePos2 = clipSpacePos;
    clipSpacePos2 *= depth;
    
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);

    float4 cameraClipSpacePos = mul(g_worldToCameraProjection, worldProjectionPos);

    output.clipSpaceCoords = cameraClipSpacePos;
    output.prevClipSpaceCoords = mul(g_prevWorldToHMDProjection, worldProjectionPos);	
    output.position = clipSpacePos;   
    output.screenCoords = output.position; 
    //output.screenCoords.z *= output.screenCoords.w;
	output.projectionValidity = depth > 0.99 ? -1.0 : (depth < 0.01 ? -1.0 : 1.0);
	
#ifndef VULKAN  
    float4 prevOutCoords = mul(g_prevWorldToCameraProjection, worldProjectionPos);
    
    output.velocity = cameraClipSpacePos.xyz / cameraClipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;
#endif

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}