
#pragma once

#include "shared_structs.h"
#include "mesh.h"

#define NEAR_PROJECTION_DISTANCE 0.05f

struct RenderModel
{
	RenderModel()
		: deviceId(0)
		, meshToWorldTransform()
	{
	}

	RenderModel(uint32_t id, std::string name, Mesh<VertexFormatBasic> inMesh)
		: deviceId(id)
		, modelName(name)
		, mesh(inMesh)
		, meshToWorldTransform()
	{
	}

	uint32_t deviceId;
	std::string modelName;
	Mesh<VertexFormatBasic> mesh;
	XrMatrix4x4f meshToWorldTransform;
};

struct CameraFrame
{
	CameraFrame()
		: readWriteMutex()
		, header()
		, frameTextureResource(nullptr)
		, cameraViewToWorldLeft()
		, cameraViewToWorldRight()
		, worldToCameraProjectionLeft()
		, worldToCameraProjectionRight()
		, worldToHMDProjectionLeft()
		, worldToHMDProjectionRight()
		, prevWorldToCameraProjectionLeft()
		, prevWorldToCameraProjectionRight()
		, prevCameraFrame_WorldToHMDProjectionLeft()
		, prevCameraFrame_WorldToHMDProjectionRight()
		, prevHMDFrame_WorldToHMDProjectionLeft()
		, prevHMDFrame_WorldToHMDProjectionRight()
		, projectionOriginWorldLeft()
		, projectionOriginWorldRight()
		, frameLayout(FrameLayout_Mono)
		, bIsValid(false)
		, bHasFrameBuffer(false)
		, bHasReversedDepth(false)
		, bIsFirstRender(true)
		, bIsRenderingMirrored(false)
	{
	}

	std::shared_mutex readWriteMutex;
	vr::CameraVideoStreamFrameHeader_t header;
	void* frameTextureResource;
	std::shared_ptr<std::vector<uint8_t>> frameBuffer;
	std::shared_ptr<std::vector<uint8_t>> rectifiedFrameBuffer;
	XrMatrix4x4f cameraViewToWorldLeft;
	XrMatrix4x4f cameraViewToWorldRight;
	XrMatrix4x4f worldToCameraProjectionLeft;
	XrMatrix4x4f worldToCameraProjectionRight;
	XrMatrix4x4f worldToHMDProjectionLeft;
	XrMatrix4x4f worldToHMDProjectionRight;

	// relative to the previous camera frame
	XrMatrix4x4f prevWorldToCameraProjectionLeft;
	XrMatrix4x4f prevWorldToCameraProjectionRight;
	XrMatrix4x4f prevCameraFrame_WorldToHMDProjectionLeft;
	XrMatrix4x4f prevCameraFrame_WorldToHMDProjectionRight;

	// relative to the previous rendered frame
	XrMatrix4x4f prevHMDFrame_WorldToHMDProjectionLeft;
	XrMatrix4x4f prevHMDFrame_WorldToHMDProjectionRight;

	XrVector3f projectionOriginWorldLeft;
	XrVector3f projectionOriginWorldRight;
	EStereoFrameLayout frameLayout;
	bool bIsValid;
	bool bHasFrameBuffer;
	bool bHasReversedDepth;
	bool bIsFirstRender;
	bool bIsRenderingMirrored;

	std::shared_ptr<std::vector<RenderModel>> renderModels;
};

struct DepthFrame
{
	DepthFrame()
		: readWriteMutex()
		, disparityViewToWorldLeft()
		, disparityViewToWorldRight()
		, disparityToDepth()
		, prevDisparityViewToWorldLeft()
		, prevDisparityViewToWorldRight()
		, prevDispWorldToCameraProjectionLeft()
		, prevDispWorldToCameraProjectionRight()
		, disparityDownscaleFactor(0.0f)
		, minDisparity(0.0f)
		, maxDisparity(0.0f)
		, bIsValid(false)
		, bIsFirstRender(true)
	{
		disparityMap = std::make_shared<std::vector<uint16_t>>();
		disparityTextureSize[0] = 0;
		disparityTextureSize[1] = 0;
	}

	std::shared_mutex readWriteMutex;
	std::shared_ptr<std::vector<uint16_t>> disparityMap;
	XrMatrix4x4f disparityViewToWorldLeft;
	XrMatrix4x4f disparityViewToWorldRight;
	XrMatrix4x4f disparityToDepth;
	XrMatrix4x4f prevDisparityViewToWorldLeft;
	XrMatrix4x4f prevDisparityViewToWorldRight;
	XrMatrix4x4f prevDispWorldToCameraProjectionLeft;
	XrMatrix4x4f prevDispWorldToCameraProjectionRight;
	uint32_t disparityTextureSize[2];
	float disparityDownscaleFactor;
	float minDisparity;
	float maxDisparity;
	bool bIsValid;
	bool bIsFirstRender;
};

struct FrameRenderParameters
{
	EPassthroughBlendMode BlendMode = Opaque;
	bool bInvertLayerAlpha = false;

	int LeftFrameIndex = -1;
	int RightFrameIndex = -1;
	int LeftDepthIndex = -1;
	int RightDepthIndex = -1;

	bool bEnableDepthBlending = false;
	bool bEnableDepthRange = false;
	float DepthRangeMin = 0.0f;
	float DepthRangeMax = std::numeric_limits<float>::infinity();

	bool bForceColorSettings = false;
	float ForcedBrightness = 0.0f;
	float ForcedContrast = 0.0f;
	float ForcedSaturation = 0.0f;

	float RenderOpacity = 1.0f;
};

struct UVDistortionParameters
{
	UVDistortionParameters()
		: readWriteMutex()
		, cameraProjectionLeft()
		, cameraProjectionRight()
		, rectifiedRotationLeft()
		, rectifiedRotationRight()
		, fovScale(-1.0f)
	{
	}

	std::shared_mutex readWriteMutex;
	std::shared_ptr<std::vector<float>> uvDistortionMap;
	XrMatrix4x4f cameraProjectionLeft;
	XrMatrix4x4f cameraProjectionRight;
	XrMatrix4x4f rectifiedRotationLeft;
	XrMatrix4x4f rectifiedRotationRight;
	float fovScale;
};

struct FBPassthroughLayerInstance
{
	// Only the type 0 (reconstruction) XrPassthroughLayerPurposeFB is supported.
	XrPassthroughLayerFB Handle;
	bool LayerStarted;
	bool DepthEnabled;
	float Opacity;
	bool ColorAdjustmentEnabled;
	float Brightness;
	float Contrast;
	float Saturation;
};

struct FBPassthroughInstance
{
	bool InstanceCreated = false;
	XrPassthroughFB InstanceHandle = XR_NULL_HANDLE;
	bool PassthroughStarted = false;
	std::vector<FBPassthroughLayerInstance>Layers;
	XrPassthroughLayerFB LastLayerHandle = XR_NULL_HANDLE;
};
