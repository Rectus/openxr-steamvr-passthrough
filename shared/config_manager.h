
#pragma once

#include "shared_structs.h"
#include "SimpleIni.h"



enum ECameraDistortionMode
{
	CameraDistortionMode_NotSet = -1,
	CameraDistortionMode_NoDistortion = 0,
	CameraDistortionMode_RegularLens = 1,
	CameraDistortionMode_Fisheye = 2
};

enum ESelectedDebugSource
{
	DebugSource_None = 0,
	DebugSource_OutputDepth,
	DebugSource_ApplicationAlpha,
	DebugSource_ApplicationDepth
};

enum ESelectedDebugTexture
{
	DebugTexture_None = 0,
	DebugTexture_TestImage,
	DebugTexture_Disparity,
	DebugTexture_Confidence
};

enum ESelectedDebugOverlay
{
	DebugOverlay_None = 0,
	DebugOverlay_ProjectionConfidence,
	DebugOverlay_CameraSelction,
	DebugOverlay_TemporalBlending,
	DebugOverlay_TemporalClipping,
	DebugOverlay_DiscontinuityFiltering
};

enum EDebugTextureFormat
{
	DebugTextureFormat_RGBA8,
	DebugTextureFormat_R8,
	DebugTextureFormat_R16U,
	DebugTextureFormat_R16S,
	DebugTextureFormat_R32F
};

struct DebugTexture
{
	DebugTexture()
		: Texture()
		, TextureSize{ 0, 0 }
		, PixelSize(0)
		, Format(DebugTextureFormat_RGBA8)
		, bDimensionsUpdated(false)
		, RWMutex()
		, CurrentTexture(DebugTexture_None)
	{}

	std::vector<uint8_t> Texture;
	VkExtent2D TextureSize;
	uint32_t PixelSize;
	EDebugTextureFormat Format;
	ESelectedDebugTexture CurrentTexture;
	bool bDimensionsUpdated;
	std::mutex RWMutex;
};

enum EStereoPreset
{
	StereoPreset_Custom = 0,
	StereoPreset_VeryLow = 1,
	StereoPreset_Low = 2,
	StereoPreset_Medium = 3,
	StereoPreset_High = 4,
	StereoPreset_VeryHigh = 5
};

struct alignas(4) Config_Main
{
	bool EnablePassthrough = true;
	ECameraProvider CameraProvider = CameraProvider_OpenVR;
	EProjectionMode ProjectionMode = Projection_Custom2D;

	bool ProjectToRenderModels = false;
	float ProjectionDistanceFar = 10.0f;
	float FloorHeightOffset = 0.0f;
	float FieldOfViewScale = 1.0f;

	float Brightness = 0.0f;
	float Contrast = 1.0f;
	float Saturation = 1.0f;
	float Sharpness = 0.0f;
	float GammaCorrection = 1.0f;

	bool EnableTemporalFiltering = false;
	int TemporalFilteringSampling = 3;
	float TemporalFilteringFactor = 0.9f;
	float TemporalFilteringRejectionOffset = 0.0f;

	bool RequireSteamVRRuntime = true;
	bool ShowSettingDescriptions = true;
	bool UseLegacyVulkanRenderer = false;
	bool AllowVulkanWithoutConfirmedFeatures = true;

	bool PauseImageHandlingOnIdle = true;
	float IdleTimeSeconds = 10.0f;
	bool CloseCameraStreamOnPause = false;
	bool LaunchMenuOnStartup = true;
	bool ShutdownMenuOnAppExit = true;

	bool EnableRenderDocDebugging = false;
	bool AutostartRenderDocInstance = false;
	bool InsertAsyncRendererRenderDocMarkers = false;
	bool EnableAsyncVulkanValidation = false;

	EStereoPreset StereoPreset = StereoPreset_Medium;

	// Transient settings not written to file
	bool DebugStereoReconstructionFreeze = false;
	ESelectedDebugSource DebugSource = DebugSource_None;
	ESelectedDebugOverlay DebugOverlay = DebugOverlay_None;
	ESelectedDebugTexture DebugTexture = DebugTexture_None;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		EnablePassthrough = ini.GetBoolValue(section, "EnablePassthrough", EnablePassthrough);
		CameraProvider = (ECameraProvider)ini.GetLongValue(section, "CameraProvider", CameraProvider);
		ProjectionMode = (EProjectionMode)ini.GetLongValue(section, "ProjectionMode", ProjectionMode);
		ProjectToRenderModels = ini.GetBoolValue(section, "ProjectToRenderModels", ProjectToRenderModels);

		ProjectionDistanceFar = (float)ini.GetDoubleValue(section, "ProjectionDistanceFar", ProjectionDistanceFar);
		FloorHeightOffset = (float)ini.GetDoubleValue(section, "FloorHeightOffset", FloorHeightOffset);
		FieldOfViewScale = (float)ini.GetDoubleValue(section, "FieldOfViewScale", FieldOfViewScale);

		Brightness = (float)ini.GetDoubleValue(section, "Brightness", Brightness);
		Contrast = (float)ini.GetDoubleValue(section, "Contrast", Contrast);
		Saturation = (float)ini.GetDoubleValue(section, "Saturation", Saturation);
		Sharpness = (float)ini.GetDoubleValue(section, "Sharpness", Sharpness);
		GammaCorrection = (float)ini.GetDoubleValue(section, "GammaCorrection", GammaCorrection);

		EnableTemporalFiltering = ini.GetBoolValue(section, "EnableTemporalFiltering", EnableTemporalFiltering);
		TemporalFilteringSampling = (int)ini.GetLongValue(section, "TemporalFilteringSampling", TemporalFilteringSampling);
		TemporalFilteringFactor = (float)ini.GetDoubleValue(section, "TemporalFilteringFactor", TemporalFilteringFactor);
		TemporalFilteringRejectionOffset = (float)ini.GetDoubleValue(section, "TemporalFilteringRejectionOffset", TemporalFilteringRejectionOffset);

		RequireSteamVRRuntime = ini.GetBoolValue(section, "RequireSteamVRRuntime", RequireSteamVRRuntime);
		ShowSettingDescriptions = ini.GetBoolValue(section, "ShowSettingDescriptions", ShowSettingDescriptions);
		UseLegacyVulkanRenderer = ini.GetBoolValue(section, "UseLegacyVulkanRenderer", UseLegacyVulkanRenderer);
		AllowVulkanWithoutConfirmedFeatures = ini.GetBoolValue(section, "AllowVulkanWithoutConfirmedFeatures", AllowVulkanWithoutConfirmedFeatures);

		PauseImageHandlingOnIdle = ini.GetBoolValue(section, "PauseImageHandlingOnIdle", PauseImageHandlingOnIdle);
		IdleTimeSeconds = (float)ini.GetDoubleValue(section, "IdleTimeSeconds", IdleTimeSeconds);
		CloseCameraStreamOnPause = ini.GetBoolValue(section, "CloseCameraStreamOnPause", CloseCameraStreamOnPause);
		LaunchMenuOnStartup = ini.GetBoolValue(section, "LaunchMenuOnStartup", LaunchMenuOnStartup);
		ShutdownMenuOnAppExit = ini.GetBoolValue(section, "ShutdownMenuOnAppExit", ShutdownMenuOnAppExit);

		EnableRenderDocDebugging = ini.GetBoolValue(section, "EnableRenderDocDebugging", EnableRenderDocDebugging);
		AutostartRenderDocInstance = ini.GetBoolValue(section, "AutostartRenderDocInstance", AutostartRenderDocInstance);
		InsertAsyncRendererRenderDocMarkers = ini.GetBoolValue(section, "InsertAsyncRendererRenderDocMarkers", InsertAsyncRendererRenderDocMarkers);
		EnableAsyncVulkanValidation = ini.GetBoolValue(section, "EnableAsyncVulkanValidation", EnableAsyncVulkanValidation);

