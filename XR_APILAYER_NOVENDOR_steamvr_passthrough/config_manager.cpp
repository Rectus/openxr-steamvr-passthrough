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
		ParseConfig_Main();
		ParseConfig_Core();
		ParseConfig_Extensions();
		ParseConfig_Stereo();
		ParseConfig_Depth();
	}
	m_bConfigUpdated = false;

	m_stereoPresets[0] = m_configCustomStereo;
}

void ConfigManager::UpdateConfigFile()
{
	UpdateConfig_Main();
	UpdateConfig_Core();
	UpdateConfig_Extensions();
	UpdateConfig_Stereo();
	UpdateConfig_Depth();

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
	m_stereoPresets[1].StereoUseHexagonGridMesh = true;
	m_stereoPresets[1].StereoFillHoles = false;
	m_stereoPresets[1].StereoFrameSkip = 0;
	m_stereoPresets[1].StereoDownscaleFactor = 6;

	m_stereoPresets[1].StereoDisparityBothEyes = false;
	m_stereoPresets[1].StereoDisparityFilterWidth = 0;
	m_stereoPresets[1].StereoCutoutEnabled = false;
	m_stereoPresets[1].StereoCutoutFactor = 0.75f;
	m_stereoPresets[1].StereoCutoutOffset = 1.5f;
	m_stereoPresets[1].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[1].StereoBlockSize = 11;
	m_stereoPresets[1].StereoMinDisparity = 0;
	m_stereoPresets[1].StereoMaxDisparity = 96;
	m_stereoPresets[1].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 200;
	m_stereoPresets[5].StereoSGBM_P2 = 220;
	m_stereoPresets[1].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[1].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[1].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[1].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[1].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[1].StereoFiltering = StereoFiltering_None;
	m_stereoPresets[1].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[1].StereoWLS_Sigma = 1.8f;
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
	m_stereoPresets[2].StereoUseHexagonGridMesh = true;
	m_stereoPresets[2].StereoFillHoles = true;
	m_stereoPresets[2].StereoFrameSkip = 0;
	m_stereoPresets[2].StereoDownscaleFactor = 4;

	m_stereoPresets[2].StereoDisparityBothEyes = false;
	m_stereoPresets[2].StereoDisparityFilterWidth = 1;
	m_stereoPresets[2].StereoCutoutEnabled = false;
	m_stereoPresets[2].StereoCutoutFactor = 0.75f;
	m_stereoPresets[2].StereoCutoutOffset = 1.5f;
	m_stereoPresets[2].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[2].StereoBlockSize = 1;
	m_stereoPresets[2].StereoMinDisparity = 0;
	m_stereoPresets[2].StereoMaxDisparity = 96;
	m_stereoPresets[2].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 200;
	m_stereoPresets[5].StereoSGBM_P2 = 220;
	m_stereoPresets[2].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[2].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[2].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[2].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[2].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[2].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[2].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[2].StereoWLS_Sigma = 1.8f;
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
	m_stereoPresets[3].StereoUseHexagonGridMesh = true;
	m_stereoPresets[3].StereoFillHoles = true;
	m_stereoPresets[3].StereoFrameSkip = 0;
	m_stereoPresets[3].StereoDownscaleFactor = 4;

	m_stereoPresets[3].StereoDisparityBothEyes = true;
	m_stereoPresets[3].StereoDisparityFilterWidth = 2;
	m_stereoPresets[3].StereoCutoutEnabled = false;
	m_stereoPresets[3].StereoCutoutFactor = 0.75f;
	m_stereoPresets[3].StereoCutoutOffset = 1.5f;
	m_stereoPresets[3].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[3].StereoBlockSize = 1;
	m_stereoPresets[3].StereoMinDisparity = 0;
	m_stereoPresets[3].StereoMaxDisparity = 96;
	m_stereoPresets[3].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 200;
	m_stereoPresets[5].StereoSGBM_P2 = 220;
	m_stereoPresets[3].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[3].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[3].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[3].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[3].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[3].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[3].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[3].StereoWLS_Sigma = 1.8f;
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
	m_stereoPresets[4].StereoUseHexagonGridMesh = true;
	m_stereoPresets[4].StereoFillHoles = true;
	m_stereoPresets[4].StereoFrameSkip = 0;
	m_stereoPresets[4].StereoDownscaleFactor = 3;

	m_stereoPresets[4].StereoDisparityBothEyes = true;
	m_stereoPresets[4].StereoDisparityFilterWidth = 4;
	m_stereoPresets[4].StereoCutoutEnabled = true;
	m_stereoPresets[4].StereoCutoutFactor = 0.75f;
	m_stereoPresets[4].StereoCutoutOffset = 1.5f;
	m_stereoPresets[4].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[4].StereoBlockSize = 1;
	m_stereoPresets[4].StereoMinDisparity = 0;
	m_stereoPresets[4].StereoMaxDisparity = 96;
	m_stereoPresets[4].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 200;
	m_stereoPresets[5].StereoSGBM_P2 = 220;
	m_stereoPresets[4].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[4].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[4].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[4].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[4].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[4].StereoFiltering = StereoFiltering_WLS;
	m_stereoPresets[4].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[4].StereoWLS_Sigma = 1.8f;
	m_stereoPresets[4].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[4].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[4].StereoFBS_Luma = 8.0f;
	m_stereoPresets[4].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[4].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[4].StereoFBS_Iterations = 11;


	m_stereoPresets[5].StereoUseMulticore = true;
	m_stereoPresets[5].StereoRectificationFiltering = false;
	m_stereoPresets[5].StereoUseColor = true;
	m_stereoPresets[5].StereoUseBWInputAlpha = false;
	m_stereoPresets[5].StereoUseHexagonGridMesh = true;
	m_stereoPresets[5].StereoFillHoles = true;
	m_stereoPresets[5].StereoFrameSkip = 0;
	m_stereoPresets[5].StereoDownscaleFactor = 2;

	m_stereoPresets[5].StereoDisparityBothEyes = true;
	m_stereoPresets[5].StereoDisparityFilterWidth = 5;
	m_stereoPresets[5].StereoCutoutEnabled = true;
	m_stereoPresets[5].StereoCutoutFactor = 0.75f;
	m_stereoPresets[5].StereoCutoutOffset = 1.5f;
	m_stereoPresets[5].StereoCutoutFilterWidth = 0.9f;

	m_stereoPresets[5].StereoBlockSize = 1;
	m_stereoPresets[5].StereoMinDisparity = 0;
	m_stereoPresets[5].StereoMaxDisparity = 96;
	m_stereoPresets[5].StereoSGBM_Mode = StereoMode_SGBM3Way;
	m_stereoPresets[5].StereoSGBM_P1 = 200;
	m_stereoPresets[5].StereoSGBM_P2 = 220;
	m_stereoPresets[5].StereoSGBM_DispMaxDiff = 3;
	m_stereoPresets[5].StereoSGBM_PreFilterCap = 4;
	m_stereoPresets[5].StereoSGBM_UniquenessRatio = 4;
	m_stereoPresets[5].StereoSGBM_SpeckleWindowSize = 80;
	m_stereoPresets[5].StereoSGBM_SpeckleRange = 1;

	m_stereoPresets[5].StereoFiltering = StereoFiltering_WLS_FBS;
	m_stereoPresets[5].StereoWLS_Lambda = 8000.0f;
	m_stereoPresets[5].StereoWLS_Sigma = 1.8f;
	m_stereoPresets[5].StereoWLS_ConfidenceRadius = 0.5f;
	m_stereoPresets[5].StereoFBS_Spatial = 6.0f;
	m_stereoPresets[5].StereoFBS_Luma = 8.0f;
	m_stereoPresets[5].StereoFBS_Chroma = 8.0f;
	m_stereoPresets[5].StereoFBS_Lambda = 128.0f;
	m_stereoPresets[5].StereoFBS_Iterations = 11;
}

