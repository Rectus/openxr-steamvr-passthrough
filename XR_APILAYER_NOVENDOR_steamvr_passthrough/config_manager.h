#pragma once
#include "SimpleIni.h"


enum EProjectionMode
{
	ProjectionRoomView2D = 0,
	ProjectionCustom2D = 1,
	ProjectionStereoReconstruction = 2,
};

struct Config_Main
{
	bool EnablePassthrough = true;
	EProjectionMode ProjectionMode = ProjectionRoomView2D;

	float PassthroughOpacity = 1.0f;
	float ProjectionDistanceFar = 10.0f;
	float FloorHeightOffset = 0.0f;
	float FieldOfViewScale = 0.9f;
	float DepthOffsetCalibration = 1.0f;

	float Brightness = 0.0f;
	float Contrast = 1.0f;
	float Saturation = 1.0f;

	bool RequireSteamVRRuntime = true;

	bool ShowTestImage = false;
	bool DebugDepth = false;
	bool DebugStereoValid = false;
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

enum EStereoAlgorithm
{
	StereoAlgorithm_BM = 0,
	StereoAlgorithm_SGBM = 1,
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
};

// Configuration for stereo reconstruction
struct Config_Stereo
{
	bool StereoUseMulticore = true;
	bool StereoReconstructionFreeze = false;
	bool StereoRectificationFiltering = false;
	int StereoFrameSkip = 0;
	int StereoDownscaleFactor = 4;
	EStereoAlgorithm StereoAlgorithm = StereoAlgorithm_SGBM;
	//int StereoAlgorithmQuality = 0;
	int StereoBlockSize = 7;
	int StereoMinDisparity = 0;
	int StereoMaxDisparity = 96;
	EStereoSGBM_Mode StereoSGBM_Mode = StereoMode_SGBM3Way;
	int StereoSGBM_P1 = 0;
	int StereoSGBM_P2 = 0;
	int StereoSGBM_DispMaxDiff = 0;
	int StereoSGBM_PreFilterCap = 4;
	int StereoSGBM_UniquenessRatio = 4;
	int StereoSGBM_SpeckleWindowSize = 80;
	int StereoSGBM_SpeckleRange = 1;

	EStereoFiltering StereoFiltering = StereoFiltering_WLS;
	float StereoWLS_Lambda = 8000.0f;
	float StereoWLS_Sigma = 1.8f;
	float StereoFBS_Spatial = 6.0f;
	float StereoFBS_Luma = 8.0f;
	float StereoFBS_Chroma = 8.0f;
	float StereoFBS_Lambda = 128.0f;
	int StereoFBS_Iterations = 11;
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
	Config_Stereo& GetConfig_Stereo() { return m_configStereo; }
	Config_Stereo& GetConfig_CustomStereo() { return m_configCustomStereo; }


private:
	void UpdateConfigFile();

	void ParseConfig_Main();
	void ParseConfig_Core();
	void ParseConfig_Stereo();

	void UpdateConfig_Main();
	void UpdateConfig_Core();
	void UpdateConfig_Stereo();

	std::wstring m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated;

	Config_Main m_configMain;
	Config_Core m_configCore;
	Config_Stereo m_configStereo;
	Config_Stereo m_configCustomStereo;
};

