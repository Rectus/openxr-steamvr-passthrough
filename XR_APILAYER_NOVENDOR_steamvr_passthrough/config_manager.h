#pragma once
#include "layer.h"
#include "SimpleIni.h"


enum ECameraProvider
{
	CameraProvider_OpenVR = 0,
	CameraProvider_OpenCV = 1,
	CameraProvider_Augmented = 2
};

enum EProjectionMode
{
	Projection_RoomView2D = 0,
	Projection_Custom2D = 1,
	Projection_StereoReconstruction = 2
};

enum ESelectedDebugTexture
{
	DebugTexture_None = 0,
	DebugTexture_TestImage,
	DebugTexture_Disparity,
	DebugTexture_Confidence
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
		, Width(0)
		, Height(0)
		, PixelSize(0)
		, Format(DebugTextureFormat_RGBA8)
		, bDimensionsUpdated(false)
		, RWMutex()
		, CurrentTexture(DebugTexture_None)
	{}

	std::vector<uint8_t> Texture;
	uint32_t Width;
	uint32_t Height;
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

struct Config_Main
{
	bool EnablePassthrough = true;
	ECameraProvider CameraProvider = CameraProvider_OpenVR;
	EProjectionMode ProjectionMode = Projection_Custom2D;

	bool ProjectToRenderModels = false;

	float PassthroughOpacity = 1.0f;
	float ProjectionDistanceFar = 10.0f;
	float FloorHeightOffset = 0.0f;
	float FieldOfViewScale = 1.0f;
	float DepthOffsetCalibration = 1.0f;

	float Brightness = 0.0f;
	float Contrast = 1.0f;
	float Saturation = 1.0f;
	float Sharpness = 0.0f;

	bool EnableTemporalFiltering = false;
	int TemporalFilteringSampling = 3;

	bool RequireSteamVRRuntime = true;
	bool ShowSettingDescriptions = true;
	bool UseLegacyD3D12Renderer = false;
	bool UseLegacyVulkanRenderer = false;

	bool PauseImageHandlingOnIdle = true;
	float IdleTimeSeconds = 10.0f;
	bool CloseCameraStreamOnPause = false;

	EStereoPreset StereoPreset = StereoPreset_Medium;

	// Transient settings not written to file
	bool DebugDepth = false;
	bool DebugStereoValid = false;
	ESelectedDebugTexture DebugTexture = DebugTexture_None;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		EnablePassthrough = ini.GetBoolValue(section, "EnablePassthrough", EnablePassthrough);
		CameraProvider = (ECameraProvider)ini.GetLongValue(section, "CameraProvider", CameraProvider);
		ProjectionMode = (EProjectionMode)ini.GetLongValue(section, "ProjectionMode", ProjectionMode);
		ProjectToRenderModels = ini.GetBoolValue(section, "ProjectToRenderModels", ProjectToRenderModels);

		PassthroughOpacity = (float)ini.GetDoubleValue(section, "PassthroughOpacity", PassthroughOpacity);
		ProjectionDistanceFar = (float)ini.GetDoubleValue(section, "ProjectionDistanceFar", ProjectionDistanceFar);
		FloorHeightOffset = (float)ini.GetDoubleValue(section, "FloorHeightOffset", FloorHeightOffset);
		FieldOfViewScale = (float)ini.GetDoubleValue(section, "FieldOfViewScale", FieldOfViewScale);
		DepthOffsetCalibration = (float)ini.GetDoubleValue(section, "DepthOffsetCalibration", DepthOffsetCalibration);

		Brightness = (float)ini.GetDoubleValue(section, "Brightness", Brightness);
		Contrast = (float)ini.GetDoubleValue(section, "Contrast", Contrast);
		Saturation = (float)ini.GetDoubleValue(section, "Saturation", Saturation);
		Sharpness = (float)ini.GetDoubleValue(section, "Sharpness", Sharpness);