void ConfigManager::ParseConfig_Main()
{
	m_configMain.EnablePassthrough = m_iniData.GetBoolValue("Main", "EnablePassthrough", m_configMain.EnablePassthrough);
	m_configMain.ProjectionMode = (EProjectionMode)m_iniData.GetLongValue("Main", "ProjectionMode", m_configMain.ProjectionMode);

	m_configMain.PassthroughOpacity = (float)m_iniData.GetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_configMain.ProjectionDistanceFar = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_configMain.FloorHeightOffset = (float)m_iniData.GetDoubleValue("Main", "FloorHeightOffset", m_configMain.FloorHeightOffset);
	m_configMain.FieldOfViewScale = (float)m_iniData.GetDoubleValue("Main", "FieldOfViewScale", m_configMain.FieldOfViewScale);
	m_configMain.DepthOffsetCalibration = (float)m_iniData.GetDoubleValue("Main", "DepthOffsetCalibration", m_configMain.DepthOffsetCalibration);

	m_configMain.Brightness = (float)m_iniData.GetDoubleValue("Main", "Brightness", m_configMain.Brightness);
	m_configMain.Contrast = (float)m_iniData.GetDoubleValue("Main", "Contrast", m_configMain.Contrast);
	m_configMain.Saturation = (float)m_iniData.GetDoubleValue("Main", "Saturation", m_configMain.Saturation);
	m_configMain.Sharpness = (float)m_iniData.GetDoubleValue("Main", "Sharpness", m_configMain.Sharpness);

	m_configMain.EnableTemporalFiltering = m_iniData.GetBoolValue("Main", "EnableTemporalFiltering", m_configMain.EnableTemporalFiltering);
	m_configMain.TemporalFilteringSampling = (int)m_iniData.GetLongValue("Main", "TemporalFilteringSampling", m_configMain.TemporalFilteringSampling);

	m_configMain.RequireSteamVRRuntime = m_iniData.GetBoolValue("Main", "RequireSteamVRRuntime", m_configMain.RequireSteamVRRuntime);	
	m_configMain.ShowSettingDescriptions = m_iniData.GetBoolValue("Main", "ShowSettingDescriptions", m_configMain.ShowSettingDescriptions);
	m_configMain.UseLegacyD3D12Renderer = m_iniData.GetBoolValue("Main", "UseLegacyD3D12Renderer", m_configMain.UseLegacyD3D12Renderer);

	m_configMain.StereoPreset = (EStereoPreset)m_iniData.GetLongValue("Main", "StereoPreset", m_configMain.StereoPreset);
}

