//#include "common_vs.hlsl"

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float3 clipSpaceCoords : TEXCOORD0;
    float3 screenCoords : TEXCOORD1;
    float projectionValidity : TEXCOORD2;
};

VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;

	// Single triangle from (0, 0) to (2, 2).
    float2 clipCoords = float2((vertexID >> 1) * 2, (vertexID & 1) * 2) * 2 - 1;

    output.position = float4(clipCoords, 0, 1);
    
    output.clipSpaceCoords = float3(clipCoords, 1);
    output.screenCoords = float3(clipCoords, 1);
    output.projectionValidity = 1;

#ifdef VULKAN
	output.position.y *= -1.0;
    output.position.y += 2.0;
    output.clipSpaceCoords.y -= 2.0;
    output.screenCoords.y -= 2.0;
#endif
    
    return output;
}