		EnableTemporalFiltering = ini.GetBoolValue(section, "EnableTemporalFiltering", EnableTemporalFiltering);
		TemporalFilteringSampling = (int)ini.GetLongValue(section, "TemporalFilteringSampling", TemporalFilteringSampling);

		RequireSteamVRRuntime = ini.GetBoolValue(section, "RequireSteamVRRuntime", RequireSteamVRRuntime);
		ShowSettingDescriptions = ini.GetBoolValue(section, "ShowSettingDescriptions", ShowSettingDescriptions);
		UseLegacyD3D12Renderer = ini.GetBoolValue(section, "UseLegacyD3D12Renderer", UseLegacyD3D12Renderer);
		UseLegacyVulkanRenderer = ini.GetBoolValue(section, "UseLegacyVulkanRenderer", UseLegacyVulkanRenderer);

		PauseImageHandlingOnIdle = ini.GetBoolValue(section, "PauseImageHandlingOnIdle", PauseImageHandlingOnIdle);
		IdleTimeSeconds = (float)ini.GetDoubleValue(section, "IdleTimeSeconds", IdleTimeSeconds);
		CloseCameraStreamOnPause = ini.GetBoolValue(section, "CloseCameraStreamOnPause", CloseCameraStreamOnPause);

		StereoPreset = (EStereoPreset)ini.GetLongValue(section, "StereoPreset", StereoPreset);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "EnablePassthrough", EnablePassthrough);
		ini.SetLongValue(section, "CameraProvider", (long)CameraProvider);
		ini.SetLongValue(section, "ProjectionMode", (long)ProjectionMode);
		ini.SetBoolValue(section, "ProjectToRenderModels", ProjectToRenderModels);

		//ini.SetBoolValue(section, "ShowTestImage", ShowTestImage);
		ini.SetDoubleValue(section, "PassthroughOpacity", PassthroughOpacity);
		ini.SetDoubleValue(section, "ProjectionDistanceFar", ProjectionDistanceFar);
		ini.SetDoubleValue(section, "FloorHeightOffset", FloorHeightOffset);
		ini.SetDoubleValue(section, "FieldOfViewScale", FieldOfViewScale);
		ini.SetDoubleValue(section, "DepthOffsetCalibration", DepthOffsetCalibration);

		ini.SetDoubleValue(section, "Brightness", Brightness);
		ini.SetDoubleValue(section, "Contrast", Contrast);
		ini.SetDoubleValue(section, "Saturation", Saturation);
		ini.SetDoubleValue(section, "Sharpness", Sharpness);

		ini.SetBoolValue(section, "EnableTemporalFiltering", EnableTemporalFiltering);
		ini.SetLongValue(section, "TemporalFilteringSampling", TemporalFilteringSampling);

		ini.SetBoolValue(section, "RequireSteamVRRuntime", RequireSteamVRRuntime);
		ini.SetBoolValue(section, "ShowSettingDescriptions", ShowSettingDescriptions);
		ini.SetBoolValue(section, "UseLegacyD3D12Renderer", UseLegacyD3D12Renderer);
		ini.SetBoolValue(section, "UseLegacyVulkanRenderer", UseLegacyVulkanRenderer);

		ini.SetBoolValue(section, "PauseImageHandlingOnIdle", PauseImageHandlingOnIdle);
		ini.SetDoubleValue(section, "IdleTimeSeconds", IdleTimeSeconds);
		ini.SetBoolValue(section, "CloseCameraStreamOnPause", CloseCameraStreamOnPause);

		ini.SetLongValue(section, "StereoPreset", StereoPreset);
	}
};

struct Config_Camera
{
	bool ClampCameraFrame = true;

	bool UseTrackedDevice = true;
	std::string TrackedDeviceSerialNumber = "";

	bool RequestCustomFrameSize = false;
	int CustomFrameDimensions[2] = { 0 };

	int CustomFrameRate = 0;
	float FrameDelayOffset = 0.08f;

	bool AutoExposureEnable = false;
	float ExposureValue = -7.0f;

