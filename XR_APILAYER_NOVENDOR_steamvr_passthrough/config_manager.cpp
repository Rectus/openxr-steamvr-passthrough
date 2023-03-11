#include "pch.h"
#include "config_manager.h"
#include <log.h>

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;

ConfigManager::ConfigManager(std::wstring configFile)
	: m_configFile(configFile)
	, m_bConfigUpdated(false)
	, m_iniData()
{
	m_iniData.SetUnicode(true);
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
		ParseConfig_Stereo();

		// TODO: Using the custom preset while we don't have presets.
		m_configStereo = m_configCustomStereo;
	}
	m_bConfigUpdated = false;
}

void ConfigManager::UpdateConfigFile()
{
	UpdateConfig_Main();
	UpdateConfig_Core();
	UpdateConfig_Stereo();

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

	// TODO: Using the custom preset while we don't have presets.
	m_configCustomStereo.StereoReconstructionFreeze = m_configStereo.StereoReconstructionFreeze;
	m_configStereo = m_configCustomStereo;
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
	m_configStereo = Config_Stereo();
	m_configCustomStereo = Config_Stereo();
	UpdateConfigFile();
}

void ConfigManager::ParseConfig_Main()
{
	m_configMain.EnablePassthrough = m_iniData.GetBoolValue("Main", "EnablePassthrough", m_configMain.EnablePassthrough);
	m_configMain.ProjectionMode = (EProjectionMode)m_iniData.GetLongValue("Main", "ProjectionMode", m_configMain.ProjectionMode);

	//m_configMain.ShowTestImage = m_iniData.GetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_configMain.PassthroughOpacity = (float)m_iniData.GetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_configMain.ProjectionDistanceFar = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_configMain.FloorHeightOffset = (float)m_iniData.GetDoubleValue("Main", "FloorHeightOffset", m_configMain.FloorHeightOffset);
	m_configMain.FieldOfViewScale = (float)m_iniData.GetDoubleValue("Main", "FieldOfViewScale", m_configMain.FieldOfViewScale);
	m_configMain.DepthOffsetCalibration = (float)m_iniData.GetDoubleValue("Main", "DepthOffsetCalibration", m_configMain.DepthOffsetCalibration);

	m_configMain.Brightness = (float)m_iniData.GetDoubleValue("Main", "Brightness", m_configMain.Brightness);
	m_configMain.Contrast = (float)m_iniData.GetDoubleValue("Main", "Contrast", m_configMain.Contrast);
	m_configMain.Saturation = (float)m_iniData.GetDoubleValue("Main", "Saturation", m_configMain.Saturation);

	m_configMain.RequireSteamVRRuntime = m_iniData.GetBoolValue("Main", "RequireSteamVRRuntime", m_configMain.RequireSteamVRRuntime);
	
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

void ConfigManager::ParseConfig_Stereo()
{
	m_configCustomStereo.StereoUseMulticore = m_iniData.GetBoolValue("StereoCustom", "StereoUseMulticore", m_configCustomStereo.StereoUseMulticore);
	m_configCustomStereo.StereoRectificationFiltering = m_iniData.GetBoolValue("StereoCustom", "StereoRectificationFiltering", m_configCustomStereo.StereoRectificationFiltering);
	m_configCustomStereo.StereoUseColor = m_iniData.GetBoolValue("StereoCustom", "StereoUseColor", m_configCustomStereo.StereoUseColor);

	m_configCustomStereo.StereoFrameSkip = m_iniData.GetLongValue("StereoCustom", "StereoFrameSkip", m_configCustomStereo.StereoFrameSkip);
	m_configCustomStereo.StereoDownscaleFactor = m_iniData.GetLongValue("StereoCustom", "StereoDownscaleFactor", m_configCustomStereo.StereoDownscaleFactor);
	m_configCustomStereo.StereoAlgorithm = (EStereoAlgorithm)m_iniData.GetLongValue("StereoCustom", "StereoAlgorithm", m_configCustomStereo.StereoAlgorithm);
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
	m_configCustomStereo.StereoFBS_Spatial = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Spatial", m_configCustomStereo.StereoFBS_Spatial);
	m_configCustomStereo.StereoFBS_Luma = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Luma", m_configCustomStereo.StereoFBS_Luma);
	m_configCustomStereo.StereoFBS_Chroma = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Chroma", m_configCustomStereo.StereoFBS_Chroma);
	m_configCustomStereo.StereoFBS_Lambda = (float)m_iniData.GetDoubleValue("StereoCustom", "StereoFBS_Lambda", m_configCustomStereo.StereoFBS_Lambda);
	m_configCustomStereo.StereoFBS_Iterations = m_iniData.GetLongValue("StereoCustom", "StereoFBS_Iterations", m_configCustomStereo.StereoFBS_Iterations);
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

	m_iniData.SetBoolValue("Main", "RequireSteamVRRuntime", m_configMain.RequireSteamVRRuntime);
	
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

void ConfigManager::UpdateConfig_Stereo()
{
	m_iniData.SetBoolValue("StereoCustom", "StereoUseMulticore", m_configCustomStereo.StereoUseMulticore);
	m_iniData.SetBoolValue("StereoCustom", "StereoRectificationFiltering", m_configCustomStereo.StereoRectificationFiltering);
	m_iniData.SetBoolValue("StereoCustom", "StereoUseColor", m_configCustomStereo.StereoUseColor);
	m_iniData.SetLongValue("StereoCustom", "StereoFrameSkip", m_configCustomStereo.StereoFrameSkip);
	m_iniData.SetLongValue("StereoCustom", "StereoDownscaleFactor", m_configCustomStereo.StereoDownscaleFactor);
	m_iniData.SetLongValue("StereoCustom", "StereoAlgorithm", m_configCustomStereo.StereoAlgorithm);
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
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Spatial", m_configCustomStereo.StereoFBS_Spatial);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Luma", m_configCustomStereo.StereoFBS_Luma);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Chroma", m_configCustomStereo.StereoFBS_Chroma);
	m_iniData.SetDoubleValue("StereoCustom", "StereoFBS_Lambda", m_configCustomStereo.StereoFBS_Lambda);
	m_iniData.SetLongValue("StereoCustom", "StereoFBS_Iterations", m_configCustomStereo.StereoFBS_Iterations);
}