		StereoPreset = (EStereoPreset)ini.GetLongValue(section, "StereoPreset", StereoPreset);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "EnablePassthrough", EnablePassthrough);
		ini.SetLongValue(section, "CameraProvider", (long)CameraProvider);
		ini.SetLongValue(section, "ProjectionMode", (long)ProjectionMode);
		ini.SetBoolValue(section, "ProjectToRenderModels", ProjectToRenderModels);

		ini.SetDoubleValue(section, "ProjectionDistanceFar", ProjectionDistanceFar);
		ini.SetDoubleValue(section, "FloorHeightOffset", FloorHeightOffset);
		ini.SetDoubleValue(section, "FieldOfViewScale", FieldOfViewScale);

		ini.SetDoubleValue(section, "Brightness", Brightness);
		ini.SetDoubleValue(section, "Contrast", Contrast);
		ini.SetDoubleValue(section, "Saturation", Saturation);
		ini.SetDoubleValue(section, "Sharpness", Sharpness);
		ini.SetDoubleValue(section, "GammaCorrection", GammaCorrection);

		ini.SetBoolValue(section, "EnableTemporalFiltering", EnableTemporalFiltering);
		ini.SetLongValue(section, "TemporalFilteringSampling", TemporalFilteringSampling);
		ini.SetDoubleValue(section, "TemporalFilteringFactor", TemporalFilteringFactor);
		ini.SetDoubleValue(section, "TemporalFilteringRejectionOffset", TemporalFilteringRejectionOffset);

		ini.SetBoolValue(section, "RequireSteamVRRuntime", RequireSteamVRRuntime);
		ini.SetBoolValue(section, "ShowSettingDescriptions", ShowSettingDescriptions);
		ini.SetBoolValue(section, "UseLegacyVulkanRenderer", UseLegacyVulkanRenderer);
		ini.SetBoolValue(section, "AllowVulkanWithoutConfirmedFeatures", AllowVulkanWithoutConfirmedFeatures);

		ini.SetBoolValue(section, "PauseImageHandlingOnIdle", PauseImageHandlingOnIdle);
		ini.SetDoubleValue(section, "IdleTimeSeconds", IdleTimeSeconds);
		ini.SetBoolValue(section, "CloseCameraStreamOnPause", CloseCameraStreamOnPause);
		ini.SetBoolValue(section, "LaunchMenuOnStartup", LaunchMenuOnStartup);
		ini.SetBoolValue(section, "ShutdownMenuOnAppExit", ShutdownMenuOnAppExit);

		ini.SetBoolValue(section, "EnableRenderDocDebugging", EnableRenderDocDebugging);
		ini.SetBoolValue(section, "AutostartRenderDocInstance", AutostartRenderDocInstance);
		ini.SetBoolValue(section, "InsertAsyncRendererRenderDocMarkers", InsertAsyncRendererRenderDocMarkers);
		ini.SetBoolValue(section, "EnableAsyncVulkanValidation", EnableAsyncVulkanValidation);

		ini.SetLongValue(section, "StereoPreset", StereoPreset);
	}
};

#define MAX_CAMERA_SERIAL_NUMBER_SIZE 127

struct alignas(4) Config_Camera
{
	bool ClampCameraFrame = false;

	bool UseTrackedDevice = true;
	char TrackedDeviceSerialNumber[MAX_CAMERA_SERIAL_NUMBER_SIZE + 1] = "";

	bool RequestCustomFrameSize = false;
	int CustomFrameDimensions[2] = { 0 };

	int CustomFrameRate = 0;
	float FrameDelayOffset = 0.08f;

	bool AutoExposureEnable = false;
	float ExposureValue = -7.0f;

	EStereoFrameLayout CameraFrameLayout = FrameLayout_Mono;
	bool CameraHasFisheyeLens = false;
	ECameraDistortionMode CameraForceDistortionMode = CameraDistortionMode_NotSet;

	int Camera0DeviceIndex = 0;

	float Camera0_Translation[3] = { 0.0f };
	float Camera0_Rotation[3] = { 0.0f };
	float Camera0_IntrinsicsFocal[2] = { 1.0f, 1.0f };
	float Camera0_IntrinsicsCenter[2] = { 0.5f, 0.5f };
	float Camera0_IntrinsicsDist[4] = { 0.0f };
	int Camera0_IntrinsicsSensorPixels[2] = { 1, 1 };

	float Camera1_Translation[3] = { 0.0f };
	float Camera1_Rotation[3] = { 0.0f };
	float Camera1_IntrinsicsFocal[2] = { 1.0f, 1.0f };
	float Camera1_IntrinsicsCenter[2] = { 0.5f, 0.5f };
	float Camera1_IntrinsicsDist[4] = { 0.0f };
	int Camera1_IntrinsicsSensorPixels[2] = { 1, 1 };

	bool OpenVR_UseBlockQueueForColor = true;
	bool OpenVR_UseBlockQueueForDepth = true;
	bool OpenVRCustomCalibration = false;
	bool OpenVR_CameraHasFisheyeLens = true;

	float OpenVR_Camera0_Translation[3] = { 0.0f };
	float OpenVR_Camera0_Rotation[3] = { 0.0f };
	float OpenVR_Camera0_IntrinsicsFocal[2] = { 1.0f, 1.0f };
	float OpenVR_Camera0_IntrinsicsCenter[2] = { 0.5f, 0.5f };
	float OpenVR_Camera0_IntrinsicsDist[4] = { 0.0f };
	int OpenVR_Camera0_IntrinsicsSensorPixels[2] = { 1, 1 };

	float OpenVR_Camera1_Translation[3] = { 0.0f };
	float OpenVR_Camera1_Rotation[3] = { 0.0f };
	float OpenVR_Camera1_IntrinsicsFocal[2] = { 1.0f, 1.0f };
	float OpenVR_Camera1_IntrinsicsCenter[2] = { 0.5f, 0.5f };
	float OpenVR_Camera1_IntrinsicsDist[4] = { 0.0f };
	int OpenVR_Camera1_IntrinsicsSensorPixels[2] = { 1, 1 };

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		ClampCameraFrame = ini.GetBoolValue(section, "ClampCameraFrame", ClampCameraFrame);

		UseTrackedDevice = ini.GetBoolValue(section, "UseTrackedDevice", UseTrackedDevice);

		const char* val = ini.GetValue(section, "TrackedDeviceSerialNumber", TrackedDeviceSerialNumber);
		strncpy_s(TrackedDeviceSerialNumber, val, MAX_CAMERA_SERIAL_NUMBER_SIZE);
		

		RequestCustomFrameSize = ini.GetBoolValue(section, "RequestCustomFrameSize", RequestCustomFrameSize);
		CustomFrameDimensions[0] = (int)ini.GetLongValue(section, "CustomFrameWidth", CustomFrameDimensions[0]);
		CustomFrameDimensions[1] = (int)ini.GetLongValue(section, "CustomFrameHeight", CustomFrameDimensions[1]);
		CustomFrameRate = (int)ini.GetLongValue(section, "CustomFrameRate", CustomFrameRate);
		FrameDelayOffset = (float)ini.GetDoubleValue(section, "FrameDelayOffset", FrameDelayOffset);

		AutoExposureEnable = ini.GetBoolValue(section, "AutoExposureEnable", AutoExposureEnable);
		ExposureValue = (float)ini.GetDoubleValue(section, "ExposureValue", ExposureValue);

