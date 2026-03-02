#pragma once


#include "config_manager.h"
#include "openvr_manager.h"
#include "vulkan_menu_renderer.h"
#include "menu_ipc_server.h"
#include "shared_structs.h"

#include "imgui.h"
#include <openxr/openxr.h>

class DesktopWindowWin32;
class DashboardOverlay;


enum EMenuTab
{
	TabMain,
	TabComposition,
	TabImage,
	TabCamera,
	TabStereo,
	TabDebug
};


class SettingsMenu : public IMenuIPCReader
{
public:
	SettingsMenu(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<DesktopWindowWin32> window, std::shared_ptr<MenuIPCServer> IPCServer, const std::string_view& imguiConfigPath);

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
	bool TreeNodePersistent(const char* label, ImGuiTreeNodeFlags flags = 0);
	bool CollapsingHeaderPersistent(const char* label, ImGuiTreeNodeFlags flags = 0);

	void DrawMenu();
	void DispatchTransientClientUpdate();

	std::shared_ptr<ConfigManager> m_configManager;
	std::unique_ptr<DashboardOverlay> m_dashboardOverlay;
	std::shared_ptr<DesktopWindowWin32> m_window; 
	std::shared_ptr<MenuIPCServer> m_IPCServer;
	std::string m_imguiConfigPath;

	std::unique_ptr<spdlog::logger> m_clientLogger;

	VulkanMenuRenderer m_renderer;

	bool m_bHasWindow = false;
	bool m_bHasOverlay = false;
	EWindowIcon m_currentIcon = WindowIcon_Base;

	LARGE_INTEGER m_lastFrameStart;

	bool m_bMenuIsVisible = false;
	EMenuTab m_activeTab = TabMain;
	int m_activeClient = -1;
	std::vector< std::unique_ptr<ClientData>> m_clientData;
	ClientData m_defaultClientData = {};

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
	bool m_bClientTransientUpdatePending = false;
	std::atomic<bool> m_bIsRendering = false;
	uint32_t m_menuWidth = 0;
	uint32_t m_menuHeight = 0;

	std::map<ImGuiID, bool> m_treeNodeData;
};

