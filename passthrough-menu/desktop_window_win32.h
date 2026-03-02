#pragma once

#include "resource.h"
#include "settings_menu.h"

#define MAX_LOADSTRING 100



class DesktopWindowWin32
{
public:
	DesktopWindowWin32(HINSTANCE hInstance, bool bExitOnClose, bool bExitOnNoClients);
	~DesktopWindowWin32();

	static void HandleOldAppInstance(HINSTANCE hInstance, bool bRestoreWindow, bool bPreventExitNoClients, bool bPreventExitOnClose);

	bool InitWindow(bool bStartOpen, int cmdShow);
	void DeinitWindow();
	void SetMenu(std::shared_ptr<SettingsMenu> menu);
	bool HandleMessages(MSG& quitMessage);
	bool IsVisible();
	bool IsWindowRunning() { return m_runWindow; }
	HWND GetWindowHandle() { return m_hSettingsWindow; }
	bool GetWindowDimensions(uint32_t& width, uint32_t& height);
	bool CreateVulkanSurface(VkInstance instance, VkSurfaceKHR& surface);
	void OnClientConnected();
	void OnAllClientsDisconnected(bool bAllowExit);
	void SendQuitMessage();
	void SetIcon(EWindowIcon icon);
	bool LoadDashboardThumbnails(std::vector<std::vector<uint8_t>>& thumbnailData, uint32_t& width, uint32_t& height);

protected:
	bool AddTrayIcon();
	bool ModifyTrayIcon();
	void RemoveTrayIcon();
	void TrayShowMenu();

	static void TimerCallback(HWND hWnd, UINT message, UINT_PTR event, DWORD time);
	void TimerCallbackImpl(HWND hWnd, UINT message, UINT_PTR event, DWORD time);

	static LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	static INT_PTR About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	HINSTANCE m_hInstance = NULL;
	HANDLE m_instanceMutex = NULL;
	HWND m_hSettingsWindow = NULL;
	HACCEL m_hAccelTable = NULL;
	HICON m_iconBase = NULL;
	HICON m_iconPlay = NULL;
	HICON m_iconPause = NULL;
	HICON m_iconOverride = NULL;
	HICON m_currentIcon = NULL;
	EWindowIcon m_iconState = WindowIcon_Base;

	bool m_bIsSettingsWindowShown = false;
	bool m_bIsDoubleClicking = false;
	POINT m_trayCursorPos = { 0 };
	INT_PTR m_trayClickTimer = NULL;
	INT_PTR m_shutdownDelayTimer = NULL;
	WCHAR m_titleString[MAX_LOADSTRING] = { 0 };
	WCHAR m_windowClass[MAX_LOADSTRING] = { 0 };
	bool m_windowInitialized = false;
	bool m_runWindow = true;
	bool m_bExitOnClose = false;
	bool m_bExitOnNoClients = false;
	bool m_bExitChangedByClients = false;
	std::weak_ptr<SettingsMenu> m_settingsMenu;
};

