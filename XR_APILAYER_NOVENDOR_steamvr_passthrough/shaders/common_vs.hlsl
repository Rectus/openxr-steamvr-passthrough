

cbuffer vsViewConstantBuffer : register(b0)
{
    float4x4 g_cameraProjectionToWorld;
    float4x4 g_worldToCameraProjection;
    float4x4 g_worldToHMDProjection;
    
    float4x4 g_prevCameraProjectionToWorld;
    float4x4 g_prevWorldToCameraProjection;
    float4x4 g_prevWorldToHMDProjection;
    float4 g_vsUVBounds;
    float3 g_hmdViewWorldPos;
    float g_projectionDistance;
    float g_floorHeightOffset;
    uint g_viewIndex;
};

cbuffer vsPassConstantBuffer : register(b1)
{
    float4x4 g_disparityViewToWorldLeft;
    float4x4 g_disparityViewToWorldRight;
    float4x4 g_disparityToDepth;
    uint2 g_disparityTextureSize;
    float g_disparityDownscaleFactor;
    float g_cutoutFactor;
    float g_cutoutOffset;
    float g_cutoutFilterWidth;
    int g_disparityFilterWidth;
    bool g_bProjectBorders;
    bool g_bFindDiscontinuities;
};
