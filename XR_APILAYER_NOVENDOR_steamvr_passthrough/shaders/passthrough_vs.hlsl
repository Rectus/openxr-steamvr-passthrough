
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
};

#ifdef VULKAN

[[vk::push_constant]]
cbuffer vsConstantBuffer
{
	float4x4 g_cameraProjectionToWorld;
	float4x4 g_hmdWorldToProjection;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
};

#else

cbuffer vsConstantBuffer : register(b0)
{
	float4x4 g_cameraProjectionToWorld;
	float4x4 g_hmdWorldToProjection;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
};
#endif


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	float heightOffset = min(g_floorHeightOffset, g_hmdViewWorldPos.y);
	inPosition.xz *= g_projectionDistance;
	inPosition.xz += g_hmdViewWorldPos.xz;
	inPosition.y *= max(g_projectionDistance * 2.0, g_hmdViewWorldPos.y + g_projectionDistance - heightOffset);
	inPosition.y += min(heightOffset, g_hmdViewWorldPos.y - 0.1);

	float4 clipSpacePos = mul(g_hmdWorldToProjection, float4(inPosition, 1.0));
	output.clipSpaceCoords = clipSpacePos.xyw;

	output.position = mul(g_hmdWorldToProjection, mul(g_cameraProjectionToWorld, clipSpacePos));
	output.screenCoords = output.position.xyw;

#ifdef VULKAN
	output.position.y *= -1.0;
#endif

	return output;
}
