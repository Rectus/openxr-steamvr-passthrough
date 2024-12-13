
//#include "common_vs.hlsl"
#include "vs_outputs.hlsl"

VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;

	// Single triangle from (0, 0) to (2, 2).
    float2 clipCoords = float2((vertexID >> 1) * 2, (vertexID & 1) * 2) * 2 - 1;

    output.position = float4(clipCoords, 0, 1);
    
    output.cameraReprojectedPos = float4(clipCoords, 1, 1);
    output.screenPos = float4(clipCoords, 1, 1);
    output.projectionConfidence = 1;

#ifdef VULKAN
	output.position.y *= -1.0;
#endif
    
    return output;
}