		CameraFrameLayout = (EStereoFrameLayout)ini.GetLongValue(section, "CameraFrameLayout", CameraFrameLayout);
		CameraHasFisheyeLens = ini.GetBoolValue(section, "CameraHasFisheyeLens", CameraHasFisheyeLens);
		CameraForceDistortionMode = (ECameraDistortionMode)ini.GetLongValue(section, "CameraForceDistortionMode", CameraForceDistortionMode);

		Camera0DeviceIndex = (int)ini.GetLongValue(section, "Camera0DeviceIndex", Camera0DeviceIndex);

		Camera0_Translation[0] = (float)ini.GetDoubleValue(section, "Camera0_TranslationX", Camera0_Translation[0]);
		Camera0_Translation[1] = (float)ini.GetDoubleValue(section, "Camera0_TranslationY", Camera0_Translation[1]);
		Camera0_Translation[2] = (float)ini.GetDoubleValue(section, "Camera0_TranslationZ", Camera0_Translation[2]);
		Camera0_Rotation[0] = (float)ini.GetDoubleValue(section, "Camera0_RotationX", Camera0_Rotation[0]);
		Camera0_Rotation[1] = (float)ini.GetDoubleValue(section, "Camera0_RotationY", Camera0_Rotation[1]);
		Camera0_Rotation[2] = (float)ini.GetDoubleValue(section, "Camera0_RotationZ", Camera0_Rotation[2]);

		Camera0_IntrinsicsFocal[0] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsFocalX", Camera0_IntrinsicsFocal[0]);
		Camera0_IntrinsicsFocal[1] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsFocalY", Camera0_IntrinsicsFocal[1]);
		Camera0_IntrinsicsCenter[0] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsCenterX", Camera0_IntrinsicsCenter[0]);
		Camera0_IntrinsicsCenter[1] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsCenterY", Camera0_IntrinsicsCenter[1]);
		Camera0_IntrinsicsDist[0] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsDistR1", Camera0_IntrinsicsDist[0]);
		Camera0_IntrinsicsDist[1] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsDistR2", Camera0_IntrinsicsDist[1]);
		Camera0_IntrinsicsDist[2] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsDistT1", Camera0_IntrinsicsDist[2]);
		Camera0_IntrinsicsDist[3] = (float)ini.GetDoubleValue(section, "Camera0_IntrinsicsDistT2", Camera0_IntrinsicsDist[3]);
		Camera0_IntrinsicsSensorPixels[0] = (int)ini.GetLongValue(section, "Camera0_IntrinsicsSensorPixelsX", Camera0_IntrinsicsSensorPixels[0]);
		Camera0_IntrinsicsSensorPixels[1] = (int)ini.GetLongValue(section, "Camera0_IntrinsicsSensorPixelsY", Camera0_IntrinsicsSensorPixels[1]);

		Camera1_Translation[0] = (float)ini.GetDoubleValue(section, "Camera1_TranslationX", Camera1_Translation[0]);
		Camera1_Translation[1] = (float)ini.GetDoubleValue(section, "Camera1_TranslationY", Camera1_Translation[1]);
		Camera1_Translation[2] = (float)ini.GetDoubleValue(section, "Camera1_TranslationZ", Camera1_Translation[2]);
		Camera1_Rotation[0] = (float)ini.GetDoubleValue(section, "Camera1_RotationX", Camera1_Rotation[0]);
		Camera1_Rotation[1] = (float)ini.GetDoubleValue(section, "Camera1_RotationY", Camera1_Rotation[1]);
		Camera1_Rotation[2] = (float)ini.GetDoubleValue(section, "Camera1_RotationZ", Camera1_Rotation[2]);

		Camera1_IntrinsicsFocal[0] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsFocalX", Camera1_IntrinsicsFocal[0]);
		Camera1_IntrinsicsFocal[1] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsFocalY", Camera1_IntrinsicsFocal[1]);
		Camera1_IntrinsicsCenter[0] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsCenterX", Camera1_IntrinsicsCenter[0]);
		Camera1_IntrinsicsCenter[1] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsCenterY", Camera1_IntrinsicsCenter[1]);
		Camera1_IntrinsicsDist[0] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsDistR1", Camera1_IntrinsicsDist[0]);
		Camera1_IntrinsicsDist[1] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsDistR2", Camera1_IntrinsicsDist[1]);
		Camera1_IntrinsicsDist[2] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsDistT1", Camera1_IntrinsicsDist[2]);
		Camera1_IntrinsicsDist[3] = (float)ini.GetDoubleValue(section, "Camera1_IntrinsicsDistT2", Camera1_IntrinsicsDist[3]);
		Camera1_IntrinsicsSensorPixels[0] = (int)ini.GetLongValue(section, "Camera1_IntrinsicsSensorPixelsX", Camera1_IntrinsicsSensorPixels[0]);
		Camera1_IntrinsicsSensorPixels[1] = (int)ini.GetLongValue(section, "Camera1_IntrinsicsSensorPixelsY", Camera1_IntrinsicsSensorPixels[1]);

		OpenVR_UseBlockQueueForColor = ini.GetBoolValue(section, "OpenVR_UseBlockQueueForColor", OpenVR_UseBlockQueueForColor);
		OpenVR_UseBlockQueueForDepth = ini.GetBoolValue(section, "OpenVR_UseBlockQueueForDepth", OpenVR_UseBlockQueueForDepth);
		OpenVRCustomCalibration = ini.GetBoolValue(section, "OpenVRCustomCalibration", OpenVRCustomCalibration);
		OpenVR_CameraHasFisheyeLens = ini.GetBoolValue(section, "OpenVR_CameraHasFisheyeLens", OpenVR_CameraHasFisheyeLens);

