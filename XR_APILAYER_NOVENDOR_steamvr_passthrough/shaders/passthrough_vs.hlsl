
#include "common_vs.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
    float4 prevClipSpaceCoords : TEXCOORD3;
    float3 velocity : TEXCOORD4;
};



VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	float heightOffset = min(g_floorHeightOffset, g_projectionOriginWorld.y);
    float3 projectionPos = inPosition;
    projectionPos.xz *= g_projectionDistance;
    projectionPos.xz += g_projectionOriginWorld.xz;
    projectionPos.y *= max(g_projectionDistance * 2.0, g_projectionOriginWorld.y + g_projectionDistance - heightOffset);
    projectionPos.y += min(heightOffset, g_projectionOriginWorld.y - 0.1);

    float4 clipSpacePos = mul(g_worldToCameraProjection, float4(projectionPos, 1.0));

    output.clipSpaceCoords = clipSpacePos;
	
    float4 worldPos = mul(g_cameraProjectionToWorld, clipSpacePos);
    output.position = mul(g_worldToHMDProjection, worldPos);
    
    output.screenCoords = output.position.xyw;

    output.prevClipSpaceCoords = mul(g_prevWorldToHMDProjection, worldPos);

	output.projectionValidity = 1.0;
	
#ifndef VULKAN  
    float4 prevOutCoords = mul(g_prevWorldToCameraProjection, float4(projectionPos, 1.0));
    
    output.velocity = clipSpacePos.xyz / clipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;
#endif

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}
