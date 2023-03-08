

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};

#ifdef VULKAN

[[vk::push_constant]]
cbuffer vsConstantBuffer
{
	float4x4 g_cameraProjectionToWorld;
	//float4x4 g_worldToCameraProjection;
	float4x4 g_worldToHMDProjection;
	float4 g_uvBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
};

#else

cbuffer vsConstantBuffer : register(b0)
{
    float4x4 g_cameraProjectionToWorld;
    float4x4 g_worldToCameraProjection;
    float4x4 g_worldToHMDProjection;
	float4 g_uvBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
};
#endif

//cbuffer vsPassConstantBuffer : register(b1)
//{
//	float4x4 g_depthProjectionToWorld;
//	float4x4 g_disparityToDepth;
//	uint2 g_disparityTextureSize;
//	float g_disparityDownscaleFactor;
//};
//
//SamplerState g_samplerState : register(s0);
//Texture2D<float> g_DepthTexture : register(t0);


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	float heightOffset = min(g_floorHeightOffset, g_hmdViewWorldPos.y);
	inPosition.xz *= g_projectionDistance;
	inPosition.xz += g_hmdViewWorldPos.xz;
	inPosition.y *= max(g_projectionDistance * 2.0, g_hmdViewWorldPos.y + g_projectionDistance - heightOffset);
	inPosition.y += min(heightOffset, g_hmdViewWorldPos.y - 0.1);

#ifdef VULKAN //TODO add to vulkan
    float4 clipSpacePos = mul(g_worldToHMDProjection, float4(inPosition, 1.0));
#else
    float4 clipSpacePos = mul(g_worldToCameraProjection, float4(inPosition, 1.0));
#endif
    output.clipSpaceCoords = clipSpacePos.xyw;
	
    float4 worldPos = mul(g_cameraProjectionToWorld, clipSpacePos);
    output.position = mul(g_worldToHMDProjection, worldPos);
    
    output.screenCoords = output.position.xyw;

	output.projectionValidity = 1.0;

#ifdef VULKAN
	output.position.z *= 0.1; // Vulkan is currently fucky with depth.
	output.position.y *= -1.0;
#endif

	return output;
}
