
#ifndef _COMMON_PS_INCLUDED
#define _COMMON_PS_INCLUDED



#ifdef VULKAN
#define REGISTER_PSVIEW register(b2)
#define REGISTER_PSPASS register(b3)
#define REGISTER_PSMASKED register(b4)
#else
#define REGISTER_PSVIEW register(b1)
#define REGISTER_PSPASS register(b0)
#define REGISTER_PSMASKED register(b2)
#endif


cbuffer psViewConstantBuffer : REGISTER_PSVIEW
{
    float4x4 g_worldToHMDProjection;
    float4x4 g_HMDProjectionToWorld;
    float4x4 g_prevHMDFrame_WorldToHMDProjection;
    float4x4 g_prevCameraFrame_WorldToHMDProjection;
    
    float4 g_uvBounds;
    float4 g_crossUVBounds;
    float4 g_uvPrepassBounds;
    uint g_arrayIndex;
    int g_cameraViewIndex;
    bool g_doCutout;
    bool g_bPremultiplyAlpha;
    bool g_bUseFullscreenQuad;
};


cbuffer psPassConstantBuffer : REGISTER_PSPASS
{
    float4x4 g_worldToCameraFrameProjectionLeft;
	float4x4 g_worldToCameraFrameProjectionRight;
	float4x4 g_worldToPrevCameraFrameProjectionLeft;
	float4x4 g_worldToPrevCameraFrameProjectionRight;
    
    float2 g_depthRange;
    float2 g_depthCutoffRange;
    float g_opacity;
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_sharpness;
    int g_temporalFilteringSampling;
    float g_temporalFilteringFactor;
    float g_temporalFilteringColorRangeCutoff;
    float g_cutoutCombineFactor;
    float g_depthTemporalFilterFactor;
	float g_depthTemporalFilterDistance;
    float g_depthContourStrength;
	float g_depthContourTreshold;
	int g_depthContourFilterWidth;
    uint g_debugOverlay;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bUseFisheyeCorrection;
    bool g_bIsFirstRenderOfCameraFrame;
    bool g_bUseDepthCutoffRange;
    bool g_bClampCameraFrame;
    bool g_bIsCutoutEnabled;
    bool g_bIsAppAlphaInverted;
    bool g_bHasReversedDepth;
};


cbuffer psMaskedConstantBuffer : REGISTER_PSMASKED
{
    float3 g_maskedKey;
    float g_maskedFracChroma;
    float g_maskedFracLuma;
    float g_maskedSmooth;
    bool g_bMaskedUseCamera;
    bool g_bMaskedInvert;
};


#endif //_COMMON_PS_INCLUDED
