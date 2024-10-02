// MIT License
//
// Copyright(c) 2022 Rectus
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "framework/dispatch.gen.h"
#include "mesh.h"

namespace steamvr_passthrough
{

    const std::string LayerName = "XR_APILAYER_NOVENDOR_steamvr_passthrough";
    const std::string VersionString = "0.3.0";

    // Singleton accessor.
    OpenXrApi* GetInstance();

    // A function to reset (delete) the singleton.
    void ResetInstance();

} // namespace steamvr_passthrough

enum ERenderAPI
{
    None,
    DirectX11,
    DirectX12,
    Vulkan,
    OpenGL
};

enum ERenderEye
{
	LEFT_EYE,
	RIGHT_EYE
};

enum EPassthroughBlendMode
{
	Masked = 0,
	Opaque = 1,
	Additive = 2,
	AlphaBlendPremultiplied = 3,
	AlphaBlendUnpremultiplied = 4
};

enum EStereoFrameLayout
{
	Mono = 0,
	StereoVerticalLayout = 1, // Stereo frames are Bottom/Top (for left/right respectively)
	StereoHorizontalLayout = 2 // Stereo frames are Left/Right
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
		, cameraProjectionToWorldLeft()
		, cameraProjectionToWorldRight()
		, worldToCameraProjectionLeft()
		, worldToCameraProjectionRight()
		, worldToHMDProjectionLeft()
		, worldToHMDProjectionRight()
		, prevCameraProjectionToWorldLeft()
		, prevCameraProjectionToWorldRight()
		, prevWorldToCameraProjectionLeft()
		, prevWorldToCameraProjectionRight()
		, prevWorldToHMDProjectionLeft()
		, prevWorldToHMDProjectionRight()
		, projectionOriginWorldLeft()
		, projectionOriginWorldRight()
		, frameLayout(Mono)
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
	XrMatrix4x4f cameraProjectionToWorldLeft;
	XrMatrix4x4f cameraProjectionToWorldRight;
	XrMatrix4x4f worldToCameraProjectionLeft;
	XrMatrix4x4f worldToCameraProjectionRight;
	XrMatrix4x4f worldToHMDProjectionLeft;
	XrMatrix4x4f worldToHMDProjectionRight;

	XrMatrix4x4f prevCameraProjectionToWorldLeft;
	XrMatrix4x4f prevCameraProjectionToWorldRight;
	XrMatrix4x4f prevWorldToCameraProjectionLeft;
	XrMatrix4x4f prevWorldToCameraProjectionRight;
	XrMatrix4x4f prevWorldToHMDProjectionLeft;
	XrMatrix4x4f prevWorldToHMDProjectionRight;

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
		, bIsValid(false)
	{
		disparityMap = std::make_shared<std::vector<uint16_t>>();
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
	bool bIsValid;
	bool bIsFirstRender;
};

struct FrameRenderParameters
{
	bool bEnableDepthBlending = false;
	bool bEnableDepthRange = false;
	float DepthRangeMin = 0.0f;
	float DepthRangeMax = std::numeric_limits<float>::infinity();
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

struct CameraDebugProperties
{
	vr::HmdVector2_t DistortedFocalLength;
	vr::HmdVector2_t UndistortedFocalLength;
	vr::HmdVector2_t MaximumUndistortedFocalLength;

	vr::HmdVector2_t DistortedOpticalCenter;
	vr::HmdVector2_t UndistortedOpticalCenter;
	vr::HmdVector2_t MaximumUndistortedOpticalCenter;

	vr::HmdMatrix44_t UndistortedProjecton;
	vr::HmdMatrix44_t MaximumUndistortedProjecton;

	vr::HmdMatrix34_t CameraToHeadTransform;
	vr::HmdVector4_t WhiteBalance;
	vr::EVRDistortionFunctionType DistortionFunction;
	double DistortionCoefficients[8];
};

struct DeviceDebugProperties
{
	vr::ETrackedDeviceClass DeviceClass;
	uint32_t DeviceId;
	std::string DeviceName;
	bool bHasCamera;
	uint32_t NumCameras;
	CameraDebugProperties CameraProps[4];

	uint32_t DistortedFrameHeight;
	uint32_t DistortedFrameWidth;
	uint32_t UndistortedFrameHeight;
	uint32_t UndistortedFrameWidth;
	uint32_t MaximumUndistortedFrameHeight;
	uint32_t MaximumUndistortedFrameWidth;

	vr::EVRTrackedCameraFrameLayout CameraFrameLayout;
	int32_t	CameraStreamFormat;
	vr::HmdMatrix34_t CameraToHeadTransform;
	uint64_t CameraFirmwareVersion;
	std::string CameraFirmwareDescription;
	int32_t CameraCompatibilityMode;
	bool bCameraSupportsCompatibilityModes;
	float CameraExposureTime;
	float CameraGlobalGain;
};

struct DeviceIdentProperties
{
	uint32_t DeviceId;
	std::string DeviceName;
	std::string DeviceSerial;
};

#define NEAR_PROJECTION_DISTANCE 0.05f


// Profiling functions

inline LARGE_INTEGER StartPerfTimer()
{
	LARGE_INTEGER startTime;	
	QueryPerformanceCounter(&startTime);
	return startTime;
}

inline float EndPerfTimer(LARGE_INTEGER startTime)
{
	LARGE_INTEGER endTime, perfFrequency;

	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTime.QuadPart - startTime.QuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float EndPerfTimer(uint64_t startTimeQuadPart)
{
	LARGE_INTEGER endTime, perfFrequency;

	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTime.QuadPart - startTimeQuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float GetPerfTimerDiff(uint64_t startTimeQuadPart, uint64_t endTimeQuadPart)
{
	LARGE_INTEGER perfFrequency;

	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTimeQuadPart - startTimeQuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float UpdateAveragePerfTime(std::deque<float>& times, float newTime, int numAverages)
{
	if (times.size() >= numAverages)
	{
		times.pop_front();
	}

	times.push_back(newTime);

	float average = 0;

	for (const float& val : times)
	{
		average += val;
	}
	return average / times.size();
}