void ConfigManager::ParseConfig_Core()
{
	m_configCore.CorePassthroughEnable = m_iniData.GetBoolValue("Core", "CorePassthroughEnable", m_configCore.CorePassthroughEnable);
	m_configCore.CoreAlphaBlend = m_iniData.GetBoolValue("Core", "CoreAlphaBlend", m_configCore.CoreAlphaBlend);
	m_configCore.CoreAdditive = m_iniData.GetBoolValue("Core", "CoreAdditive", m_configCore.CoreAdditive);
	m_configCore.CorePreferredMode = (int)m_iniData.GetLongValue("Core", "CorePreferredMode", m_configCore.CorePreferredMode);

	m_configCore.CoreForcePassthrough = m_iniData.GetBoolValue("Core", "CoreForcePassthrough", m_configCore.CoreForcePassthrough);
	m_configCore.CoreForceMode = (int)m_iniData.GetLongValue("Core", "CoreForceMode", m_configCore.CoreForceMode);
	m_configCore.CoreForceMaskedFractionChroma = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedFractionChroma", m_configCore.CoreForceMaskedFractionChroma);
	m_configCore.CoreForceMaskedFractionLuma = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedFractionLuma", m_configCore.CoreForceMaskedFractionLuma);
	m_configCore.CoreForceMaskedSmoothing = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedSmoothing", m_configCore.CoreForceMaskedSmoothing);

	m_configCore.CoreForceMaskedKeyColor[0] = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedKeyColorR", m_configCore.CoreForceMaskedKeyColor[0]);
	m_configCore.CoreForceMaskedKeyColor[1] = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedKeyColorG", m_configCore.CoreForceMaskedKeyColor[1]);
	m_configCore.CoreForceMaskedKeyColor[2] = (float)m_iniData.GetDoubleValue("Core", "CoreForceMaskedKeyColorB", m_configCore.CoreForceMaskedKeyColor[2]);

	m_configCore.CoreForceMaskedUseCameraImage = m_iniData.GetBoolValue("Core", "CoreForceMaskedUseCameraImage", m_configCore.CoreForceMaskedUseCameraImage);
	m_configCore.CoreForceMaskedInvertMask = m_iniData.GetBoolValue("Core", "CoreForceMaskedInvertMask", m_configCore.CoreForceMaskedInvertMask);
}

