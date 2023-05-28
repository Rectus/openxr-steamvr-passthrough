
#include "common_vs.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float3 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
};



VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	float heightOffset = min(g_floorHeightOffset, g_hmdViewWorldPos.y);
	inPosition.xz *= g_projectionDistance;
	inPosition.xz += g_hmdViewWorldPos.xz;
	inPosition.y *= max(g_projectionDistance * 2.0, g_hmdViewWorldPos.y + g_projectionDistance - heightOffset);
	inPosition.y += min(heightOffset, g_hmdViewWorldPos.y - 0.1);

    float4 clipSpacePos = mul(g_worldToCameraProjection, float4(inPosition, 1.0));

    output.clipSpaceCoords = clipSpacePos.xyw;
	
    float4 worldPos = mul(g_cameraProjectionToWorld, clipSpacePos);
    output.position = mul(g_worldToHMDProjection, worldPos);
    
    output.screenCoords = output.position.xyw;

    output.prevClipSpaceCoords = mul(g_prevWorldToHMDProjection, worldPos).xyw;

	output.projectionValidity = 1.0;
	
#ifndef VULKAN  
    float4 prevOutCoords = mul(g_prevWorldToCameraProjection, worldPos);
    
    float4 prevClipCoords = mul(g_prevWorldToHMDProjection, worldPos);
    output.prevClipSpaceCoords = prevClipCoords.xyw;
    
    output.velocity = clipSpacePos.xyz / clipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;
#endif

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}
