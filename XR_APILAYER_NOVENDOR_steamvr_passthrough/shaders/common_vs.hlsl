

cbuffer vsViewConstantBuffer : register(b0)
{
    float4x4 g_worldToHMDProjection;
    float4x4 g_HMDProjectionToWorld;
    float4x4 g_prevWorldToHMDProjection;
    float4 g_disparityUVBounds;
    float3 g_projectionOriginWorld;
    float g_projectionDistance;
    float g_floorHeightOffset;
    int g_cameraViewIndex;
    bool g_bWriteDisparityFilter;
};

cbuffer vsPassConstantBuffer : register(b1)
{
    float4x4 g_worldToCameraFrameProjectionLeft;
	float4x4 g_worldToCameraFrameProjectionRight;
	float4x4 g_worldToPrevCameraFrameProjectionLeft;
	float4x4 g_worldToPrevCameraFrameProjectionRight;
	float4x4 g_worldToPrevDepthFrameProjectionLeft;
	float4x4 g_worldToPrevDepthFrameProjectionRight;
	float4x4 g_depthFrameViewToWorldLeft;
	float4x4 g_depthFrameViewToWorldRight;
	float4x4 g_prevDepthFrameViewToWorldLeft;
	float4x4 g_prevDepthFrameViewToWorldRight;
    
    float4x4 g_disparityToDepth;
    uint2 g_disparityTextureSize;
    float minDisparity;
	float maxDisparity;
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
