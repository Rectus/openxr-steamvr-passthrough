#pragma once
#include "SimpleIni.h"

struct Config_Main
{
	bool EnablePassthough = true;
	bool ShowTestImage = false;
	float PassthroughOpacity = 1.0f;
	float ProjectionDistanceFar = 5.0f;
	float ProjectionDistanceNear = 1.0f;

	float Brightness = 0.0f;
	float Contrast = 1.0f;
	float Saturation = 1.0f;

	bool RequireSteamVRRuntime = true;
};

// Configuration for core-spec passthough
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


private:
	void UpdateConfigFile();

	void ParseConfig_Main();
	void ParseConfig_Core();
	void UpdateConfig_Main();
	void UpdateConfig_Core();

	std::wstring m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated;

	Config_Main m_configMain;
	Config_Core m_configCore;
};

