
#include "common_vs.hlsl"
#include "vs_outputs.hlsl"


cbuffer vsMeshConstantBuffer : register(b2)
{
    float4x4 g_meshToWorldTransform;
};


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;

    float4 worldPos = mul(g_meshToWorldTransform, float4(inPosition, 1.0));
       
    float4 clipSpacePos = mul((g_disparityUVBounds.x < 0.5) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldPos);
    output.cameraReprojectedPos = clipSpacePos;   
    
    output.position = mul(g_worldToHMDProjection, worldPos);
    output.screenPos = output.position;
    output.prevCameraFrameScreenPos = mul(g_prevCameraFrame_WorldToHMDProjection, worldPos);
    output.prevHMDFrameScreenPos = mul(g_prevHMDFrame_WorldToHMDProjection, worldPos);
    output.projectionConfidence = 1.0;
	
    float4 prevOutCoords = mul((g_disparityUVBounds.x < 0.5) ? g_worldToPrevCameraFrameProjectionLeft : g_worldToPrevCameraFrameProjectionRight, worldPos);  

    output.prevCameraFrameVelocity = clipSpacePos.xyz / clipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;

    output.cameraBlendConfidence = 1.0;
    output.crossCameraReprojectedPos = 0;
    output.cameraDepth = 0;

    return output;
}