		OpenVR_Camera0_Translation[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_TranslationX", OpenVR_Camera0_Translation[0]);
		OpenVR_Camera0_Translation[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_TranslationY", OpenVR_Camera0_Translation[1]);
		OpenVR_Camera0_Translation[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_TranslationZ", OpenVR_Camera0_Translation[2]);
		OpenVR_Camera0_Rotation[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_RotationX", OpenVR_Camera0_Rotation[0]);
		OpenVR_Camera0_Rotation[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_RotationY", OpenVR_Camera0_Rotation[1]);
		OpenVR_Camera0_Rotation[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_RotationZ", OpenVR_Camera0_Rotation[2]);

		OpenVR_Camera0_IntrinsicsFocal[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsFocalX", OpenVR_Camera0_IntrinsicsFocal[0]);
		OpenVR_Camera0_IntrinsicsFocal[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsFocalY", OpenVR_Camera0_IntrinsicsFocal[1]);
		OpenVR_Camera0_IntrinsicsCenter[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsCenterX", OpenVR_Camera0_IntrinsicsCenter[0]);
		OpenVR_Camera0_IntrinsicsCenter[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsCenterY", OpenVR_Camera0_IntrinsicsCenter[1]);
		OpenVR_Camera0_IntrinsicsDist[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistR1", OpenVR_Camera0_IntrinsicsDist[0]);
		OpenVR_Camera0_IntrinsicsDist[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistR2", OpenVR_Camera0_IntrinsicsDist[1]);
		OpenVR_Camera0_IntrinsicsDist[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistT1", OpenVR_Camera0_IntrinsicsDist[2]);
		OpenVR_Camera0_IntrinsicsDist[3] = (float)ini.GetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistT2", OpenVR_Camera0_IntrinsicsDist[3]);
		OpenVR_Camera0_IntrinsicsSensorPixels[0] = (int)ini.GetLongValue(section, "OpenVR_Camera0_IntrinsicsSensorPixelsX", OpenVR_Camera0_IntrinsicsSensorPixels[0]);
		OpenVR_Camera0_IntrinsicsSensorPixels[1] = (int)ini.GetLongValue(section, "OpenVR_Camera0_IntrinsicsSensorPixelsY", OpenVR_Camera0_IntrinsicsSensorPixels[1]);

		OpenVR_Camera1_Translation[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_TranslationX", OpenVR_Camera1_Translation[0]);
		OpenVR_Camera1_Translation[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_TranslationY", OpenVR_Camera1_Translation[1]);
		OpenVR_Camera1_Translation[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_TranslationZ", OpenVR_Camera1_Translation[2]);
		OpenVR_Camera1_Rotation[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_RotationX", OpenVR_Camera1_Rotation[0]);
		OpenVR_Camera1_Rotation[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_RotationY", OpenVR_Camera1_Rotation[1]);
		OpenVR_Camera1_Rotation[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_RotationZ", OpenVR_Camera1_Rotation[2]);

		OpenVR_Camera1_IntrinsicsFocal[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsFocalX", OpenVR_Camera1_IntrinsicsFocal[0]);
		OpenVR_Camera1_IntrinsicsFocal[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsFocalY", OpenVR_Camera1_IntrinsicsFocal[1]);
		OpenVR_Camera1_IntrinsicsCenter[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsCenterX", OpenVR_Camera1_IntrinsicsCenter[0]);
		OpenVR_Camera1_IntrinsicsCenter[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsCenterY", OpenVR_Camera1_IntrinsicsCenter[1]);
		OpenVR_Camera1_IntrinsicsDist[0] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistR1", OpenVR_Camera1_IntrinsicsDist[0]);
		OpenVR_Camera1_IntrinsicsDist[1] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistR2", OpenVR_Camera1_IntrinsicsDist[1]);
		OpenVR_Camera1_IntrinsicsDist[2] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistT1", OpenVR_Camera1_IntrinsicsDist[2]);
		OpenVR_Camera1_IntrinsicsDist[3] = (float)ini.GetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistT2", OpenVR_Camera1_IntrinsicsDist[3]);
		OpenVR_Camera1_IntrinsicsSensorPixels[0] = (int)ini.GetLongValue(section, "OpenVR_Camera1_IntrinsicsSensorPixelsX", OpenVR_Camera1_IntrinsicsSensorPixels[0]);
		OpenVR_Camera1_IntrinsicsSensorPixels[1] = (int)ini.GetLongValue(section, "OpenVR_Camera1_IntrinsicsSensorPixelsY", OpenVR_Camera1_IntrinsicsSensorPixels[1]);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "ClampCameraFrame", ClampCameraFrame);

		ini.SetBoolValue(section, "UseTrackedDevice", UseTrackedDevice);
		ini.SetValue(section, "TrackedDeviceSerialNumber", TrackedDeviceSerialNumber);

		ini.SetBoolValue(section, "RequestCustomFrameSize", RequestCustomFrameSize);
		ini.SetLongValue(section, "CustomFrameWidth", (long)CustomFrameDimensions[0]);
		ini.SetLongValue(section, "CustomFrameHeight", (long)CustomFrameDimensions[1]);
		ini.SetLongValue(section, "CustomFrameRate", (long)CustomFrameRate);
		ini.SetDoubleValue(section, "FrameDelayOffset", FrameDelayOffset);

		ini.SetBoolValue(section, "AutoExposureEnable", AutoExposureEnable);
		ini.SetDoubleValue(section, "ExposureValue", ExposureValue);

		ini.SetLongValue(section, "CameraFrameLayout", (long)CameraFrameLayout);
		ini.SetBoolValue(section, "CameraHasFisheyeLens", CameraHasFisheyeLens);
		ini.SetLongValue(section, "CameraForceDistortionMode", (long)CameraForceDistortionMode);

		ini.SetLongValue(section, "Camera0DeviceIndex", (long)Camera0DeviceIndex);

		ini.SetDoubleValue(section, "Camera0_TranslationX", Camera0_Translation[0]);
		ini.SetDoubleValue(section, "Camera0_TranslationY", Camera0_Translation[1]);
		ini.SetDoubleValue(section, "Camera0_TranslationZ", Camera0_Translation[2]);
		ini.SetDoubleValue(section, "Camera0_RotationX", Camera0_Rotation[0]);
		ini.SetDoubleValue(section, "Camera0_RotationY", Camera0_Rotation[1]);
		ini.SetDoubleValue(section, "Camera0_RotationZ", Camera0_Rotation[2]);

		ini.SetDoubleValue(section, "Camera0_IntrinsicsFocalX", Camera0_IntrinsicsFocal[0]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsFocalY", Camera0_IntrinsicsFocal[1]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsCenterX", Camera0_IntrinsicsCenter[0]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsCenterY", Camera0_IntrinsicsCenter[1]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsDistR1", Camera0_IntrinsicsDist[0]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsDistR2", Camera0_IntrinsicsDist[1]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsDistT1", Camera0_IntrinsicsDist[2]);
		ini.SetDoubleValue(section, "Camera0_IntrinsicsDistT2", Camera0_IntrinsicsDist[3]);
		ini.SetLongValue(section, "Camera0_IntrinsicsSensorPixelsX", Camera0_IntrinsicsSensorPixels[0]);
		ini.SetLongValue(section, "Camera0_IntrinsicsSensorPixelsY", Camera0_IntrinsicsSensorPixels[1]);

		ini.SetDoubleValue(section, "Camera1_TranslationX", Camera1_Translation[0]);
		ini.SetDoubleValue(section, "Camera1_TranslationY", Camera1_Translation[1]);
		ini.SetDoubleValue(section, "Camera1_TranslationZ", Camera1_Translation[2]);
		ini.SetDoubleValue(section, "Camera1_RotationX", Camera1_Rotation[0]);
		ini.SetDoubleValue(section, "Camera1_RotationY", Camera1_Rotation[1]);
		ini.SetDoubleValue(section, "Camera1_RotationZ", Camera1_Rotation[2]);

		ini.SetDoubleValue(section, "Camera1_IntrinsicsFocalX", Camera1_IntrinsicsFocal[0]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsFocalY", Camera1_IntrinsicsFocal[1]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsCenterX", Camera1_IntrinsicsCenter[0]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsCenterY", Camera1_IntrinsicsCenter[1]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsDistR1", Camera1_IntrinsicsDist[0]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsDistR2", Camera1_IntrinsicsDist[1]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsDistT1", Camera1_IntrinsicsDist[2]);
		ini.SetDoubleValue(section, "Camera1_IntrinsicsDistT2", Camera1_IntrinsicsDist[3]);
		ini.SetLongValue(section, "Camera1_IntrinsicsSensorPixelsX", Camera1_IntrinsicsSensorPixels[0]);
		ini.SetLongValue(section, "Camera1_IntrinsicsSensorPixelsY", Camera1_IntrinsicsSensorPixels[1]);

		ini.SetBoolValue(section, "OpenVR_UseBlockQueueForColor", OpenVR_UseBlockQueueForColor);
		ini.SetBoolValue(section, "OpenVR_UseBlockQueueForDepth", OpenVR_UseBlockQueueForDepth);
		ini.SetBoolValue(section, "OpenVRCustomCalibration", OpenVRCustomCalibration);
		ini.SetBoolValue(section, "OpenVR_CameraHasFisheyeLens", OpenVR_CameraHasFisheyeLens);

		ini.SetDoubleValue(section, "OpenVR_Camera0_TranslationX", OpenVR_Camera0_Translation[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_TranslationY", OpenVR_Camera0_Translation[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_TranslationZ", OpenVR_Camera0_Translation[2]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_RotationX", OpenVR_Camera0_Rotation[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_RotationY", OpenVR_Camera0_Rotation[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_RotationZ", OpenVR_Camera0_Rotation[2]);

		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsFocalX", OpenVR_Camera0_IntrinsicsFocal[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsFocalY", OpenVR_Camera0_IntrinsicsFocal[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsCenterX", OpenVR_Camera0_IntrinsicsCenter[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsCenterY", OpenVR_Camera0_IntrinsicsCenter[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistR1", OpenVR_Camera0_IntrinsicsDist[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistR2", OpenVR_Camera0_IntrinsicsDist[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistT1", OpenVR_Camera0_IntrinsicsDist[2]);
		ini.SetDoubleValue(section, "OpenVR_Camera0_IntrinsicsDistT2", OpenVR_Camera0_IntrinsicsDist[3]);
		ini.SetLongValue(section, "OpenVR_Camera0_IntrinsicsSensorPixelsX", OpenVR_Camera0_IntrinsicsSensorPixels[0]);
		ini.SetLongValue(section, "OpenVR_Camera0_IntrinsicsSensorPixelsY", OpenVR_Camera0_IntrinsicsSensorPixels[1]);

		ini.SetDoubleValue(section, "OpenVR_Camera1_TranslationX", OpenVR_Camera1_Translation[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_TranslationY", OpenVR_Camera1_Translation[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_TranslationZ", OpenVR_Camera1_Translation[2]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_RotationX", OpenVR_Camera1_Rotation[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_RotationY", OpenVR_Camera1_Rotation[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_RotationZ", OpenVR_Camera1_Rotation[2]);

		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsFocalX", OpenVR_Camera1_IntrinsicsFocal[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsFocalY", OpenVR_Camera1_IntrinsicsFocal[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsCenterX", OpenVR_Camera1_IntrinsicsCenter[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsCenterY", OpenVR_Camera1_IntrinsicsCenter[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistR1", OpenVR_Camera1_IntrinsicsDist[0]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistR2", OpenVR_Camera1_IntrinsicsDist[1]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistT1", OpenVR_Camera1_IntrinsicsDist[2]);
		ini.SetDoubleValue(section, "OpenVR_Camera1_IntrinsicsDistT2", OpenVR_Camera1_IntrinsicsDist[3]);
		ini.SetLongValue(section, "OpenVR_Camera1_IntrinsicsSensorPixelsX", OpenVR_Camera1_IntrinsicsSensorPixels[0]);
		ini.SetLongValue(section, "OpenVR_Camera1_IntrinsicsSensorPixelsY", OpenVR_Camera1_IntrinsicsSensorPixels[1]);
	}
};

// Configuration for core-spec passthrough
struct alignas(4) Config_Core
{
	bool CorePassthroughEnable = true;
	bool CoreAlphaBlend = true;
	bool CoreAdditive = true;
	int CorePreferredMode = 3;

	bool CoreForcePassthrough = false;
	int CoreForceMode = 1;

	int CoreForcePremultipliedAlpha = -1;
	float CoreForcePassthroughOpacity = 1.0f;
	float CoreForceAlphaTestTreshold = 0.33f;


	float CoreForceMaskedFractionChroma = 0.2f;
	float CoreForceMaskedFractionLuma = 0.4f;
	float CoreForceMaskedSmoothing = 0.01f;
	float CoreForceMaskedKeyColor[3] = { 0 ,0 ,0 };
	bool CoreForceMaskedUseCameraImage = false;
	bool CoreForceMaskedInvertMask = false;
	bool CoreForceMaskedUseAppAlpha = true;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		CorePassthroughEnable = ini.GetBoolValue(section, "CorePassthroughEnable", CorePassthroughEnable);
		CoreAlphaBlend = ini.GetBoolValue(section, "CoreAlphaBlend", CoreAlphaBlend);
		CoreAdditive = ini.GetBoolValue(section, "CoreAdditive", CoreAdditive);
		CorePreferredMode = (int)ini.GetLongValue(section, "CorePreferredMode", CorePreferredMode);

		CoreForcePassthrough = ini.GetBoolValue(section, "CoreForcePassthrough", CoreForcePassthrough);
		CoreForceMode = (int)ini.GetLongValue(section, "CoreForceMode", CoreForceMode);

		CoreForcePremultipliedAlpha = (int)ini.GetLongValue(section, "CoreForcePremultipliedAlpha", CoreForcePremultipliedAlpha);
		CoreForcePassthroughOpacity = (float)ini.GetDoubleValue(section, "CoreForcePassthroughOpacity", CoreForcePassthroughOpacity);
		CoreForceAlphaTestTreshold = (float)ini.GetDoubleValue(section, "CoreForceAlphaTestTreshold", CoreForceAlphaTestTreshold);

		CoreForceMaskedFractionChroma = (float)ini.GetDoubleValue(section, "CoreForceMaskedFractionChroma", CoreForceMaskedFractionChroma);
		CoreForceMaskedFractionLuma = (float)ini.GetDoubleValue(section, "CoreForceMaskedFractionLuma", CoreForceMaskedFractionLuma);
		CoreForceMaskedSmoothing = (float)ini.GetDoubleValue(section, "CoreForceMaskedSmoothing", CoreForceMaskedSmoothing);

		CoreForceMaskedKeyColor[0] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorR", CoreForceMaskedKeyColor[0]);
		CoreForceMaskedKeyColor[1] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorG", CoreForceMaskedKeyColor[1]);
		CoreForceMaskedKeyColor[2] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorB", CoreForceMaskedKeyColor[2]);

		CoreForceMaskedUseCameraImage = ini.GetBoolValue(section, "CoreForceMaskedUseCameraImage", CoreForceMaskedUseCameraImage);
		CoreForceMaskedInvertMask = ini.GetBoolValue(section, "CoreForceMaskedInvertMask", CoreForceMaskedInvertMask);
		CoreForceMaskedUseAppAlpha = ini.GetBoolValue(section, "CoreForceMaskedUseAppAlpha", CoreForceMaskedUseAppAlpha);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "CorePassthroughEnable", CorePassthroughEnable);
		ini.SetBoolValue(section, "CoreAlphaBlend", CoreAlphaBlend);
		ini.SetBoolValue(section, "CoreAdditive", CoreAdditive);
		ini.SetLongValue(section, "CorePreferredMode", CorePreferredMode);

		ini.SetBoolValue(section, "CoreForcePassthrough", CoreForcePassthrough);
		ini.SetLongValue(section, "CoreForceMode", CoreForceMode);

		ini.SetLongValue(section, "CoreForcePremultipliedAlpha", CoreForcePremultipliedAlpha);
		ini.SetDoubleValue(section, "CoreForcePassthroughOpacity", CoreForcePassthroughOpacity);
		ini.SetDoubleValue(section, "CoreForceAlphaTestTreshold", CoreForceAlphaTestTreshold);

		ini.SetDoubleValue(section, "CoreForceMaskedFractionChroma", CoreForceMaskedFractionChroma);
		ini.SetDoubleValue(section, "CoreForceMaskedFractionLuma", CoreForceMaskedFractionLuma);
		ini.SetDoubleValue(section, "CoreForceMaskedSmoothing", CoreForceMaskedSmoothing);

		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorR", CoreForceMaskedKeyColor[0]);
		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorG", CoreForceMaskedKeyColor[1]);
		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorB", CoreForceMaskedKeyColor[2]);

		ini.SetBoolValue(section, "CoreForceMaskedUseCameraImage", CoreForceMaskedUseCameraImage);
		ini.SetBoolValue(section, "CoreForceMaskedInvertMask", CoreForceMaskedInvertMask);
		ini.SetBoolValue(section, "CoreForceMaskedUseAppAlpha", CoreForceMaskedUseAppAlpha);
	}
};

struct alignas(4) Config_Extensions
{
	bool ExtFBPassthrough = true;
	bool ExtFBPassthroughAllowDepth = true;
	bool ExtFBPassthroughAllowColorSettings = true;
	bool ExtFBPassthroughFakeUnsupportedFeatures = false;

	bool ExtVarjoDepthEstimation = true;
	bool ExtVarjoDepthComposition = true;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		ExtFBPassthrough = ini.GetBoolValue(section, "ExtFBPassthrough", ExtFBPassthrough);
		ExtFBPassthroughAllowDepth = ini.GetBoolValue(section, "ExtFBPassthroughAllowDepth", ExtFBPassthroughAllowDepth);
		ExtFBPassthroughAllowColorSettings = ini.GetBoolValue(section, "ExtFBPassthroughAllowColorSettings", ExtFBPassthroughAllowColorSettings);
		ExtFBPassthroughFakeUnsupportedFeatures = ini.GetBoolValue(section, "ExtFBPassthroughFakeUnsupportedFeatures", ExtFBPassthroughFakeUnsupportedFeatures);

		ExtVarjoDepthEstimation = ini.GetBoolValue(section, "ExtVarjoDepthEstimation", ExtVarjoDepthEstimation);
		ExtVarjoDepthComposition = ini.GetBoolValue(section, "ExtVarjoDepthComposition", ExtVarjoDepthComposition);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "ExtFBPassthrough", ExtFBPassthrough);
		ini.SetBoolValue(section, "ExtFBPassthroughAllowDepth", ExtFBPassthroughAllowDepth);
		ini.SetBoolValue(section, "ExtFBPassthroughAllowColorSettings", ExtFBPassthroughAllowColorSettings);
		ini.SetBoolValue(section, "ExtFBPassthroughFakeUnsupportedFeatures", ExtFBPassthroughFakeUnsupportedFeatures);

		ini.SetBoolValue(section, "ExtVarjoDepthEstimation", ExtVarjoDepthEstimation);
		ini.SetBoolValue(section, "ExtVarjoDepthComposition", ExtVarjoDepthComposition);
	}
};

enum EStereoSGBM_Mode
{
	StereoMode_SGBM = 0,
	StereoMode_HH = 1,
	StereoMode_SGBM3Way = 2,
	StereoMode_HH4 = 3,
};

enum EStereoFiltering
{
	StereoFiltering_None = 0,
	StereoFiltering_WLS = 1,
	StereoFiltering_WLS_FBS = 2,
	StereoFiltering_FBS = 3
};

// Configuration for stereo reconstruction
struct alignas(4) Config_Stereo
{
	bool StereoUseMulticore = true;
	bool StereoRectificationFiltering = false;
	bool StereoUseColor = false;
	bool StereoUseBWInputAlpha = false;
	bool StereoUseHexagonGridMesh = false;
	bool StereoFillHoles = true;
	int StereoFillHolesIterations = 7;
	bool StereoDrawBackground = false;
	int StereoFrameSkip = 0;
	int StereoDownscaleFactor = 3;

	bool StereoUseDisparityTemporalFiltering = false;
	float StereoDisparityTemporalFilteringStrength = 0.9f;
	float StereoDisparityTemporalFilteringDistance = 0.5f;
	int StereoDepthMapScale = 1;

	bool StereoDisparityBothEyes = true;
	int StereoDisparityFilterWidth = 0;
	float StereoDisparityFilterConfidenceCutout = 0.5f;
	bool StereoCutoutEnabled = false;
	float StereoCutoutFactor = 3.0f;
	float StereoCutoutOffset = 1.0f;
	float StereoCutoutFilterWidth = 1.5f;
	float StereoCutoutCombineFactor = 1.0f;
	float StereoCutoutSecondaryCameraWeight = 0.5f;

	float StereoDepthContourStrength = 2.0f;
	float StereoDepthContourThreshold = 3.0f;

	float StereoDepthFullscreenContourStrength = 1.0f;
	float StereoDepthFullscreenContourThreshold = 0.15f;
	int StereoDepthFullscreenContourFilterWidth = 2;

	int StereoBlockSize = 5;
	int StereoMinDisparity = 0;
	int StereoMaxDisparity = 96;
	EStereoSGBM_Mode StereoSGBM_Mode = StereoMode_SGBM3Way;
	int StereoSGBM_P1 = 40;
	int StereoSGBM_P2 = 64;
	int StereoSGBM_DispMaxDiff = 0;
	int StereoSGBM_PreFilterCap = 4;
	int StereoSGBM_UniquenessRatio = 1;
	int StereoSGBM_SpeckleWindowSize = 80;
	int StereoSGBM_SpeckleRange = 3;

	bool StereoFilteringWLS_Enable = true;
	float StereoFilteringWLS_Lambda = 8000.0f;
	float StereoFilteringWLS_Sigma = 0.5f;
	float StereoFilteringWLS_ConfidenceRadius = 0.5f;

	bool StereoFilteringBilateral_Enable = false;
	int StereoFilteringBilateral_OutputScale = 1;
	int StereoFilteringBilateral_Distance = 9;
	float StereoFilteringBilateral_DispCutoff = 0.3f;
	float StereoFilteringBilateral_SigmaSpace = 10.0f;
	float StereoFilteringBilateral_SigmaLuma = 0.01f;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		StereoUseMulticore = ini.GetBoolValue(section, "StereoUseMulticore", StereoUseMulticore);
		StereoRectificationFiltering = ini.GetBoolValue(section, "StereoRectificationFiltering", StereoRectificationFiltering);
		StereoUseColor = ini.GetBoolValue(section, "StereoUseColor", StereoUseColor);
		StereoUseBWInputAlpha = ini.GetBoolValue(section, "StereoUseBWInputAlpha", StereoUseBWInputAlpha);
		StereoUseHexagonGridMesh = ini.GetBoolValue(section, "StereoUseHexagonGridMesh", StereoUseHexagonGridMesh);
		StereoFillHoles = ini.GetBoolValue(section, "StereoFillHoles", StereoFillHoles);
		StereoFillHolesIterations = ini.GetLongValue(section, "StereoFillHolesIterations", StereoFillHolesIterations);
		StereoDrawBackground = ini.GetBoolValue(section, "StereoDrawBackground", StereoDrawBackground);
		StereoFrameSkip = ini.GetLongValue(section, "StereoFrameSkip", StereoFrameSkip);
		StereoDownscaleFactor = ini.GetLongValue(section, "StereoDownscaleFactor", StereoDownscaleFactor);
		StereoUseDisparityTemporalFiltering = ini.GetBoolValue(section, "StereoUseDisparityTemporalFiltering", StereoUseDisparityTemporalFiltering);
		StereoDisparityTemporalFilteringStrength = (float)ini.GetDoubleValue(section, "StereoDisparityTemporalFilteringStrength", StereoDisparityTemporalFilteringStrength);
		StereoDisparityTemporalFilteringDistance = (float)ini.GetDoubleValue(section, "StereoDisparityTemporalFilteringDistance", StereoDisparityTemporalFilteringDistance);
		StereoDepthMapScale = ini.GetLongValue(section, "StereoDepthMapScale", StereoDepthMapScale);

		StereoDisparityBothEyes = ini.GetBoolValue(section, "StereoDisparityBothEyes", StereoDisparityBothEyes);
		StereoCutoutEnabled = ini.GetBoolValue(section, "StereoCutoutEnabled", StereoCutoutEnabled);
		StereoCutoutFactor = (float)ini.GetDoubleValue(section, "StereoCutoutFactor", StereoCutoutFactor);
		StereoCutoutOffset = (float)ini.GetDoubleValue(section, "StereoCutoutOffset", StereoCutoutOffset);
		StereoDisparityFilterWidth = (int)ini.GetLongValue(section, "StereoDisparityFilterWidth", StereoDisparityFilterWidth);
		StereoDisparityFilterConfidenceCutout = (float)ini.GetDoubleValue(section, "StereoDisparityFilterConfidenceCutout", StereoDisparityFilterConfidenceCutout);
		StereoCutoutFilterWidth = (float)ini.GetDoubleValue(section, "StereoCutoutFilterWidth", StereoCutoutFilterWidth);
		StereoCutoutCombineFactor = (float)ini.GetDoubleValue(section, "StereoCutoutCombineFactor", StereoCutoutCombineFactor);
		StereoCutoutSecondaryCameraWeight = (float)ini.GetDoubleValue(section, "StereoCutoutSecondaryCameraWeight", StereoCutoutSecondaryCameraWeight);

		StereoDepthContourStrength = (float)ini.GetDoubleValue(section, "StereoDepthContourStrength", StereoDepthContourStrength);
		StereoDepthContourThreshold = (float)ini.GetDoubleValue(section, "StereoDepthContourThreshold", StereoDepthContourThreshold);

		StereoDepthFullscreenContourStrength = (float)ini.GetDoubleValue(section, "StereoDepthFullscreenContourStrength", StereoDepthFullscreenContourStrength);
		StereoDepthFullscreenContourThreshold = (float)ini.GetDoubleValue(section, "StereoDepthFullscreenContourThreshold", StereoDepthFullscreenContourThreshold);
		StereoDepthFullscreenContourFilterWidth = ini.GetLongValue(section, "StereoDepthFullscreenContourFilterWidth", StereoDepthFullscreenContourFilterWidth);

		StereoBlockSize = ini.GetLongValue(section, "StereoBlockSize", StereoBlockSize);
		StereoMinDisparity = ini.GetLongValue(section, "StereoMinDisparity", StereoMinDisparity);
		StereoMaxDisparity = ini.GetLongValue(section, "StereoMaxDisparity", StereoMaxDisparity);
		StereoSGBM_Mode = (EStereoSGBM_Mode)ini.GetLongValue(section, "StereoSGBM_Mode", StereoSGBM_Mode);
		StereoSGBM_P1 = ini.GetLongValue(section, "StereoSGBM_P1", StereoSGBM_P1);
		StereoSGBM_P2 = ini.GetLongValue(section, "StereoSGBM_P2", StereoSGBM_P2);
		StereoSGBM_DispMaxDiff = ini.GetLongValue(section, "StereoSGBM_DispMaxDiff", StereoSGBM_DispMaxDiff);
		StereoSGBM_PreFilterCap = ini.GetLongValue(section, "StereoSGBM_PreFilterCap", StereoSGBM_PreFilterCap);
		StereoSGBM_UniquenessRatio = ini.GetLongValue(section, "StereoSGBM_UniquenessRatio", StereoSGBM_UniquenessRatio);
		StereoSGBM_SpeckleWindowSize = ini.GetLongValue(section, "StereoSGBM_SpeckleWindowSize", StereoSGBM_SpeckleWindowSize);
		StereoSGBM_SpeckleRange = ini.GetLongValue(section, "StereoSGBM_SpeckleRange", StereoSGBM_SpeckleRange);

		StereoFilteringWLS_Enable = ini.GetBoolValue(section, "StereoFilteringWLS_Enable", StereoFilteringWLS_Enable);
		StereoFilteringWLS_Lambda = (float)ini.GetDoubleValue(section, "StereoFilteringWLS_Lambda", StereoFilteringWLS_Lambda);
		StereoFilteringWLS_Sigma = (float)ini.GetDoubleValue(section, "StereoFilteringWLS_Sigma", StereoFilteringWLS_Sigma);
		StereoFilteringWLS_ConfidenceRadius = (float)ini.GetDoubleValue(section, "StereoFilteringWLS_ConfidenceRadius", StereoFilteringWLS_ConfidenceRadius);
		
		StereoFilteringBilateral_Enable = ini.GetBoolValue(section, "StereoFilteringBilateral_Enable", StereoFilteringBilateral_Enable);
		StereoFilteringBilateral_OutputScale = ini.GetLongValue(section, "StereoFilteringBilateral_OutputScale", StereoFilteringBilateral_OutputScale);
		StereoFilteringBilateral_Distance = ini.GetLongValue(section, "StereoFilteringBilateral_Distance", StereoFilteringBilateral_Distance);
		StereoFilteringBilateral_DispCutoff = (float)ini.GetDoubleValue(section, "StereoFilteringBilateral_DispCutoff", StereoFilteringBilateral_DispCutoff);
		StereoFilteringBilateral_SigmaSpace = (float)ini.GetDoubleValue(section, "StereoFilteringBilateral_SigmaSpace", StereoFilteringBilateral_SigmaSpace);
		StereoFilteringBilateral_SigmaLuma = (float)ini.GetDoubleValue(section, "StereoFilteringBilateral_SigmaLuma", StereoFilteringBilateral_SigmaLuma);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "StereoUseMulticore", StereoUseMulticore);
		ini.SetBoolValue(section, "StereoRectificationFiltering", StereoRectificationFiltering);
		ini.SetBoolValue(section, "StereoUseColor", StereoUseColor);
		ini.SetBoolValue(section, "StereoUseBWInputAlpha", StereoUseBWInputAlpha);
		ini.SetBoolValue(section, "StereoUseHexagonGridMesh", StereoUseHexagonGridMesh);
		ini.SetBoolValue(section, "StereoFillHoles", StereoFillHoles);
		ini.SetLongValue(section, "StereoFillHolesIterations", StereoFillHolesIterations);
		ini.SetBoolValue(section, "StereoDrawBackground", StereoDrawBackground);
		ini.SetLongValue(section, "StereoFrameSkip", StereoFrameSkip);
		ini.SetLongValue(section, "StereoDownscaleFactor", StereoDownscaleFactor);
		ini.SetBoolValue(section, "StereoUseDisparityTemporalFiltering", StereoUseDisparityTemporalFiltering);
		ini.SetDoubleValue(section, "StereoDisparityTemporalFilteringStrength", StereoDisparityTemporalFilteringStrength);
		ini.SetDoubleValue(section, "StereoDisparityTemporalFilteringDistance", StereoDisparityTemporalFilteringDistance);
		ini.SetLongValue(section, "StereoDepthMapScale", StereoDepthMapScale);

		ini.SetBoolValue(section, "StereoDisparityBothEyes", StereoDisparityBothEyes);
		ini.SetBoolValue(section, "StereoCutoutEnabled", StereoCutoutEnabled);
		ini.SetDoubleValue(section, "StereoCutoutFactor", StereoCutoutFactor);
		ini.SetDoubleValue(section, "StereoCutoutOffset", StereoCutoutOffset);
		ini.SetLongValue(section, "StereoDisparityFilterWidth", StereoDisparityFilterWidth);
		ini.SetDoubleValue(section, "StereoDisparityFilterConfidenceCutout", StereoDisparityFilterConfidenceCutout);
		ini.SetDoubleValue(section, "StereoCutoutFilterWidth", StereoCutoutFilterWidth);
		ini.SetDoubleValue(section, "StereoCutoutCombineFactor", StereoCutoutCombineFactor);
		ini.SetDoubleValue(section, "StereoCutoutSecondaryCameraWeight", StereoCutoutSecondaryCameraWeight);

		ini.SetDoubleValue(section, "StereoDepthContourStrength", StereoDepthContourStrength);
		ini.SetDoubleValue(section, "StereoDepthContourThreshold", StereoDepthContourThreshold);

		ini.SetDoubleValue(section, "StereoDepthFullscreenContourStrength", StereoDepthFullscreenContourStrength);
		ini.SetDoubleValue(section, "StereoDepthFullscreenContourThreshold", StereoDepthFullscreenContourThreshold);
		ini.SetLongValue(section, "StereoDepthFullscreenContourFilterWidth", StereoDepthFullscreenContourFilterWidth);

		ini.SetLongValue(section, "StereoBlockSize", StereoBlockSize);
		ini.SetLongValue(section, "StereoMinDisparity", StereoMinDisparity);
		ini.SetLongValue(section, "StereoMaxDisparity", StereoMaxDisparity);
		ini.SetLongValue(section, "StereoSGBM_Mode", StereoSGBM_Mode);
		ini.SetLongValue(section, "StereoSGBM_P1", StereoSGBM_P1);
		ini.SetLongValue(section, "StereoSGBM_P2", StereoSGBM_P2);
		ini.SetLongValue(section, "StereoSGBM_DispMaxDiff", StereoSGBM_DispMaxDiff);
		ini.SetLongValue(section, "StereoSGBM_PreFilterCap", StereoSGBM_PreFilterCap);
		ini.SetLongValue(section, "StereoSGBM_UniquenessRatio", StereoSGBM_UniquenessRatio);
		ini.SetLongValue(section, "StereoSGBM_SpeckleWindowSize", StereoSGBM_SpeckleWindowSize);
		ini.SetLongValue(section, "StereoSGBM_SpeckleRange", StereoSGBM_SpeckleRange);

		ini.SetBoolValue(section, "StereoFilteringWLS_Enable", StereoFilteringWLS_Enable);
		ini.SetDoubleValue(section, "StereoFilteringWLS_Lambda", StereoFilteringWLS_Lambda);
		ini.SetDoubleValue(section, "StereoFilteringWLS_Sigma", StereoFilteringWLS_Sigma);
		ini.SetDoubleValue(section, "StereoFilteringWLS_ConfidenceRadius", StereoFilteringWLS_ConfidenceRadius);

		ini.SetBoolValue(section, "StereoFilteringBilateral_Enable", StereoFilteringBilateral_Enable);
		ini.SetLongValue(section, "StereoFilteringBilateral_Distance", StereoFilteringBilateral_Distance);
		ini.SetLongValue(section, "StereoFilteringBilateral_OutputScale", StereoFilteringBilateral_OutputScale);
		ini.SetDoubleValue(section, "StereoFilteringBilateral_DispCutoff", StereoFilteringBilateral_DispCutoff);
		ini.SetDoubleValue(section, "StereoFilteringBilateral_SigmaSpace", StereoFilteringBilateral_SigmaSpace);
		ini.SetDoubleValue(section, "StereoFilteringBilateral_SigmaLuma", StereoFilteringBilateral_SigmaLuma);
	}
};

struct alignas(4) Config_Depth
{
	bool DepthReadFromApplication = true;
	bool DepthWriteOutput = true;
	bool DepthForceComposition = false;

	bool DepthForceRangeTest = false;
	float DepthForceRangeTestMin = 0.0f;
	float DepthForceRangeTestMax = 1.0f;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		DepthReadFromApplication = ini.GetBoolValue(section, "DepthReadFromApplication", DepthReadFromApplication);
		DepthWriteOutput = ini.GetBoolValue(section, "DepthWriteOutput", DepthWriteOutput);
		DepthForceComposition = ini.GetBoolValue(section, "DepthForceComposition", DepthForceComposition);

		DepthForceRangeTest = ini.GetBoolValue(section, "DepthForceRangeTest", DepthForceRangeTest);
		DepthForceRangeTestMin = (float)ini.GetDoubleValue(section, "DepthForceRangeTestMin", DepthForceRangeTestMin);
		DepthForceRangeTestMax = (float)ini.GetDoubleValue(section, "DepthForceRangeTestMax", DepthForceRangeTestMax);

	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "DepthReadFromApplication", DepthReadFromApplication);
		ini.SetBoolValue(section, "DepthWriteOutput", DepthWriteOutput);
		ini.SetBoolValue(section, "DepthForceComposition", DepthForceComposition);

		ini.SetBoolValue(section, "DepthForceRangeTest", DepthForceRangeTest);
		ini.SetDoubleValue(section, "DepthForceRangeTestMin", DepthForceRangeTestMin);
		ini.SetDoubleValue(section, "DepthForceRangeTestMax", DepthForceRangeTestMax);
	}
};


class ConfigManager
{
public:
	ConfigManager(const std::string_view& configFile, bool bAllowWrite);
	~ConfigManager();

	bool ReadConfigFile();
	void ConfigUpdated();
	bool IsUpdatePending() { return m_bConfigUpdated; }
	void DispatchUpdate();
	void ResetToDefaults();

	void SetRendererResetPending() { m_bRendererResetPending = true; }
	bool CheckResetRendererResetPending()
	{ 
		bool bPending = m_bRendererResetPending;
		m_bRendererResetPending = false; 
		return bPending;
	}

	void SetCameraParamChangesPending() { m_bCameraParamChangesPending = true; }
	bool CheckCameraParamChangesPending()
	{
		bool bPending = m_bCameraParamChangesPending;
		m_bCameraParamChangesPending = false;
		return bPending;
	}

	void SetFrameTextureDumpPending() { m_bFrameTextureDumpPending = true; }
	bool CheckFrameTextureDumpPending()
	{
		bool bPending = m_bFrameTextureDumpPending;
		m_bFrameTextureDumpPending = false;
		return bPending;
	}

	// TODO: make better system for things like this
	void SetEnableAsyncColorAdjustment(bool bEnable) { m_bEnableAsyncColorAdjustment = bEnable; }
	bool CheckEnableAsyncColorAdjustment()
	{
		return m_bEnableAsyncColorAdjustment;
	}

	Config_Main& GetConfig_Main() { return m_configMain; }
	Config_Camera& GetConfig_Camera() { return m_configCamera; }
	Config_Core& GetConfig_Core() { return m_configCore; }
	Config_Extensions& GetConfig_Extensions() { return m_configExtensions; }
	Config_Stereo& GetConfig_Stereo() { return m_stereoPresets[m_configMain.StereoPreset]; }
	Config_Stereo& GetConfig_CustomStereo() { return m_configCustomStereo; }
	Config_Depth& GetConfig_Depth() { return m_configDepth; }

	DebugTexture& GetDebugTexture() { return m_debugTexture; }

private:
	void UpdateConfigFile();

	void SetupStereoPresets();

	std::string m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated = false;
	bool m_bAllowWrite = false;
	bool m_bRendererResetPending = false;
	bool m_bCameraParamChangesPending = false;
	bool m_bFrameTextureDumpPending = false;
	bool m_bEnableAsyncColorAdjustment = true;

	Config_Main m_configMain;
	Config_Camera m_configCamera;
	Config_Core m_configCore;
	Config_Extensions m_configExtensions;
	Config_Stereo m_configCustomStereo;
	Config_Stereo m_stereoPresets[6];
	Config_Depth m_configDepth;

	DebugTexture m_debugTexture;
};