	EStereoFrameLayout CameraFrameLayout = Mono;
	bool CameraHasFisheyeLens = false;

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
		TrackedDeviceSerialNumber = ini.GetValue(section, "TrackedDeviceSerialNumber", TrackedDeviceSerialNumber.data());

		RequestCustomFrameSize = ini.GetBoolValue(section, "RequestCustomFrameSize", RequestCustomFrameSize);
		CustomFrameDimensions[0] = (int)ini.GetLongValue(section, "CustomFrameWidth", CustomFrameDimensions[0]);
		CustomFrameDimensions[1] = (int)ini.GetLongValue(section, "CustomFrameHeight", CustomFrameDimensions[1]);
		CustomFrameRate = (int)ini.GetLongValue(section, "CustomFrameRate", CustomFrameRate);
		FrameDelayOffset = (float)ini.GetDoubleValue(section, "FrameDelayOffset", FrameDelayOffset);

		AutoExposureEnable = ini.GetBoolValue(section, "AutoExposureEnable", AutoExposureEnable);
		ExposureValue = (float)ini.GetDoubleValue(section, "ExposureValue", ExposureValue);

		CameraFrameLayout = (EStereoFrameLayout)ini.GetLongValue(section, "CameraFrameLayout", CameraFrameLayout);
		CameraHasFisheyeLens = ini.GetBoolValue(section, "CameraHasFisheyeLens", CameraHasFisheyeLens);

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
		ini.SetValue(section, "TrackedDeviceSerialNumber", TrackedDeviceSerialNumber.data());

		ini.SetBoolValue(section, "RequestCustomFrameSize", RequestCustomFrameSize);
		ini.SetLongValue(section, "CustomFrameWidth", (long)CustomFrameDimensions[0]);
		ini.SetLongValue(section, "CustomFrameHeight", (long)CustomFrameDimensions[1]);
		ini.SetLongValue(section, "CustomFrameRate", (long)CustomFrameRate);
		ini.SetDoubleValue(section, "FrameDelayOffset", FrameDelayOffset);

		ini.SetBoolValue(section, "AutoExposureEnable", AutoExposureEnable);
		ini.SetDoubleValue(section, "ExposureValue", ExposureValue);

		ini.SetLongValue(section, "CameraFrameLayout", (long)CameraFrameLayout);
		ini.SetBoolValue(section, "CameraHasFisheyeLens", CameraHasFisheyeLens);

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
struct Config_Core
{
	bool CorePassthroughEnable = true;
	bool CoreAlphaBlend = true;
	bool CoreAdditive = true;
	int CorePreferredMode = 3;
	bool CoreForcePassthrough = false;
	int CoreForceMode = 1;
	float CoreForceMaskedFractionChroma = 0.2f;
	float CoreForceMaskedFractionLuma = 0.4f;
	float CoreForceMaskedSmoothing = 0.01f;
	float CoreForceMaskedKeyColor[3] = { 0 ,0 ,0 };
	bool CoreForceMaskedUseCameraImage = false;
	bool CoreForceMaskedInvertMask = false;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		CorePassthroughEnable = ini.GetBoolValue(section, "CorePassthroughEnable", CorePassthroughEnable);
		CoreAlphaBlend = ini.GetBoolValue(section, "CoreAlphaBlend", CoreAlphaBlend);
		CoreAdditive = ini.GetBoolValue(section, "CoreAdditive", CoreAdditive);
		CorePreferredMode = (int)ini.GetLongValue(section, "CorePreferredMode", CorePreferredMode);

		CoreForcePassthrough = ini.GetBoolValue(section, "CoreForcePassthrough", CoreForcePassthrough);
		CoreForceMode = (int)ini.GetLongValue(section, "CoreForceMode", CoreForceMode);
		CoreForceMaskedFractionChroma = (float)ini.GetDoubleValue(section, "CoreForceMaskedFractionChroma", CoreForceMaskedFractionChroma);
		CoreForceMaskedFractionLuma = (float)ini.GetDoubleValue(section, "CoreForceMaskedFractionLuma", CoreForceMaskedFractionLuma);
		CoreForceMaskedSmoothing = (float)ini.GetDoubleValue(section, "CoreForceMaskedSmoothing", CoreForceMaskedSmoothing);

