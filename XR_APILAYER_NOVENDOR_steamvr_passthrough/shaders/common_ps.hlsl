

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
    float4 g_uvBounds;
    float4 g_crossUVBounds;
    float4 g_uvPrepassBounds;
    uint g_arrayIndex;
    bool g_doCutout;
    bool g_bPremultiplyAlpha;
};


cbuffer psPassConstantBuffer : REGISTER_PSPASS
{
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
    uint g_debugOverlay;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bUseFisheyeCorrection;
    bool g_bIsFirstRenderOfCameraFrame;
    bool g_bUseDepthCutoffRange;
    bool g_bClampCameraFrame;
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


