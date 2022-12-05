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
	}
	m_bConfigUpdated = false;
}

void ConfigManager::UpdateConfigFile()
{
	UpdateConfig_Main();
	UpdateConfig_Core();

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
	UpdateConfigFile();
}

void ConfigManager::ParseConfig_Main()
{
	m_configMain.EnablePassthough = m_iniData.GetBoolValue("Main", "EnablePassthough", m_configMain.EnablePassthough);
	m_configMain.ShowTestImage = m_iniData.GetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_configMain.PassthroughOpacity = (float)m_iniData.GetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_configMain.ProjectionDistanceFar = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_configMain.ProjectionDistanceNear = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceNear", m_configMain.ProjectionDistanceNear);

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
}

void ConfigManager::UpdateConfig_Main()
{
	m_iniData.SetBoolValue("Main", "EnablePassthough", m_configMain.EnablePassthough);
	m_iniData.SetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_iniData.SetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_iniData.SetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_iniData.SetDoubleValue("Main", "ProjectionDistanceNear", m_configMain.ProjectionDistanceNear);

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
}