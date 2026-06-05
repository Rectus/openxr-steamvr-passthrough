
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


struct ProjectedView
{
	XrMatrix4x4f WorldToProjection{};
	XrMatrix4x4f ViewToWorld{};
};


struct CameraGPUFrame
{
	CameraGPUFrame()
		: FrameTextureResource(nullptr)
		, FrameSequence(0)
		, FrameExposureTimestamp(0)
		, CameraLeft()
		, CameraRight()
		, FrameLayout(FrameLayout_Mono)
		, bIsValid(false)
		, bColorsPreadjusted(false)
		, bisRectifiedFrame(false)
	{
		FrameSize[0] = 0;
		FrameSize[1] = 0;
	}

	void* FrameTextureResource;
	int32_t FrameSize[2];
	uint32_t FrameSequence;
	uint64_t FrameExposureTimestamp;
	ProjectedView CameraLeft;
	ProjectedView CameraRight;
	EStereoFrameLayout FrameLayout;
	bool bIsValid;
	bool bColorsPreadjusted;
	bool bisRectifiedFrame;
};

struct CameraCPUFrame
{
	CameraCPUFrame()
		: CameraViewToWorldLeft()
		, CameraViewToWorldRight()
		, RawFrameFormat(FrameFormat_Unknown)
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


	std::shared_ptr<std::vector<uint8_t>> FrameBuffer;
	XrMatrix4x4f CameraViewToWorldLeft;
	XrMatrix4x4f CameraViewToWorldRight;
	
	ECameraFrameFormat RawFrameFormat;
	int32_t RawFrameDataBytes;
	int32_t RawFrameSize[2];

	int32_t FrameSize[2];
	uint32_t FrameSequence;
	uint64_t FrameExposureTimestamp;
	EStereoFrameLayout FrameLayout;
	bool bIsValid;
	bool bIsRaw;
};

struct DepthFrame
{
	DepthFrame()
		: DisparityViewToWorldLeft()
		, DisparityViewToWorldRight()
		, DisparityToDepth()
		, DisparityDownscaleFactor(0.0f)
		, FrameSequence(0)
		, FrameExposureTimestamp(0)
		, MinDisparity(0.0f)
		, MaxDisparity(0.0f)
		, bIsValid(false)
		, OutputDisparityMapNativeTexture(nullptr)
		, DisparityTextureIndex(-1)
	{
		InputDisparityTextureSize[0] = 0;
		InputDisparityTextureSize[1] = 0;
		OutputDisparityTextureSize[0] = 0;
		OutputDisparityTextureSize[1] = 0;
		CameraFrameTextureSize[0] = 0;
		CameraFrameTextureSize[1] = 0;
	}

	void* OutputDisparityMapNativeTexture;
	int DisparityTextureIndex;
	XrMatrix4x4f DisparityViewToWorldLeft;
	XrMatrix4x4f DisparityViewToWorldRight;
	XrMatrix4x4f DisparityToDepth;
	uint32_t InputDisparityTextureSize[2];
	uint32_t OutputDisparityTextureSize[2];
	uint32_t CameraFrameTextureSize[2];
	uint32_t FrameSequence;
	uint64_t FrameExposureTimestamp;
	float DisparityDownscaleFactor;
	float MinDisparity;
	float MaxDisparity;
	bool bIsValid;
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
	ECameraProvider CameraProvider = CameraProvider_None;
	EProjectionMode ProjectionMode = Projection_RoomView2D;
	EPassthroughBlendMode BlendMode = Opaque;
	uint64_t DisplayTime = 0;
	XrReferenceSpaceCreateInfo ReferenceSpace{};
	float ProjectionDistance = 10.0f;

	int LeftFrameIndex = -1;
	int RightFrameIndex = -1;
	int LeftDepthIndex = -1;
	int RightDepthIndex = -1;

	

	// Current view to be rendered
	ProjectedView HMDEyeLeft{};
	ProjectedView HMDEyeRight{};
	ProjectedView CameraLeft{};
	ProjectedView CameraRight{};

	// Relative to the previous rendered frame
	ProjectedView PrevHMDFrame_HMDEyeLeft{};
	ProjectedView PrevHMDFrame_HMDEyeRight{};

	// Relative to the last written history buffer
	ProjectedView ColorHistory_HMDEyeLeft{};
	ProjectedView ColorHistory_HMDEyeRight{};

	bool bIsFirstRenderOfCameraFrame = true;
	bool bMirrorRender = false;
	bool bInvertLayerAlpha = false;

	bool bHasReversedDepth = false;
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

struct VulkanTexture
{
	VkImage Image = VK_NULL_HANDLE;
	VkDeviceMemory 	Memory = VK_NULL_HANDLE;
	VkImageView View = VK_NULL_HANDLE;
	VkFramebuffer Framebuffer = VK_NULL_HANDLE;
	VkBuffer StagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory 	StagingBufferMemory = VK_NULL_HANDLE;
	uint8_t* MappedMemory = nullptr;

	HANDLE SharedHandle = INVALID_HANDLE_VALUE;
	void* nativeTexture = NULL;

	VkExtent2D Extent = { 0, 0 };
	bool bIsValid = false;
	VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkFormat Format = VK_FORMAT_UNDEFINED;
};