		CoreForceMaskedKeyColor[0] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorR", CoreForceMaskedKeyColor[0]);
		CoreForceMaskedKeyColor[1] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorG", CoreForceMaskedKeyColor[1]);
		CoreForceMaskedKeyColor[2] = (float)ini.GetDoubleValue(section, "CoreForceMaskedKeyColorB", CoreForceMaskedKeyColor[2]);

		CoreForceMaskedUseCameraImage = ini.GetBoolValue(section, "CoreForceMaskedUseCameraImage", CoreForceMaskedUseCameraImage);
		CoreForceMaskedInvertMask = ini.GetBoolValue(section, "CoreForceMaskedInvertMask", CoreForceMaskedInvertMask);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "CorePassthroughEnable", CorePassthroughEnable);
		ini.SetBoolValue(section, "CoreAlphaBlend", CoreAlphaBlend);
		ini.SetBoolValue(section, "CoreAdditive", CoreAdditive);
		ini.SetLongValue(section, "CorePreferredMode", CorePreferredMode);

		ini.SetBoolValue(section, "CoreForcePassthrough", CoreForcePassthrough);
		ini.SetLongValue(section, "CoreForceMode", CoreForceMode);
		ini.SetDoubleValue(section, "CoreForceMaskedFractionChroma", CoreForceMaskedFractionChroma);
		ini.SetDoubleValue(section, "CoreForceMaskedFractionLuma", CoreForceMaskedFractionLuma);
		ini.SetDoubleValue(section, "CoreForceMaskedSmoothing", CoreForceMaskedSmoothing);

		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorR", CoreForceMaskedKeyColor[0]);
		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorG", CoreForceMaskedKeyColor[1]);
		ini.SetDoubleValue(section, "CoreForceMaskedKeyColorB", CoreForceMaskedKeyColor[2]);

		ini.SetBoolValue(section, "CoreForceMaskedUseCameraImage", CoreForceMaskedUseCameraImage);
		ini.SetBoolValue(section, "CoreForceMaskedInvertMask", CoreForceMaskedInvertMask);
	}
};

struct Config_Extensions
{
	bool ExtVarjoDepthEstimation = true;
	bool ExtVarjoDepthComposition = true;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		ExtVarjoDepthEstimation = ini.GetBoolValue(section, "ExtVarjoDepthEstimation", ExtVarjoDepthEstimation);
		ExtVarjoDepthComposition = ini.GetBoolValue(section, "ExtVarjoDepthComposition", ExtVarjoDepthComposition);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
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
struct Config_Stereo
{
	bool StereoUseMulticore = true;
	bool StereoReconstructionFreeze = false;
	bool StereoRectificationFiltering = false;
	bool StereoUseColor = false;
	bool StereoUseBWInputAlpha= false;
	bool StereoUseHexagonGridMesh = false;
	bool StereoFillHoles = true;
	int StereoFrameSkip = 0;
	int StereoDownscaleFactor = 2;
	bool StereoUseDisparityTemporalFiltering = false;
	float StereoDisparityTemporalFilteringStrength = 0.9f;
	float StereoDisparityTemporalFilteringDistance = 1.0f;

	bool StereoDisparityBothEyes = true;
	int StereoDisparityFilterWidth = 3;
	bool StereoCutoutEnabled = false;
	float StereoCutoutFactor = 0.75f;
	float StereoCutoutOffset = 1.5f;
	float StereoCutoutFilterWidth = 0.9f;

	int StereoBlockSize = 1;
	int StereoMinDisparity = 0;
	int StereoMaxDisparity = 96;
	EStereoSGBM_Mode StereoSGBM_Mode = StereoMode_SGBM3Way;
	int StereoSGBM_P1 = 200;
	int StereoSGBM_P2 = 220;
	int StereoSGBM_DispMaxDiff = 3;
	int StereoSGBM_PreFilterCap = 4;
	int StereoSGBM_UniquenessRatio = 4;
	int StereoSGBM_SpeckleWindowSize = 80;
	int StereoSGBM_SpeckleRange = 1;