void ConfigManager::ParseConfig_Extensions()
{
	m_configExtensions.ExtVarjoDepthEstimation = m_iniData.GetBoolValue("Extensions", "ExtVarjoDepthEstimation", m_configExtensions.ExtVarjoDepthEstimation);
}

void ConfigManager::ParseConfig_Stereo()
{
	m_configCustomStereo.StereoUseMulticore = m_iniData.GetBoolValue("StereoCustom", "StereoUseMulticore", m_configCustomStereo.StereoUseMulticore);
	m_configCustomStereo.StereoRectificationFiltering = m_iniData.GetBoolValue("StereoCustom", "StereoRectificationFiltering", m_configCustomStereo.StereoRectificationFiltering);
	m_configCustomStereo.StereoUseColor = m_iniData.GetBoolValue("StereoCustom", "StereoUseColor", m_configCustomStereo.StereoUseColor);
	m_configCustomStereo.StereoUseBWInputAlpha = m_iniData.GetBoolValue("StereoCustom", "StereoUseBWInputAlpha", m_configCustomStereo.StereoUseBWInputAlpha);
	m_configCustomStereo.StereoUseHexagonGridMesh = m_iniData.GetBoolValue("StereoCustom", "StereoUseHexagonGridMesh", m_configCustomStereo.StereoUseHexagonGridMesh);
	m_configCustomStereo.StereoFillHoles = m_iniData.GetBoolValue("StereoCustom", "StereoFillHoles", m_configCustomStereo.StereoFillHoles);
	m_configCustomStereo.StereoFrameSkip = m_iniData.GetLongValue("StereoCustom", "StereoFrameSkip", m_configCustomStereo.StereoFrameSkip);
	m_configCustomStereo.StereoDownscaleFactor = m_iniData.GetLongValue("StereoCustom", "StereoDownscaleFactor", m_configCustomStereo.StereoDownscaleFactor);
	m_configCustomStereo.StereoUseDisparityTemporalFiltering = m_iniData.GetBoolValue("StereoCustom", "StereoUseDisparityTemporalFiltering", m_configCustomStereo.StereoUseDisparityTemporalFiltering);
	m_configCustomStereo.StereoDisparityTemporalFilteringStrength = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoDisparityTemporalFilteringStrength", m_configCustomStereo.StereoDisparityTemporalFilteringStrength);
	m_configCustomStereo.StereoDisparityTemporalFilteringDistance = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoDisparityTemporalFilteringDistance", m_configCustomStereo.StereoDisparityTemporalFilteringDistance);

	m_configCustomStereo.StereoDisparityBothEyes = m_iniData.GetBoolValue("StereoCustom", "StereoDisparityBothEyes", m_configCustomStereo.StereoDisparityBothEyes);
	m_configCustomStereo.StereoCutoutEnabled = m_iniData.GetBoolValue("StereoCustom", "StereoCutoutEnabled", m_configCustomStereo.StereoCutoutEnabled);
	m_configCustomStereo.StereoCutoutFactor = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoCutoutFactor", m_configCustomStereo.StereoCutoutFactor);
	m_configCustomStereo.StereoCutoutOffset = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoCutoutOffset", m_configCustomStereo.StereoCutoutOffset);
	m_configCustomStereo.StereoDisparityFilterWidth = (int)m_iniData.GetLongValue("StereoCustom", "StereoDisparityFilterWidth", m_configCustomStereo.StereoDisparityFilterWidth);
	m_configCustomStereo.StereoCutoutFilterWidth = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoCutoutFilterWidth", m_configCustomStereo.StereoCutoutFilterWidth);

	m_configCustomStereo.StereoBlockSize = m_iniData.GetLongValue("StereoCustom", "StereoBlockSize", m_configCustomStereo.StereoBlockSize);
	m_configCustomStereo.StereoMinDisparity = m_iniData.GetLongValue("StereoCustom", "StereoMinDisparity", m_configCustomStereo.StereoMinDisparity);
	m_configCustomStereo.StereoMaxDisparity = m_iniData.GetLongValue("StereoCustom", "StereoMaxDisparity", m_configCustomStereo.StereoMaxDisparity);
	m_configCustomStereo.StereoSGBM_Mode = (EStereoSGBM_Mode)m_iniData.GetLongValue("StereoCustom", "StereoSGBM_Mode", m_configCustomStereo.StereoSGBM_Mode);
	m_configCustomStereo.StereoSGBM_P1 = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_P1", m_configCustomStereo.StereoSGBM_P1);
	m_configCustomStereo.StereoSGBM_P2 = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_P2", m_configCustomStereo.StereoSGBM_P2);
	m_configCustomStereo.StereoSGBM_DispMaxDiff = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_DispMaxDiff", m_configCustomStereo.StereoSGBM_DispMaxDiff);
	m_configCustomStereo.StereoSGBM_PreFilterCap = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_PreFilterCap", m_configCustomStereo.StereoSGBM_PreFilterCap);
	m_configCustomStereo.StereoSGBM_UniquenessRatio = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_UniquenessRatio", m_configCustomStereo.StereoSGBM_UniquenessRatio);
	m_configCustomStereo.StereoSGBM_SpeckleWindowSize = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_SpeckleWindowSize", m_configCustomStereo.StereoSGBM_SpeckleWindowSize);
	m_configCustomStereo.StereoSGBM_SpeckleRange = m_iniData.GetLongValue("StereoCustom", "StereoSGBM_SpeckleRange", m_configCustomStereo.StereoSGBM_SpeckleRange);

	m_configCustomStereo.StereoFiltering = (EStereoFiltering)m_iniData.GetLongValue("StereoCustom", "StereoFiltering", m_configCustomStereo.StereoFiltering);
	m_configCustomStereo.StereoWLS_Lambda = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoWLS_Lambda", m_configCustomStereo.StereoWLS_Lambda);
	m_configCustomStereo.StereoWLS_Sigma = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoWLS_Sigma", m_configCustomStereo.StereoWLS_Sigma);
	m_configCustomStereo.StereoWLS_ConfidenceRadius = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoWLS_ConfidenceRadius", m_configCustomStereo.StereoWLS_ConfidenceRadius);
	m_configCustomStereo.StereoFBS_Spatial = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Spatial", m_configCustomStereo.StereoFBS_Spatial);
	m_configCustomStereo.StereoFBS_Luma = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Luma", m_configCustomStereo.StereoFBS_Luma);
	m_configCustomStereo.StereoFBS_Chroma = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Chroma", m_configCustomStereo.StereoFBS_Chroma);
	m_configCustomStereo.StereoFBS_Lambda = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Lambda", m_configCustomStereo.StereoFBS_Lambda);
	m_configCustomStereo.StereoFBS_Iterations = m_iniData.GetLongValue("StereoCustom", "StereoFBS_Iterations", m_configCustomStereo.StereoFBS_Iterations);
}

