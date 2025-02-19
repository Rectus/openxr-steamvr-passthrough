

#ifdef VULKAN

cbuffer psViewConstantBuffer : register(b2)
{
    float4 g_uvBounds;
    float4 g_crossUVBounds;
    float4 g_uvPrepassBounds;
    uint g_arrayIndex;
    bool g_doCutout;
    bool g_bPremultiplyAlpha;
};

cbuffer psPassConstantBuffer : register(b3)
{
    float2 g_depthRange;
    float2 g_depthCutoffRange;
    float g_opacity;
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_sharpness;
    int g_temporalFilteringSampling;
    float g_temporalFilteringColorRangeCutoff;
    float g_cutoutCombineFactor;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bDebugValidStereo;
    bool g_bUseFisheyeCorrection;
    bool g_bIsFirstRenderOfCameraFrame;
    bool g_bUseDepthCutoffRange;
    bool g_bClampCameraFrame;
};

cbuffer psMaskedConstantBuffer : register(b4)
{
    float3 g_maskedKey;
    float g_maskedFracChroma;
    float g_maskedFracLuma;
    float g_maskedSmooth;
    bool g_bMaskedUseCamera;
    bool g_bMaskedInvert;
};

#else

cbuffer psViewConstantBuffer : register(b1)
{
    float4 g_uvBounds;
    float4 g_crossUVBounds;
    float4 g_uvPrepassBounds;
    uint g_arrayIndex;
    bool g_doCutout;
    bool g_bPremultiplyAlpha;
};

cbuffer psPassConstantBuffer : register(b0)
{
    float2 g_depthRange;
    float2 g_depthCutoffRange;
    float g_opacity;
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_sharpness;
    int g_temporalFilteringSampling;
    float g_temporalFilteringColorRangeCutoff;
    float g_cutoutCombineFactor;
    bool g_bDoColorAdjustment;
    bool g_bDebugDepth;
    bool g_bDebugValidStereo;
    bool g_bUseFisheyeCorrection;
    bool g_bIsFirstRenderOfCameraFrame;
    bool g_bUseDepthCutoffRange;
    bool g_bClampCameraFrame;
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

#endif

