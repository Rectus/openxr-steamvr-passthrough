

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

cbuffer vsPassConstantBuffer : register(b1)
{
    float4x4 g_disparityViewToWorld;
	float4x4 g_disparityToDepth;
	uint2 g_disparityTextureSize;
	float g_disparityDownscaleFactor;
};

SamplerState g_samplerState : register(s0);
Texture2D<float> g_disparityTexture : register(t0);


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
	inPosition.xy = inPosition.xy * float2(1, 1) + float2(0, 0);

	float disparity = g_disparityTexture.Load(uint3(inPosition.xy * g_disparityTextureSize, 0));
	//float disparity = g_disparityTexture.SampleLevel(g_samplerState, inPosition.xy, 0);

	output.projectionValidity = 0.0;

	//if (disparity < 0.00001) { disparity = 0.00001;}
	if (disparity > 0.9) 
	{
		disparity = 0.0001; 
		output.projectionValidity = -1.0;
	}


	float2 texturePos = float2(inPosition.x, inPosition.y) * g_disparityTextureSize * g_disparityDownscaleFactor;

	// Convert to uint16 range with 4 bit fixed decimal: 65536 / 16
	disparity *= 4096.0 * g_disparityDownscaleFactor;
	float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, disparity, 1.0));
	viewSpaceCoords.y = 1 - viewSpaceCoords.y;
	viewSpaceCoords.z *= -1;
	viewSpaceCoords /= viewSpaceCoords.w;

    float4 worldSpacePoint = mul(g_disparityViewToWorld, viewSpaceCoords);
	
#ifndef VULKAN
    float4 outCoords = mul(g_worldToCameraProjection, worldSpacePoint);
	output.clipSpaceCoords = outCoords.xyz / outCoords.w;
#endif
	
    output.position = mul(g_worldToHMDProjection, worldSpacePoint);
	output.screenCoords = output.position.xyw;

#ifdef VULKAN
	output.position.y *= -1.0;
#endif

	return output;
}
