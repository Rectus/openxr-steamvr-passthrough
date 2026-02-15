#pragma once


#include "config_manager.h"
#include "openvr_manager.h"
#include "vulkan_menu_renderer.h"
#include "menu_ipc_server.h"

#include "imgui.h"
#include <openxr/openxr.h>

class DesktopWindowWin32;
class DashboardOverlay;


enum EMenuTab
{
	TabMain,
	TabApplication,
	TabStereo,
	TabOverrides,
	TabCamera,
	TabDebug
};

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

	MenuDisplayValues() {}
};


class SettingsMenu : public IMenuIPCReader
{
public:
	SettingsMenu(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<DashboardOverlay> dashboardOverlay, std::shared_ptr<DesktopWindowWin32> window, std::shared_ptr<MenuIPCServer> IPCServer);

	~SettingsMenu();

	bool InitMenu();

	bool TickMenu();
	
	LRESULT HandleWin32Events(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	virtual void MenuIPCClientConnected(int clientIndex) override;
	virtual void MenuIPCClientDisconnected(int clientIndex) override;
	virtual void MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex) override;

private:
	void TextDescription(const char* fmt, ...);
	void TextDescriptionSpaced(const char* fmt, ...);
	void DrawMenu();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<DashboardOverlay> m_dashboardOverlay;
	std::shared_ptr<DesktopWindowWin32> m_window; 
	std::shared_ptr<MenuIPCServer> m_IPCServer;

	VulkanMenuRenderer m_renderer;

	bool m_bHasWindow = false;
	bool m_bHasOverlay = false;

	LARGE_INTEGER m_lastFrameStart;

	bool m_bMenuIsVisible = false;
	EMenuTab m_activeTab = TabMain;
	int m_activeClient = -1;
	std::vector< std::unique_ptr<MenuDisplayValues>> m_displayValues;
	MenuDisplayValues m_defualtDisplayValues = {};

	ImFont* m_mainFont;
	ImFont* m_smallFont;
	ImFont* m_fixedFont;

	std::vector<DeviceDebugProperties> m_deviceDebugProps;
	int m_currentDebugDevice;

	std::vector<DeviceIdentProperties> m_deviceIdentProps;
	int m_currentIdentDevice;

	std::vector<std::string> m_cameraDevices;

	std::mutex m_menuWriteMutex;

	bool m_cameraTabBeenOpened = false;
	bool m_debugTabBeenOpened = false;

	bool m_bIsKeyboardOpen = false;
	bool m_bElementActiveLastFrame = false;
	bool m_bSettingsUpdatedThisSession = false;
	std::atomic<bool> m_bIsRendering = false;
	uint32_t m_menuWidth = 0;
	uint32_t m_menuHeight = 0;
};

