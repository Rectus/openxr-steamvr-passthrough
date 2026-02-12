#pragma once

#include "resource.h"
#include "settings_menu.h"

#define MAX_LOADSTRING 100

class DesktopWindowWin32
{
public:
	DesktopWindowWin32(HINSTANCE hInstance, bool bExitOnClose);
	~DesktopWindowWin32();

	static void RestoreExistingWindow(HINSTANCE hInstance);

	bool InitWindow(bool bStartOpen, int cmdShow);
	void DeinitWindow();
	void SetMenu(std::shared_ptr<SettingsMenu> menu);
	bool HandleMessages(MSG& quitMessage);
	bool IsVisible();
	bool IsWindowRunning() { return m_runWindow; }
	HWND GetWindowHandle() { return m_hSettingsWindow; }
	bool GetWindowDimensions(uint32_t& width, uint32_t& height);
	bool CreateVulkanSurface(VkInstance instance, VkSurfaceKHR& surface);

protected:
	bool AddTrayIcon();
	bool ModifyTrayIcon();
	void RemoveTrayIcon();
	void TrayShowMenu();

	static void TrayClickTimerCallback(HWND hWnd, UINT message, UINT_PTR event, DWORD time);
	void TrayClickTimerCallbackImpl(HWND hWnd, UINT message, UINT_PTR event, DWORD time);

	static LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	static INT_PTR About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	HINSTANCE m_hInstance = NULL;
	HANDLE m_instanceMutex = NULL;
	HWND m_hSettingsWindow = NULL;
	HACCEL m_hAccelTable = NULL;
	HICON m_iconOn = NULL;
	HICON m_iconOff = NULL;
	bool m_bIsSettingsWindowShown = false;
	bool m_bIsDoubleClicking = false;
	std::atomic_bool m_bIsPainting = false;
	POINT m_trayCursorPos = { 0 };
	INT_PTR m_trayClickTimer = NULL;
	WCHAR m_titleString[MAX_LOADSTRING] = { 0 };
	WCHAR m_windowClass[MAX_LOADSTRING] = { 0 };
	bool m_windowInitialized = false;
	bool m_runWindow = true;
	bool m_bExitOnClose = false;
	std::shared_ptr<SettingsMenu> m_settingsMenu = nullptr;
};