void ConfigManager::ParseConfig_Depth()
{
	m_configDepth.DepthReadFromApplication = m_iniData.GetBoolValue("Depth", "DepthReadFromApplication", m_configDepth.DepthReadFromApplication);
	m_configDepth.DepthWriteOutput = m_iniData.GetBoolValue("Depth", "DepthWriteOutput", m_configDepth.DepthWriteOutput);
	m_configDepth.DepthForceComposition = m_iniData.GetBoolValue("Depth", "DepthForceComposition", m_configDepth.DepthForceComposition);
}


void ConfigManager::UpdateConfig_Main()
{
	m_iniData.SetBoolValue("Main", "EnablePassthrough", m_configMain.EnablePassthrough);
	m_iniData.SetLongValue("Main", "ProjectionMode", (long)m_configMain.ProjectionMode);

	//m_iniData.SetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_iniData.SetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_iniData.SetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_iniData.SetDoubleValue("Main", "FloorHeightOffset", m_configMain.FloorHeightOffset);
	m_iniData.SetDoubleValue("Main", "FieldOfViewScale", m_configMain.FieldOfViewScale);
	m_iniData.SetDoubleValue("Main", "DepthOffsetCalibration", m_configMain.DepthOffsetCalibration);

	m_iniData.SetDoubleValue("Main", "Brightness", m_configMain.Brightness);
	m_iniData.SetDoubleValue("Main", "Contrast", m_configMain.Contrast);
	m_iniData.SetDoubleValue("Main", "Saturation", m_configMain.Saturation);
	m_iniData.SetDoubleValue("Main", "Sharpness", m_configMain.Sharpness);

	m_iniData.SetBoolValue("Main", "EnableTemporalFiltering", m_configMain.EnableTemporalFiltering);
	m_iniData.SetLongValue("Main", "TemporalFilteringSampling", m_configMain.TemporalFilteringSampling);

	m_iniData.SetBoolValue("Main", "RequireSteamVRRuntime", m_configMain.RequireSteamVRRuntime);
	m_iniData.SetBoolValue("Main", "ShowSettingDescriptions", m_configMain.ShowSettingDescriptions);
	m_iniData.SetBoolValue("Main", "UseLegacyD3D12Renderer", m_configMain.UseLegacyD3D12Renderer);

	m_iniData.SetLongValue("Main", "StereoPreset", m_configMain.StereoPreset);
}

