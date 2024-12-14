

cbuffer vsViewConstantBuffer : register(b0)
{
    float4x4 g_cameraProjectionToWorld;
    float4x4 g_worldToCameraProjection;
    float4x4 g_crossWorldToCameraProjection;
    float4x4 g_worldToHMDProjection;
    float4x4 g_HMDProjectionToWorld;
    float4x4 g_prevCameraProjectionToWorld;
    float4x4 g_prevWorldToCameraProjection;
    float4x4 g_prevWorldToHMDProjection;
    float4x4 g_prevDispWorldToCameraProjection;
    float4 g_disparityUVBounds;
    float3 g_projectionOriginWorld;
    float g_projectionDistance;
    float g_floorHeightOffset;
    uint g_viewIndex;
    bool g_bWriteDisparityFilter;
    bool g_bIsFirstRender;
};

cbuffer vsPassConstantBuffer : register(b1)
{
    float4x4 g_disparityViewToWorldLeft;
    float4x4 g_disparityViewToWorldRight;
    float4x4 g_prevDisparityViewToWorldLeft;
    float4x4 g_prevDisparityViewToWorldRight;
    float4x4 g_disparityToDepth;
    uint2 g_disparityTextureSize;
    float g_disparityDownscaleFactor;
    float g_cutoutFactor;
    float g_cutoutOffset;
    float g_cutoutFilterWidth;
    int g_disparityFilterWidth;
    bool g_bProjectBorders;
    bool g_bFindDiscontinuities;
    bool g_bUseDisparityTemporalFilter;
    float g_disparityTemporalFilterStrength;
    float g_disparityTemporalFilterDistance;
};
