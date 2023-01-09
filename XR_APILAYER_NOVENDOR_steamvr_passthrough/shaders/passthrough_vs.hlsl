
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
};

cbuffer vsConstantBuffer : register(b0)
{
	float4x4 g_cameraProjectionToWorld;
	float4x4 g_hmdWorldToProjection;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
};

VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	inPosition.xz *= g_projectionDistance;
	inPosition.xz += g_hmdViewWorldPos.xz;
	inPosition.y *= max(g_projectionDistance, g_hmdViewWorldPos.y + g_floorHeightOffset + 0.1);
	inPosition.y += min(g_floorHeightOffset, g_hmdViewWorldPos.y - 0.1);
	
	float4 clipSpacePos = mul(g_hmdWorldToProjection, float4(inPosition, 1.0));
	output.clipSpaceCoords = clipSpacePos.xyw;

	output.position = mul(g_hmdWorldToProjection, mul(g_cameraProjectionToWorld, clipSpacePos));
	output.screenCoords = output.position.xyw;

	return output;
}
