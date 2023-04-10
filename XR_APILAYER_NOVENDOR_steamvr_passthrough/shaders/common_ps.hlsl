

#ifdef VULKAN

[[vk::push_constant]]
cbuffer psViewConstantBuffer
{
// Vertex __XB_GetShaderUserData variables
	float4x4 g_cameraProjectionToWorld;
	//float4x4 g_worldToCameraProjection;
	float4x4 g_worldToHMDProjection;
	float4 g_vsUVBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
	uint g_viewIndex;

// Pixel shader variables
	float4 g_uvBounds;
	float4 g_uvPrepassBounds;
	uint g_arrayIndex;
	bool g_doCutout;
    bool g_bPremultiplyAlpha;
};

#else

cbuffer psViewConstantBuffer : register(b1)
{
    float4 g_uvBounds;
    float4 g_uvPrepassBounds;
    uint g_arrayIndex;
    bool g_doCutout;
    bool g_bPremultiplyAlpha;
};

#endif

cbuffer psPassConstantBuffer : register(b0)
{
    float2 g_depthRange;
    float g_opacity;
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_sharpness;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bDebugValidStereo;
    bool g_bUseFisheyeCorrection;
};

cbuffer psMaskedConstantBuffer : register(b2)
{
    float3 g_maskedKey;
    float g_maskedFracChroma;
    float g_maskedFracLuma;
    float g_maskedSmooth;
    bool g_bMaskedUseCamera;
    bool g_bMaskedInvert;
};