void ConfigManager::UpdateConfig_Core()
{
	m_iniData.SetBoolValue("Core", "CorePassthroughEnable", m_configCore.CorePassthroughEnable);
	m_iniData.SetBoolValue("Core", "CoreAlphaBlend", m_configCore.CoreAlphaBlend);
	m_iniData.SetBoolValue("Core", "CoreAdditive", m_configCore.CoreAdditive);
	m_iniData.SetLongValue("Core", "CorePreferredMode", m_configCore.CorePreferredMode);

	m_iniData.SetBoolValue("Core", "CoreForcePassthrough", m_configCore.CoreForcePassthrough);
	m_iniData.SetLongValue("Core", "CoreForceMode", m_configCore.CoreForceMode);
	m_iniData.SetDoubleValue("Core", "CoreForceMaskedFractionChroma", m_configCore.CoreForceMaskedFractionChroma);
	m_iniData.SetDoubleValue("Core", "CoreForceMaskedFractionLuma", m_configCore.CoreForceMaskedFractionLuma);
	m_iniData.SetDoubleValue("Core", "CoreForceMaskedSmoothing", m_configCore.CoreForceMaskedSmoothing);

	m_iniData.SetDoubleValue("Core", "CoreForceMaskedKeyColorR", m_configCore.CoreForceMaskedKeyColor[0]);
	m_iniData.SetDoubleValue("Core", "CoreForceMaskedKeyColorG", m_configCore.CoreForceMaskedKeyColor[1]);
	m_iniData.SetDoubleValue("Core", "CoreForceMaskedKeyColorB", m_configCore.CoreForceMaskedKeyColor[2]);

	m_iniData.SetBoolValue("Core", "CoreForceMaskedUseCameraImage", m_configCore.CoreForceMaskedUseCameraImage);
	m_iniData.SetBoolValue("Core", "CoreForceMaskedInvertMask", m_configCore.CoreForceMaskedInvertMask);
}

void ConfigManager::UpdateConfig_Extensions()
{
	m_iniData.SetBoolValue("Extensions", "ExtVarjoDepthEstimation", m_configExtensions.ExtVarjoDepthEstimation);
}

