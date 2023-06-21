
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


cbuffer vsMeshConstantBuffer : register(b2)
{
    float4x4 g_meshToWorldTransform;
};


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;

    float4 worldPos = mul(g_meshToWorldTransform, float4(inPosition, 1.0));
       
    float4 clipSpacePos = mul(g_worldToCameraProjection, worldPos);
    output.clipSpaceCoords = clipSpacePos.xyw;   
    
    output.position = mul(g_worldToHMDProjection, worldPos);
    output.screenCoords = output.position.xyw;
    output.prevClipSpaceCoords = mul(g_prevWorldToHMDProjection, worldPos).xyw;
    output.projectionValidity = 1.0;
	
    
    float4 prevOutCoords = mul(g_prevWorldToCameraProjection, worldPos);  
    float4 prevClipCoords = mul(g_prevWorldToHMDProjection, worldPos);
    
    output.prevClipSpaceCoords = prevClipCoords.xyw;  
    output.velocity = clipSpacePos.xyz / clipSpacePos.w - prevOutCoords.xyz / prevOutCoords.w;

    return output;
}
