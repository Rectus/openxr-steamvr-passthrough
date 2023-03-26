

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};

cbuffer psPassConstantBuffer : register(b0)
{
    float2 g_depthRange;
    float g_opacity;
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_cutoutFactor;
	float g_cutoutOffset;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bDebugValidStereo;
    bool g_bUseFisheyeCorrection;
};

#ifdef VULKAN

[[vk::push_constant]]
cbuffer psViewConstantBuffer
{
	float4x4 g_cameraProjectionToWorld;
	//float4x4 g_worldToCameraProjection;
	float4x4 g_worldToHMDProjection;
	float4 g_vsUVBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;

	float4 g_uvBounds;
	float4 g_uvPrepassBounds;
	uint g_arrayIndex;
	bool g_doCutout;
};

#else

cbuffer psViewConstantBuffer : register(b1)
{
	float4 g_uvBounds;
    float4 g_uvPrepassBounds;
	uint g_arrayIndex;
	bool g_doCutout;
};

#endif



float4 main(VS_OUTPUT input) : SV_TARGET
{
    if (g_doCutout)
    {
        //float uvDensitySqr = pow(ddx(input.clipSpaceCoords.x / input.clipSpaceCoords.z), 2) +
		//	pow(ddy(input.clipSpaceCoords.y / input.clipSpaceCoords.z), 2);
        //float cutout = step(uvDensitySqr * g_cutoutFactor * 1000000, 1 - g_cutoutOffset);
        //float cutout = step(input.projectionValidity * 100, 1 - g_cutoutOffset);
        clip(input.projectionValidity);
    }
	
	return float4(0, 0, 0, 1.0 - g_opacity);
}