void ConfigManager::UpdateConfig_Stereo()
{
	m_iniData.SetBoolValue("StereoCustom", "StereoUseMulticore", m_configCustomStereo.StereoUseMulticore);
	m_iniData.SetBoolValue("StereoCustom", "StereoRectificationFiltering", m_configCustomStereo.StereoRectificationFiltering);
	m_iniData.SetBoolValue("StereoCustom", "StereoUseColor", m_configCustomStereo.StereoUseColor);
	m_iniData.SetBoolValue("StereoCustom", "StereoUseBWInputAlpha", m_configCustomStereo.StereoUseBWInputAlpha);
	m_iniData.SetBoolValue("StereoCustom", "StereoUseHexagonGridMesh", m_configCustomStereo.StereoUseHexagonGridMesh);
	m_iniData.SetBoolValue("StereoCustom", "StereoFillHoles", m_configCustomStereo.StereoFillHoles);
	m_iniData.SetLongValue("StereoCustom", "StereoFrameSkip", m_configCustomStereo.StereoFrameSkip);
	m_iniData.SetLongValue("StereoCustom", "StereoDownscaleFactor", m_configCustomStereo.StereoDownscaleFactor);
	m_iniData.SetBoolValue("StereoCustom", "StereoUseDisparityTemporalFiltering", m_configCustomStereo.StereoUseDisparityTemporalFiltering);
	m_iniData.SetDoubleValue("StereoCustom", "StereoDisparityTemporalFilteringStrength", m_configCustomStereo.StereoDisparityTemporalFilteringStrength);
	m_iniData.SetDoubleValue("StereoCustom", "StereoDisparityTemporalFilteringDistance", m_configCustomStereo.StereoDisparityTemporalFilteringDistance);

	m_iniData.SetBoolValue("StereoCustom", "StereoDisparityBothEyes", m_configCustomStereo.StereoDisparityBothEyes);
	m_iniData.SetBoolValue("StereoCustom", "StereoCutoutEnabled", m_configCustomStereo.StereoCutoutEnabled);
	m_iniData.SetDoubleValue("StereoCustom", "StereoCutoutFactor", m_configCustomStereo.StereoCutoutFactor);
	m_iniData.SetDoubleValue("StereoCustom", "StereoCutoutOffset", m_configCustomStereo.StereoCutoutOffset);
	m_iniData.SetLongValue("StereoCustom", "StereoDisparityFilterWidth", m_configCustomStereo.StereoDisparityFilterWidth);
	m_iniData.SetDoubleValue("StereoCustom", "StereoCutoutFilterWidth", m_configCustomStereo.StereoCutoutFilterWidth);

	m_iniData.SetLongValue("StereoCustom", "StereoBlockSize", m_configCustomStereo.StereoBlockSize);
	m_iniData.SetLongValue("StereoCustom", "StereoMinDisparity", m_configCustomStereo.StereoMinDisparity);
	m_iniData.SetLongValue("StereoCustom", "StereoMaxDisparity", m_configCustomStereo.StereoMaxDisparity);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_Mode", m_configCustomStereo.StereoSGBM_Mode);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_P1", m_configCustomStereo.StereoSGBM_P1);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_P2", m_configCustomStereo.StereoSGBM_P2);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_DispMaxDiff", m_configCustomStereo.StereoSGBM_DispMaxDiff);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_PreFilterCap", m_configCustomStereo.StereoSGBM_PreFilterCap);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_UniquenessRatio", m_configCustomStereo.StereoSGBM_UniquenessRatio);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_SpeckleWindowSize", m_configCustomStereo.StereoSGBM_SpeckleWindowSize);
	m_iniData.SetLongValue("StereoCustom", "StereoSGBM_SpeckleRange", m_configCustomStereo.StereoSGBM_SpeckleRange);

	m_iniData.SetLongValue("StereoCustom", "StereoFiltering", m_configCustomStereo.StereoFiltering);
	m_iniData.SetDoubleValue("StereoCustom", "StereoWLS_Lambda", m_configCustomStereo.StereoWLS_Lambda);
	m_iniData.SetDoubleValue("StereoCustom", "StereoWLS_Sigma", m_configCustomStereo.StereoWLS_Sigma);
	m_iniData.SetDoubleValue("StereoCustom", "StereoWLS_ConfidenceRadius", m_configCustomStereo.StereoWLS_ConfidenceRadius);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Spatial", m_configCustomStereo.StereoFBS_Spatial);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Luma", m_configCustomStereo.StereoFBS_Luma);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Chroma", m_configCustomStereo.StereoFBS_Chroma);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Lambda", m_configCustomStereo.StereoFBS_Lambda);
	m_iniData.SetLongValue("StereoCustom", "StereoFBS_Iterations", m_configCustomStereo.StereoFBS_Iterations);
}

void ConfigManager::UpdateConfig_Depth()
{
	m_iniData.SetBoolValue("Depth", "DepthReadFromApplication", m_configDepth.DepthReadFromApplication);
	m_iniData.SetBoolValue("Depth", "DepthWriteOutput", m_configDepth.DepthWriteOutput);
	m_iniData.SetBoolValue("Depth", "DepthForceComposition", m_configDepth.DepthForceComposition);
}