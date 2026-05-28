
#pragma once

#include "shared_structs.h"
#include "mesh.h"

#define NEAR_PROJECTION_DISTANCE 0.05f

struct ExtensionData
{
	bool bAndroidPassthroughStateExtensionEnabled = false;
	bool bInverseAlphaExtensionEnabled = false;
	bool bFBPassthroughExtensionEnabled = false;
	bool bVarjoDepthExtensionEnabled = false;
	bool bVarjoCompositionExtensionEnabled = false;
	bool bVulkan2ExtensionEnabled = false;
};

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
		, bHasReversedDepth(false)
		, bIsFirstRender(true)
		, bIsRenderingMirrored(false)
	{
	}

	std::shared_mutex readWriteMutex;
	vr::CameraVideoStreamFrameHeader_t header;
	void* frameTextureResource;
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
	bool bHasReversedDepth;
	bool bIsFirstRender;
	bool bIsRenderingMirrored;
};

struct CameraCPUFrame
{
	CameraCPUFrame()
		: ReadWriteMutex()
		, CameraViewToWorldLeft()
		, CameraViewToWorldRight()
		, RawFrameFormat(0)
		, RawFrameDataBytes(0)
		, FrameSequence(0)
		, FrameExposureTimestamp(0)
		, FrameLayout(FrameLayout_Mono)
		, bIsValid(false)
		, bIsRaw(false)
	{
		RawFrameSize[0] = 0;
		RawFrameSize[1] = 0;
		FrameSize[0] = 0;
		FrameSize[1] = 0;
	}

	std::shared_mutex ReadWriteMutex;
	std::shared_ptr<std::vector<uint8_t>> FrameBuffer;
	XrMatrix4x4f CameraViewToWorldLeft;
	XrMatrix4x4f CameraViewToWorldRight;
	
	int32_t RawFrameFormat;
	int32_t RawFrameDataBytes;
	int32_t RawFrameSize[2];

	int32_t FrameSize[2];
	uint64_t FrameSequence;
	uint64_t FrameExposureTimestamp;
	EStereoFrameLayout FrameLayout;
	bool bIsValid;
	bool bIsRaw;
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
		, frameExposureTimestamp(0)
		, minDisparity(0.0f)
		, maxDisparity(0.0f)
		, bIsValid(false)
		, bIsFirstRender(true)
		, outputDisparityMapNativeTexture(nullptr)
		, disparityTextureIndex(-1)
	{
		inputDisparityTextureSize[0] = 0;
		inputDisparityTextureSize[1] = 0;
		outputDisparityTextureSize[0] = 0;
		outputDisparityTextureSize[1] = 0;
		cameraFrameTextureSize[0] = 0;
		cameraFrameTextureSize[1] = 0;
	}

	std::shared_mutex readWriteMutex;
	void* outputDisparityMapNativeTexture;
	int disparityTextureIndex;
	XrMatrix4x4f disparityViewToWorldLeft;
	XrMatrix4x4f disparityViewToWorldRight;
	XrMatrix4x4f disparityToDepth;
	XrMatrix4x4f prevDisparityViewToWorldLeft;
	XrMatrix4x4f prevDisparityViewToWorldRight;
	XrMatrix4x4f prevDispWorldToCameraProjectionLeft;
	XrMatrix4x4f prevDispWorldToCameraProjectionRight;
	uint32_t inputDisparityTextureSize[2];
	uint32_t outputDisparityTextureSize[2];
	uint32_t cameraFrameTextureSize[2];
	uint64_t frameExposureTimestamp;
	float disparityDownscaleFactor;
	float minDisparity;
	float maxDisparity;
	bool bIsValid;
	bool bIsFirstRender;
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

struct FrameRenderParameters
{
	EPassthroughBlendMode BlendMode = Opaque;
	bool bInvertLayerAlpha = false;
	uint64_t DisplayTime = 0;
	XrReferenceSpaceCreateInfo ReferenceSpace{};

	int LeftFrameIndex = -1;
	int RightFrameIndex = -1;
	int LeftDepthIndex = -1;
	int RightDepthIndex = -1;

	bool bReadApplicationDepth = false;
	bool bEnableDepthBlending = false;
	bool bEnableDepthRange = false;
	float DepthRangeMin = 0.0f;
	float DepthRangeMax = std::numeric_limits<float>::infinity();

	bool bForceColorSettings = false;
	float ForcedBrightness = 0.0f;
	float ForcedContrast = 0.0f;
	float ForcedSaturation = 0.0f;

	float RenderOpacity = 1.0f;

	bool bUseFBPassthrough = false; 
	FBPassthroughLayerInstance* FBLayer = nullptr;
	bool bVarjoDepthEnabled = false;

	std::shared_ptr<std::vector<RenderModel>> RenderModels;
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

struct FBPassthroughInstance
{
	bool InstanceCreated = false;
	XrPassthroughFB InstanceHandle = XR_NULL_HANDLE;
	bool PassthroughStarted = false;
	std::vector<FBPassthroughLayerInstance>Layers;
	XrPassthroughLayerFB LastLayerHandle = XR_NULL_HANDLE;
};