	EStereoFiltering StereoFiltering = StereoFiltering_WLS;
	float StereoWLS_Lambda = 8000.0f;
	float StereoWLS_Sigma = 1.8f;
	float StereoWLS_ConfidenceRadius = 0.5f;
	float StereoFBS_Spatial = 6.0f;
	float StereoFBS_Luma = 8.0f;
	float StereoFBS_Chroma = 8.0f;
	float StereoFBS_Lambda = 128.0f;
	int StereoFBS_Iterations = 11;

	void ParseConfig(CSimpleIniA& ini, const char* section)
	{
		StereoUseMulticore = ini.GetBoolValue(section, "StereoUseMulticore", StereoUseMulticore);
		StereoRectificationFiltering = ini.GetBoolValue(section, "StereoRectificationFiltering", StereoRectificationFiltering);
		StereoUseColor = ini.GetBoolValue(section, "StereoUseColor", StereoUseColor);
		StereoUseBWInputAlpha = ini.GetBoolValue(section, "StereoUseBWInputAlpha", StereoUseBWInputAlpha);
		StereoUseHexagonGridMesh = ini.GetBoolValue(section, "StereoUseHexagonGridMesh", StereoUseHexagonGridMesh);
		StereoFillHoles = ini.GetBoolValue(section, "StereoFillHoles", StereoFillHoles);
		StereoFrameSkip = ini.GetLongValue(section, "StereoFrameSkip", StereoFrameSkip);
		StereoDownscaleFactor = ini.GetLongValue(section, "StereoDownscaleFactor", StereoDownscaleFactor);
		StereoUseDisparityTemporalFiltering = ini.GetBoolValue(section, "StereoUseDisparityTemporalFiltering", StereoUseDisparityTemporalFiltering);
		StereoDisparityTemporalFilteringStrength = (float)ini.GetDoubleValue(section, "StereoDisparityTemporalFilteringStrength", StereoDisparityTemporalFilteringStrength);
		StereoDisparityTemporalFilteringDistance = (float)ini.GetDoubleValue(section, "StereoDisparityTemporalFilteringDistance", StereoDisparityTemporalFilteringDistance);

		StereoDisparityBothEyes = ini.GetBoolValue(section, "StereoDisparityBothEyes", StereoDisparityBothEyes);
		StereoCutoutEnabled = ini.GetBoolValue(section, "StereoCutoutEnabled", StereoCutoutEnabled);
		StereoCutoutFactor = (float)ini.GetDoubleValue(section, "StereoCutoutFactor", StereoCutoutFactor);
		StereoCutoutOffset = (float)ini.GetDoubleValue(section, "StereoCutoutOffset", StereoCutoutOffset);
		StereoDisparityFilterWidth = (int)ini.GetLongValue(section, "StereoDisparityFilterWidth", StereoDisparityFilterWidth);
		StereoCutoutFilterWidth = (float)ini.GetDoubleValue(section, "StereoCutoutFilterWidth", StereoCutoutFilterWidth);

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

		StereoFiltering = (EStereoFiltering)ini.GetLongValue(section, "StereoFiltering", StereoFiltering);
		StereoWLS_Lambda = (float)ini.GetDoubleValue(section, "StereoWLS_Lambda", StereoWLS_Lambda);
		StereoWLS_Sigma = (float)ini.GetDoubleValue(section, "StereoWLS_Sigma", StereoWLS_Sigma);
		StereoWLS_ConfidenceRadius = (float)ini.GetDoubleValue(section, "StereoWLS_ConfidenceRadius", StereoWLS_ConfidenceRadius);
		StereoFBS_Spatial = (float)ini.GetDoubleValue(section, "StereoFBS_Spatial", StereoFBS_Spatial);
		StereoFBS_Luma = (float)ini.GetDoubleValue(section, "StereoFBS_Luma", StereoFBS_Luma);
		StereoFBS_Chroma = (float)ini.GetDoubleValue(section, "StereoFBS_Chroma", StereoFBS_Chroma);
		StereoFBS_Lambda = (float)ini.GetDoubleValue(section, "StereoFBS_Lambda", StereoFBS_Lambda);
		StereoFBS_Iterations = ini.GetLongValue(section, "StereoFBS_Iterations", StereoFBS_Iterations);
	}

