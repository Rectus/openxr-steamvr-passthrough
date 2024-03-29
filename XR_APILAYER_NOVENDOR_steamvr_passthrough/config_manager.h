#pragma once
#include "SimpleIni.h"


enum EProjectionMode
{
	Projection_RoomView2D = 0,
	Projection_Custom2D = 1,
	Projection_StereoReconstruction = 2,
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
	EProjectionMode ProjectionMode = Projection_Custom2D;

	bool ProjectToRenderModels = false;

	float PassthroughOpacity = 1.0f;
	float ProjectionDistanceFar = 10.0f;
	float FloorHeightOffset = 0.0f;
	float FieldOfViewScale = 0.9f;
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

	EStereoPreset StereoPreset = StereoPreset_Medium;

	// Transient settings not written to file
	bool DebugDepth = false;
	bool DebugStereoValid = false;
	ESelectedDebugTexture DebugTexture = DebugTexture_None;
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
};

struct Config_Extensions
{
	bool ExtVarjoDepthEstimation = true;
	bool ExtVarjoDepthComposition = true;
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
	bool StereoUseHexagonGridMesh = true;
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
};

struct Config_Depth
{
	bool DepthReadFromApplication = true;
	bool DepthWriteOutput = true;
	bool DepthForceComposition = false;

	bool DepthForceRangeTest = false;
	float DepthForceRangeTestMin = 0.0f;
	float DepthForceRangeTestMax = 1.0f;
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

	Config_Main& GetConfig_Main() { return m_configMain; }
	Config_Core& GetConfig_Core() { return m_configCore; }
	Config_Extensions& GetConfig_Extensions() { return m_configExtensions; }
	Config_Stereo& GetConfig_Stereo() { return m_stereoPresets[m_configMain.StereoPreset]; }
	Config_Stereo& GetConfig_CustomStereo() { return m_configCustomStereo; }
	Config_Depth& GetConfig_Depth() { return m_configDepth; }

	DebugTexture& GetDebugTexture() { return m_debugTexture; }

private:
	void UpdateConfigFile();

	void SetupStereoPresets();

	void ParseConfig_Main();
	void ParseConfig_Core();
	void ParseConfig_Extensions();
	void ParseConfig_Stereo();
	void ParseConfig_Depth();

	void UpdateConfig_Main();
	void UpdateConfig_Core();
	void UpdateConfig_Extensions();
	void UpdateConfig_Stereo();
	void UpdateConfig_Depth();

	std::wstring m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated;

	Config_Main m_configMain;
	Config_Core m_configCore;
	Config_Extensions m_configExtensions;
	Config_Stereo m_configCustomStereo;
	Config_Stereo m_stereoPresets[6];
	Config_Depth m_configDepth;

	DebugTexture m_debugTexture;
};

