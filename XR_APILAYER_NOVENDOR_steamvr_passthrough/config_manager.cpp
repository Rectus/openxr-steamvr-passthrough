#include "pch.h"
#include "config_manager.h"
#include <log.h>

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;

ConfigManager::ConfigManager(std::wstring configFile)
	: m_configFile(configFile)
	, m_bConfigUpdated(false)
	, m_iniData()
	, m_debugTexture()
{
	m_iniData.SetUnicode(true);
	SetupStereoPresets();
}

ConfigManager::~ConfigManager()
{
	DispatchUpdate();
}

void ConfigManager::ReadConfigFile()
{
	SI_Error result = m_iniData.LoadFile(m_configFile.c_str());
	if (result < 0)
	{
		Log("Failed to read config file, writing default values...\n");
		UpdateConfigFile();
	}
	else
	{
		m_configMain.ParseConfig(m_iniData, "Main");
		m_configCamera.ParseConfig(m_iniData, "Camera");
		m_configCore.ParseConfig(m_iniData, "Core");
		m_configExtensions.ParseConfig(m_iniData, "Extensions");
		m_configCustomStereo.ParseConfig(m_iniData, "StereoCustom");
		m_configDepth.ParseConfig(m_iniData, "Depth");
	}
	m_bConfigUpdated = false;

	m_stereoPresets[0] = m_configCustomStereo;
}

void ConfigManager::UpdateConfigFile()
{
	m_configMain.UpdateConfig(m_iniData, "Main");
	m_configCamera.UpdateConfig(m_iniData, "Camera");
	m_configCore.UpdateConfig(m_iniData, "Core");
	m_configExtensions.UpdateConfig(m_iniData, "Extensions");
	m_configCustomStereo.UpdateConfig(m_iniData, "StereoCustom");
	m_configDepth.UpdateConfig(m_iniData, "Depth");

	SI_Error result = m_iniData.SaveFile(m_configFile.c_str());
	if (result < 0)
	{
		ErrorLog("Failed to save config file, %i \n", errno);
	}

	m_bConfigUpdated = false;
}

void ConfigManager::ConfigUpdated()
{
	m_bConfigUpdated = true;

	if (m_configMain.StereoPreset == StereoPreset_Custom)
	{
		m_stereoPresets[0] = m_configCustomStereo;
	}
}

void ConfigManager::DispatchUpdate()
{
	if (m_bConfigUpdated)
	{
		UpdateConfigFile();
	}
}

void ConfigManager::ResetToDefaults()
{
	m_configMain = Config_Main();
	m_configCamera = Config_Camera();
	m_configCore = Config_Core();
	m_configExtensions = Config_Extensions();
	m_configCustomStereo = Config_Stereo();
	m_configDepth = Config_Depth();
	UpdateConfigFile();

	m_stereoPresets[0] = m_configCustomStereo;
}

