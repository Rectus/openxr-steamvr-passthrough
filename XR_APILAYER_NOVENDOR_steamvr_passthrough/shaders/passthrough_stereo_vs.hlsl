

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
    float4x4 g_disparityViewToWorldLeft;
    float4x4 g_disparityViewToWorldRight;
	float4x4 g_disparityToDepth;
	uint2 g_disparityTextureSize;
	float g_disparityDownscaleFactor;
    float g_cutoutFactor;
    float g_cutoutOffset;
};

SamplerState g_samplerState : register(s0);
Texture2D<float2> g_disparityTexture : register(t0);


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    float2 disparityUVs = inPosition.xy * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
	
    float2 dispConf = g_disparityTexture.Load(uint3(round(disparityUVs * g_disparityTextureSize), 0));
    float disparity = dispConf.x;
    float confidence = dispConf.y;

    output.projectionValidity = 1;
    //output.projectionValidity = confidence;

	// Disparity at the max projection distance
    float minDisparity = g_disparityToDepth[3][2] /
    (g_projectionDistance * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[2][3]);
	
    if (disparity < minDisparity)
    {
		// Hack that causes some artifacting. Ideally patch any holes or discard and render behind instead.
        //disparity = minDisparity;
        disparity = 0.002;
        output.projectionValidity = -1.0;
    }
    else if (disparity > 0.04)
    {
        disparity = 0.002;
        output.projectionValidity = -1.0;
    }
	else if(confidence < 0.8)
    {
        uint3 uvPos = uint3(round(disparityUVs * g_disparityTextureSize), 0);
        
        float dispL = g_disparityTexture.Load(uvPos + uint3(-1, 0, 0)).x;
        float dispR = g_disparityTexture.Load(uvPos + uint3(1, 0, 0)).x;
        float dispU = g_disparityTexture.Load(uvPos + uint3(0, -1, 0)).x;
        float dispD = g_disparityTexture.Load(uvPos + uint3(0, 1, 0)).x;
        
        float dispUL = g_disparityTexture.Load(uvPos + uint3(-1, -1, 0)).x;
        float dispUR = g_disparityTexture.Load(uvPos + uint3(1, -1, 0)).x;
        float dispDL = g_disparityTexture.Load(uvPos + uint3(-1, 1, 0)).x;
        float dispDR = g_disparityTexture.Load(uvPos + uint3(1, 1, 0)).x;
        
        float maxNeighborDisp = max(dispL, max(dispR, max(dispU, max(dispD, max(dispUL, max(dispUR, max(dispDL, dispDR)))))));
        
        output.projectionValidity = 1 + g_cutoutOffset + 1000 * g_cutoutFactor * (disparity - maxNeighborDisp);
    }

    float2 texturePos = inPosition.xy * g_disparityTextureSize * float2(0.5, 1) * g_disparityDownscaleFactor;

	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
	disparity *= 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, disparity, 1.0));
	viewSpaceCoords.y = 1 - viewSpaceCoords.y;
	viewSpaceCoords.z *= -1;
	viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    float4 worldSpacePoint = 
		mul((g_uvBounds.x < 0.5) ? g_disparityViewToWorldLeft : g_disparityViewToWorldRight, viewSpaceCoords);
    
    
    // Clamp positions to floor height
    if ((worldSpacePoint.y / worldSpacePoint.w) < g_floorHeightOffset)
    {
        float3 ray = normalize(worldSpacePoint.xyz / worldSpacePoint.w - g_hmdViewWorldPos);
        
        float num = (dot(float3(0, 1, 0), float3(0, g_floorHeightOffset, 0)) - dot(float3(0, 1, 0), g_hmdViewWorldPos));
        float denom = dot(float3(0, 1, 0), ray);

        worldSpacePoint = float4(g_hmdViewWorldPos + ray * num / denom, 1);
    }
    
	
#ifndef VULKAN
    float4 outCoords = mul(g_worldToCameraProjection, worldSpacePoint);
	output.clipSpaceCoords = outCoords.xyw;
#endif
	
    output.position = mul(g_worldToHMDProjection, worldSpacePoint);
	output.screenCoords = output.position.xyw;
    
	
#ifdef VULKAN
	output.position.y *= -1.0;
#endif

	return output;
}
