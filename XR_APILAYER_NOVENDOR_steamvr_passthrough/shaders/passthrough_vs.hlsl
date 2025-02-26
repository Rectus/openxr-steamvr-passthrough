
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	float heightOffset = min(g_floorHeightOffset, g_projectionOriginWorld.y);
    float4 worldProjectionPos = float4(inPosition, 1.0);
    worldProjectionPos.xz *= g_projectionDistance;
    worldProjectionPos.xz += g_projectionOriginWorld.xz;
    worldProjectionPos.y *= max(g_projectionDistance * 2.0, g_projectionOriginWorld.y + g_projectionDistance - heightOffset);
    worldProjectionPos.y += min(heightOffset, g_projectionOriginWorld.y - 0.1);
    
    float4 cameraClipSpacePos = mul((g_disparityUVBounds.x < 0.5) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);

    output.cameraReprojectedPos = cameraClipSpacePos;
    output.prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldProjectionPos);
    output.prevHMDFrameScreenPos = mul(g_prevHMDFrame_WorldToHMDProjection, worldProjectionPos);	
    output.position = mul(g_worldToHMDProjection, worldProjectionPos);   
    output.screenPos = output.position; 
	output.projectionConfidence = 1.0;
	
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