	void UpdateConfig(CSimpleIniA& ini, const char* section)
	{
		ini.SetBoolValue(section, "StereoUseMulticore", StereoUseMulticore);
		ini.SetBoolValue(section, "StereoRectificationFiltering", StereoRectificationFiltering);
		ini.SetBoolValue(section, "StereoUseColor", StereoUseColor);
		ini.SetBoolValue(section, "StereoUseBWInputAlpha", StereoUseBWInputAlpha);
		ini.SetBoolValue(section, "StereoUseHexagonGridMesh", StereoUseHexagonGridMesh);
		ini.SetBoolValue(section, "StereoFillHoles", StereoFillHoles);
		ini.SetLongValue(section, "StereoFrameSkip", StereoFrameSkip);
		ini.SetLongValue(section, "StereoDownscaleFactor", StereoDownscaleFactor);
		ini.SetBoolValue(section, "StereoUseDisparityTemporalFiltering", StereoUseDisparityTemporalFiltering);
		ini.SetDoubleValue(section, "StereoDisparityTemporalFilteringStrength", StereoDisparityTemporalFilteringStrength);
		ini.SetDoubleValue(section, "StereoDisparityTemporalFilteringDistance", StereoDisparityTemporalFilteringDistance);

		ini.SetBoolValue(section, "StereoDisparityBothEyes", StereoDisparityBothEyes);
		ini.SetBoolValue(section, "StereoCutoutEnabled", StereoCutoutEnabled);
		ini.SetDoubleValue(section, "StereoCutoutFactor", StereoCutoutFactor);
		ini.SetDoubleValue(section, "StereoCutoutOffset", StereoCutoutOffset);
		ini.SetLongValue(section, "StereoDisparityFilterWidth", StereoDisparityFilterWidth);
		ini.SetDoubleValue(section, "StereoCutoutFilterWidth", StereoCutoutFilterWidth);

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

		ini.SetLongValue(section, "StereoFiltering", StereoFiltering);
		ini.SetDoubleValue(section, "StereoWLS_Lambda", StereoWLS_Lambda);
		ini.SetDoubleValue(section, "StereoWLS_Sigma", StereoWLS_Sigma);
		ini.SetDoubleValue(section, "StereoWLS_ConfidenceRadius", StereoWLS_ConfidenceRadius);
		ini.SetDoubleValue(section, "StereoFBS_Spatial", StereoFBS_Spatial);
		ini.SetDoubleValue(section, "StereoFBS_Luma", StereoFBS_Luma);
		ini.SetDoubleValue(section, "StereoFBS_Chroma", StereoFBS_Chroma);
		ini.SetDoubleValue(section, "StereoFBS_Lambda", StereoFBS_Lambda);
		ini.SetLongValue(section, "StereoFBS_Iterations", StereoFBS_Iterations);
	}
};

struct Config_Depth
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
	ConfigManager(std::wstring configFile);
	~ConfigManager();

	void ReadConfigFile();
	void ConfigUpdated();
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

	std::wstring m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated = false;
	bool m_bRendererResetPending = false;
	bool m_bCameraParamChangesPending = false;

	Config_Main m_configMain;
	Config_Camera m_configCamera;
	Config_Core m_configCore;
	Config_Extensions m_configExtensions;
	Config_Stereo m_configCustomStereo;
	Config_Stereo m_stereoPresets[6];
	Config_Depth m_configDepth;

	DebugTexture m_debugTexture;
};

