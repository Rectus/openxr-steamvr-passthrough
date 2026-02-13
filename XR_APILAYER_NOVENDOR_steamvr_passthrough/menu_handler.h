
#pragma once

#include "config_manager.h"
#include "menu_ipc.h"

class MenuIPCClient;

struct MenuDisplayValues
{
	bool bSessionActive = false;
	bool bDepthBlendingActive = false;
	ERenderAPI renderAPI = None;
	ERenderAPI appRenderAPI = None;
	std::string currentApplication;
	int numCompositionLayers = 0;
	bool bDepthLayerSubmitted = false;
	int frameBufferWidth = 0;
	int frameBufferHeight = 0;
	XrCompositionLayerFlags frameBufferFlags = 0;
	int64_t frameBufferFormat = 0;
	int64_t depthBufferFormat = 0;
	float nearZ = 0.0f;
	float farZ = 0.0f;

	float frameToRenderLatencyMS = 0.0f;
	float frameToPhotonsLatencyMS = 0.0f;
	float renderTimeMS = 0.0f;
	float stereoReconstructionTimeMS = 0.0f;
	float frameRetrievalTimeMS = 0.0f;
	uint64_t lastFrameTimestamp = 0;

	bool bCorePassthroughActive = false;
	bool bFBPassthroughActive = false;
	bool bFBPassthroughDepthActive = false;
	int CoreCurrentMode = 0;

	bool bExtInvertedAlphaActive = false;
	bool bAndroidPassthroughStateActive = false;
	bool bFBPassthroughExtensionActive = false;
	bool bVarjoDepthEstimationExtensionActive = false;
	bool bVarjoDepthCompositionExtensionActive = false;

	uint32_t CameraFrameWidth = 0;
	uint32_t CameraFrameHeight = 0;
	float CameraFrameRate = 0.0f;
	std::string CameraAPI;
};

class MenuHandler : public IMenuIPCReader
{
public:

	MenuHandler(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<MenuIPCClient> IPCClient);

	~MenuHandler();

	MenuDisplayValues& GetDisplayValues() { return m_displayValues; }
	void DispatchDisplayValues();
	void DispatchApplicationName();

	virtual void MenuIPCConnectedToServer() override;
	virtual void MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex) override;

private:

	void RunThread();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<MenuIPCClient> m_IPCClient;
	HMODULE m_dllModule;

	std::thread m_menuThread;
	bool m_bRunThread;

	MenuDisplayValues m_displayValues;
	bool m_bHasDisplayValues = false;
	bool m_bHasApplicationName = false;
};

