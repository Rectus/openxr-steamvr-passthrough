#pragma once




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

	uint64_t HMDFirmwareVersion;
	uint64_t FPGAFirmwareVersion;
	bool bHMDSupportsRoomViewDirect;
	bool bSupportsRoomViewDepthProjection;
	bool bAllowCameraToggle;
	bool bAllowLightSourceFrequency;
};

struct DeviceIdentProperties
{
	uint32_t DeviceId;
	std::string DeviceName;
	std::string DeviceSerial;
};

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