void ConfigManager::SetupStereoPresets()
{
	m_stereoPresets[1].StereoUseMulticore = true;
	m_stereoPresets[1].StereoRectificationFiltering = false;
	m_stereoPresets[1].StereoUseColor = false;
	m_stereoPresets[1].StereoUseBWInputAlpha = false;
	m_stereoPresets[1].StereoUseHexagonGridMesh = false;
	m_stereoPresets[1].StereoFillHoles = true;
	m_stereoPresets[1].StereoDrawBackground = false;
	m_stereoPresets[1].StereoFrameSkip = 0;
	m_stereoPresets[1].StereoDownscaleFactor = 5;
	m_stereoPresets[1].StereoUseDeferredDepthPass = false;
	m_stereoPresets[1].StereoUseDisparityTemporalFiltering = false;

	m_stereoPresets[1].StereoDisparityBothEyes = false;
	m_stereoPresets[1].StereoDisparityFilterWidth = 0;
	m_stereoPresets[1].StereoCutoutEnabled = false;
	m_stereoPresets[1].StereoCutoutFactor = 0.75f;
	m_stereoPresets[1].StereoCutoutOffset = 1.5f;
	m_stereoPresets[1].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[1].StereoBlockSize = 9;
	m_stereoPresets[1].StereoMinDisparity = 0;
	m_stereoPresets[1].StereoMaxDisparity = 96;
	m_stereoPresets[1].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[1].StereoSGBM_P1 = 200;
	m_stereoPresets[1].StereoSGBM_P2 = 220;
	m_stereoPresets[1].StereoSGBM_DispMaxDiff = 0;
	m_stereoPresets[1].StereoSGBM_PreFilterCap = 0;
	m_stereoPresets[1].StereoSGBM_UniquenessRatio = 0;
	m_stereoPresets[1].StereoSGBM_SpeckleWindowSize = 0;
	m_stereoPresets[1].StereoSGBM_SpeckleRange = 0;

	m_stereoPresets[1].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[1].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[1].StereoWLS_Sigma = 1.9f;
	m_stereoPresets[1].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[1].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[1].StereoFBS_Luma = 8.0f;
	m_stereoPresets[1].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[1].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[1].StereoFBS_Iterations = 11;


	m_stereoPresets[2].StereoUseMulticore = true;
	m_stereoPresets[2].StereoRectificationFiltering = false;
	m_stereoPresets[2].StereoUseColor = false;
	m_stereoPresets[2].StereoUseBWInputAlpha = false;
	m_stereoPresets[2].StereoUseHexagonGridMesh = false;
	m_stereoPresets[2].StereoFillHoles = true;
	m_stereoPresets[2].StereoDrawBackground = false;
	m_stereoPresets[2].StereoFrameSkip = 0;
	m_stereoPresets[2].StereoDownscaleFactor = 4;
	m_stereoPresets[2].StereoUseDeferredDepthPass = true;
	m_stereoPresets[2].StereoUseDisparityTemporalFiltering = false;

	m_stereoPresets[2].StereoDisparityBothEyes = false;
	m_stereoPresets[2].StereoDisparityFilterWidth = 0;
	m_stereoPresets[2].StereoCutoutEnabled = false;
	m_stereoPresets[2].StereoCutoutFactor = 0.75f;
	m_stereoPresets[2].StereoCutoutOffset = 1.5f;
	m_stereoPresets[2].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[2].StereoBlockSize = 7;
	m_stereoPresets[2].StereoMinDisparity = 0;
	m_stereoPresets[2].StereoMaxDisparity = 96;
	m_stereoPresets[2].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[2].StereoSGBM_P1 = 200;
	m_stereoPresets[2].StereoSGBM_P2 = 220;
	m_stereoPresets[2].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[2].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[2].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[2].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[2].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[2].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[2].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[2].StereoWLS_Sigma = 0.5f;
	m_stereoPresets[2].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[2].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[2].StereoFBS_Luma = 8.0f;
	m_stereoPresets[2].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[2].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[2].StereoFBS_Iterations = 11;


	m_stereoPresets[3].StereoUseMulticore = true;
	m_stereoPresets[3].StereoRectificationFiltering = false;
	m_stereoPresets[3].StereoUseColor = false;
	m_stereoPresets[3].StereoUseBWInputAlpha = false;
	m_stereoPresets[3].StereoUseHexagonGridMesh = false;
	m_stereoPresets[3].StereoFillHoles = true;
	m_stereoPresets[3].StereoDrawBackground = false;
	m_stereoPresets[3].StereoFrameSkip = 0;
	m_stereoPresets[3].StereoDownscaleFactor = 4;
	m_stereoPresets[3].StereoUseDeferredDepthPass = true;
	m_stereoPresets[3].StereoUseDisparityTemporalFiltering = false;

	m_stereoPresets[3].StereoDisparityBothEyes = true;
	m_stereoPresets[3].StereoDisparityFilterWidth = 2;
	m_stereoPresets[3].StereoCutoutEnabled = false;
	m_stereoPresets[3].StereoCutoutFactor = 0.75f;
	m_stereoPresets[3].StereoCutoutOffset = 1.5f;
	m_stereoPresets[3].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[3].StereoBlockSize = 5;
	m_stereoPresets[3].StereoMinDisparity = 0;
	m_stereoPresets[3].StereoMaxDisparity = 96;
	m_stereoPresets[3].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[3].StereoSGBM_P1 = 40;
	m_stereoPresets[3].StereoSGBM_P2 = 64;
	m_stereoPresets[3].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[3].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[3].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[3].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[3].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[3].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[3].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[3].StereoWLS_Sigma = 0.5f;
	m_stereoPresets[3].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[3].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[3].StereoFBS_Luma = 8.0f;
	m_stereoPresets[3].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[3].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[3].StereoFBS_Iterations = 11;


	m_stereoPresets[4].StereoUseMulticore = true;
	m_stereoPresets[4].StereoRectificationFiltering = false;
	m_stereoPresets[4].StereoUseColor = false;
	m_stereoPresets[4].StereoUseBWInputAlpha = false;
	m_stereoPresets[4].StereoUseHexagonGridMesh = false;
	m_stereoPresets[4].StereoFillHoles = true;
	m_stereoPresets[4].StereoDrawBackground = false;
	m_stereoPresets[4].StereoFrameSkip = 0;
	m_stereoPresets[4].StereoDownscaleFactor = 3;
	m_stereoPresets[4].StereoUseDeferredDepthPass = true;
	m_stereoPresets[4].StereoUseDisparityTemporalFiltering = true;

	m_stereoPresets[4].StereoDisparityBothEyes = true;
	m_stereoPresets[4].StereoDisparityFilterWidth = 2;
	m_stereoPresets[4].StereoCutoutEnabled = false;
	m_stereoPresets[4].StereoCutoutFactor = 0.75f;
	m_stereoPresets[4].StereoCutoutOffset = 1.5f;
	m_stereoPresets[4].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[4].StereoBlockSize = 5;
	m_stereoPresets[4].StereoMinDisparity = 0;
	m_stereoPresets[4].StereoMaxDisparity = 96;
	m_stereoPresets[4].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[4].StereoSGBM_P1 = 40;
	m_stereoPresets[4].StereoSGBM_P2 = 64;
	m_stereoPresets[4].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[4].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[4].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[4].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[4].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[4].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[4].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[4].StereoWLS_Sigma = 0.5f;
	m_stereoPresets[4].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[4].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[4].StereoFBS_Luma = 8.0f;
	m_stereoPresets[4].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[4].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[4].StereoFBS_Iterations = 11;


	m_stereoPresets[5].StereoUseMulticore = true;
	m_stereoPresets[5].StereoRectificationFiltering = false;
	m_stereoPresets[5].StereoUseColor = false;
	m_stereoPresets[5].StereoUseBWInputAlpha = false;
	m_stereoPresets[5].StereoUseHexagonGridMesh = true;
	m_stereoPresets[5].StereoFillHoles = true;
	m_stereoPresets[5].StereoDrawBackground = false;
	m_stereoPresets[5].StereoFrameSkip = 0;
	m_stereoPresets[5].StereoDownscaleFactor = 2;
	m_stereoPresets[5].StereoUseDeferredDepthPass = true;
	m_stereoPresets[5].StereoUseDisparityTemporalFiltering = false;

	m_stereoPresets[5].StereoDisparityBothEyes = true;
	m_stereoPresets[5].StereoDisparityFilterWidth = 3;
	m_stereoPresets[5].StereoCutoutEnabled = true;
	m_stereoPresets[5].StereoCutoutFactor = 0.75f;
	m_stereoPresets[5].StereoCutoutOffset = 1.5f;
	m_stereoPresets[5].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[5].StereoBlockSize = 3;
	m_stereoPresets[5].StereoMinDisparity = 0;
	m_stereoPresets[5].StereoMaxDisparity = 96;
	m_stereoPresets[5].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 40;
	m_stereoPresets[5].StereoSGBM_P2 = 64;
	m_stereoPresets[5].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[5].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[5].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[5].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[5].StereoSGBM_SpeckleRange = 3;

	m_stereoPresets[5].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[5].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[5].StereoWLS_Sigma = 0.5f;
	m_stereoPresets[5].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[5].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[5].StereoFBS_Luma = 8.0f;
	m_stereoPresets[5].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[5].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[5].StereoFBS_Iterations = 11;
}
