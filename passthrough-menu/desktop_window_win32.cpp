

#include "pch.h"
#include "desktop_window_win32.h"
#include "dashboard_overlay.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include <dwmapi.h>
#include <Uxtheme.h>
#include <shellapi.h>
#include <shlobj_core.h>

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 700


DesktopWindowWin32::DesktopWindowWin32(HINSTANCE hInstance, bool bExitOnClose, bool bExitOnNoClients)
    : m_hInstance(hInstance)
    , m_bExitOnClose(bExitOnClose)
    , m_bExitOnNoClients(bExitOnNoClients)
{
    m_runWindow = true;
}

DesktopWindowWin32::~DesktopWindowWin32()
{
    DeinitWindow();
}

bool DesktopWindowWin32::InitWindow(bool bStartOpen, int cmdShow)
{
    m_iconOn = LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_PASSTHROUGHMENU));
    //m_iconOff = LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_ICON2_OFF));
    LoadStringW(m_hInstance, IDS_APP_TITLE, m_titleString, MAX_LOADSTRING);
    LoadStringW(m_hInstance, IDC_PASSTHROUGHMENU, m_windowClass, MAX_LOADSTRING);

    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = m_iconOn;
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = m_windowClass;
    wcex.hIconSm = m_iconOn;
    RegisterClassExW(&wcex);

    RECT windowSize;
    windowSize.left = 0;
    windowSize.right = WINDOW_WIDTH;
    windowSize.top = 0;
    windowSize.bottom = WINDOW_HEIGHT;

    AdjustWindowRect(&windowSize, false, WS_OVERLAPPEDWINDOW);
    LONG width = windowSize.right - windowSize.left;
    LONG height = windowSize.bottom - windowSize.top;

    m_hSettingsWindow = CreateWindowW(m_windowClass, m_titleString, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, width, height, nullptr, nullptr, m_hInstance, nullptr);

    if (!m_hSettingsWindow)
    {
        LPSTR messageBuffer;

        DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&messageBuffer, 0, NULL);

        ErrorLog("CreateWindowW failed: %s\n", messageBuffer);

        LocalFree(messageBuffer);

        return false;
    }

    SetWindowLongPtr(m_hSettingsWindow, GWLP_USERDATA, (LONG_PTR)this);


    // Hack for enabling dark mode menus in Win32.
    // Mostly from: https://gist.github.com/rounk-ctrl/b04e5622e30e0d62956870d5c22b7017

    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (uxtheme)
    {
        enum class PreferredAppMode
        {
            Default,
            AllowDark,
            ForceDark,
            ForceLight,
            Max
        };

        using fnShouldAppsUseDarkMode = bool (WINAPI*)(); // ordinal 132
        using fnAllowDarkModeForWindow = bool (WINAPI*)(HWND hWnd, bool allow); // ordinal 133
        using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode); // ordinal 135
        using fnFlushMenuThemes = void (WINAPI*)(); // ordinal 136

        fnShouldAppsUseDarkMode ShouldAppsUseDarkMode = (fnShouldAppsUseDarkMode)GetProcAddress(uxtheme, MAKEINTRESOURCEA(132));
        fnSetPreferredAppMode SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
        fnAllowDarkModeForWindow AllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(uxtheme, MAKEINTRESOURCEA(133));
        fnFlushMenuThemes FlushMenuThemes = (fnFlushMenuThemes)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));

        if (ShouldAppsUseDarkMode)
        {
            SetPreferredAppMode(PreferredAppMode::AllowDark);

            SetWindowTheme(m_hSettingsWindow, L"Explorer", nullptr);
            AllowDarkModeForWindow(m_hSettingsWindow, true);
            SendMessageW(m_hSettingsWindow, WM_THEMECHANGED, 0, 0);
            FlushMenuThemes();

            BOOL value = TRUE;
            DwmSetWindowAttribute(m_hSettingsWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
        }
        FreeLibrary(uxtheme);
    }


    if (!AddTrayIcon())
    {
        return false;
    }

    if (bStartOpen)
    {
        m_bIsSettingsWindowShown = true;
        ShowWindow(m_hSettingsWindow, cmdShow);
        InvalidateRect(m_hSettingsWindow, NULL, NULL);
    }

    HACCEL hAccelTable = LoadAccelerators(m_hInstance, MAKEINTRESOURCE(IDC_PASSTHROUGHMENU));

    m_windowInitialized = true;

    return true;
}

void DesktopWindowWin32::DeinitWindow()
{
    if (m_windowInitialized)
    {
        m_windowInitialized = false;
        DestroyWindow(m_hSettingsWindow);
        RemoveTrayIcon();
        PostQuitMessage(0);
    }
}

void DesktopWindowWin32::SetMenu(std::shared_ptr<SettingsMenu> menu)
{
    m_settingsMenu = menu;
}

void DesktopWindowWin32::HandleOldAppInstance(HINSTANCE hInstance, bool bRestoreWindow, bool bPreventExitNoClients, bool bPreventExitOnClose)
{
    WCHAR titleString[MAX_LOADSTRING];
    LoadStringW(hInstance, IDS_APP_TITLE, titleString, MAX_LOADSTRING);

    HWND window = FindWindow(NULL, titleString);
    if (window != NULL)
    {
        if (bPreventExitNoClients || bPreventExitOnClose)
        {
            int flags = bPreventExitNoClients ? 1 : 0;
            flags |= bPreventExitOnClose ? 2 : 0;
            SendMessage(window, WM_COMMAND, IDM_NEWAPPLAUNCH, flags);
        }

        if (bRestoreWindow)
        {
            SendMessage(window, WM_COMMAND, IDM_TRAYOPEN, 0);
            SetForegroundWindow(window);
            ShowWindow(window, SW_RESTORE);
        }
    }
}

bool DesktopWindowWin32::HandleMessages(MSG& quitMessage)
{
    MSG msg = {};

    bool bHandledMessages = false;

    // Main message loop:
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        bHandledMessages = true;
        if (!TranslateAccelerator(msg.hwnd, m_hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!m_runWindow)
        {
            quitMessage = msg;
            break;
        }
    }

    return bHandledMessages;
}

bool DesktopWindowWin32::IsVisible()
{
    return m_bIsSettingsWindowShown && IsWindowVisible(m_hSettingsWindow) && !IsIconic(m_hSettingsWindow);
}

bool DesktopWindowWin32::GetWindowDimensions(uint32_t& width, uint32_t& height)
{
    RECT dimensions = {};
    if (GetClientRect(m_hSettingsWindow, &dimensions))
    {
        width = dimensions.right - dimensions.left;
        height = dimensions.bottom - dimensions.top;
        return true;
    }
    return false;
}

bool DesktopWindowWin32::CreateVulkanSurface(VkInstance instance, VkSurfaceKHR& surface)
{
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = m_hSettingsWindow;
    createInfo.hinstance = GetModuleHandle(nullptr);
    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

void DesktopWindowWin32::OnClientConnected()
{
    KillTimer(m_hSettingsWindow, m_shutdownDelayTimer);
    m_shutdownDelayTimer = NULL;
}

void DesktopWindowWin32::OnAllClientsDisconnected()
{
    if (m_bExitOnNoClients && !m_bIsSettingsWindowShown)
    {
        m_shutdownDelayTimer = SetTimer(m_hSettingsWindow, IDT_TIMER_EXITDELAY, 1000, DesktopWindowWin32::TimerCallback);
    }

    if (m_bExitChangedByClients)
    {
        m_bExitOnClose = true;
    }
}

void DesktopWindowWin32::SendQuitMessage()
{
    SendMessage(m_hSettingsWindow, WM_MENU_QUIT, 0, 0);
}


#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif


bool DesktopWindowWin32::AddTrayIcon()
{
    NOTIFYICONDATA iconData = {};

    iconData.cbSize = sizeof(NOTIFYICONDATA);
    iconData.hWnd = m_hSettingsWindow;
    iconData.uID = IDI_PASSTHROUGHMENU;
    iconData.uVersion = NOTIFYICON_VERSION;
    iconData.hIcon = m_iconOn;
    LoadString(m_hInstance, IDS_APP_TITLE, iconData.szTip, 128);
    iconData.uCallbackMessage = WM_TRAYMESSAGE;
    iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

    if (!Shell_NotifyIcon(NIM_ADD, &iconData))
    {
        return false;
    }

    return true;
}

bool DesktopWindowWin32::ModifyTrayIcon()
{
    NOTIFYICONDATA iconData = {};

    iconData.cbSize = sizeof(NOTIFYICONDATA);
    iconData.hWnd = m_hSettingsWindow;
    iconData.uID = IDI_PASSTHROUGHMENU;
    iconData.uVersion = NOTIFYICON_VERSION;
    iconData.hIcon = m_iconOn;
    LoadString(m_hInstance, IDS_APP_TITLE, iconData.szTip, 128);
    iconData.uCallbackMessage = WM_TRAYMESSAGE;
    iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

    if (!Shell_NotifyIcon(NIM_MODIFY, &iconData))
    {
        return false;
    }

    return true;
}

void DesktopWindowWin32::RemoveTrayIcon()
{
    NOTIFYICONDATA iconData = {};
    iconData.cbSize = sizeof(NOTIFYICONDATA);
    iconData.hWnd = m_hSettingsWindow;
    iconData.uID = IDI_PASSTHROUGHMENU;

    Shell_NotifyIcon(NIM_DELETE, &iconData);
}

void DesktopWindowWin32::TrayShowMenu()
{
    // Needed to be able to cancel the menu.
    SetForegroundWindow(m_hSettingsWindow);

    HMENU menu = (HMENU)GetSubMenu(LoadMenu(m_hInstance, MAKEINTRESOURCE(IDC_PASSTHROUGHMENU)), 0);
    SetMenuDefaultItem(menu, IDM_TRAYOPEN, false);
    TrackPopupMenu(menu, TPM_LEFTALIGN, m_trayCursorPos.x, m_trayCursorPos.y, 0, m_hSettingsWindow, NULL);
}

void CALLBACK DesktopWindowWin32::TimerCallback(HWND hWnd, UINT message, UINT_PTR event, DWORD time)
{
    DesktopWindowWin32* inst = reinterpret_cast<DesktopWindowWin32*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (inst)
    {
        inst->TimerCallbackImpl(hWnd, message, event, time);
    }
}

void CALLBACK DesktopWindowWin32::TimerCallbackImpl(HWND hWnd, UINT message, UINT_PTR event, DWORD time)
{
    if (event == IDT_TIMER_EXITDELAY)
    {
        KillTimer(m_hSettingsWindow, m_shutdownDelayTimer);
        m_shutdownDelayTimer = NULL;
        if (m_bExitOnNoClients && !m_bIsSettingsWindowShown)
        {
            m_runWindow = false;
        }
    }
    else if (event == IDT_TIMER_TRAYCLICK)
    {
        KillTimer(m_hSettingsWindow, m_trayClickTimer);
        TrayShowMenu();
    }
}


LRESULT CALLBACK DesktopWindowWin32::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    DesktopWindowWin32* inst = reinterpret_cast<DesktopWindowWin32*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (inst)
    {
        return inst->WndProcImpl(hWnd, message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK DesktopWindowWin32::WndProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case SC_KEYMENU:
            return 0; // Disable alt menu
            break;

        case IDM_TRAYOPEN:
            m_bIsSettingsWindowShown = true;
            ShowWindow(m_hSettingsWindow, SW_SHOW);
            InvalidateRect(m_hSettingsWindow, NULL, NULL);
            break;

        case IDM_ABOUT:
            DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case IDM_EXIT:

            m_runWindow = false;
            break;

        case IDM_NEWAPPLAUNCH:
            
            if (lParam & 1)
            {
                m_bExitOnNoClients = false;
            }
            if (lParam & 2)
            {
                m_bExitOnClose = false;
                m_bExitChangedByClients = !(lParam & 1);
            }
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_PAINT:
    case WM_MOVE:
    {
        auto menu = m_settingsMenu.lock();
        if (m_settingsMenu.expired() || !menu->TickMenu())
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }
        

    case WM_CLOSE:
        if (m_bExitOnClose)
        {
            m_runWindow = false;
        }
        else
        {
            m_bIsSettingsWindowShown = false;
            ShowWindow(m_hSettingsWindow, SW_HIDE);
        }

        break;

    case WM_DESTROY:
        m_runWindow = false;
        break;


    case WM_TRAYMESSAGE:
        switch (lParam)
        {
        case WM_LBUTTONDBLCLK:

            KillTimer(m_hSettingsWindow, m_trayClickTimer);
            m_bIsDoubleClicking = true;

            break;

        case WM_LBUTTONDOWN:

            m_trayClickTimer = SetTimer(m_hSettingsWindow, IDT_TIMER_TRAYCLICK, GetDoubleClickTime(), TimerCallback);
            GetCursorPos(&m_trayCursorPos);

            break;

        case WM_LBUTTONUP:

            if (m_bIsDoubleClicking)
            {
                m_bIsDoubleClicking = false;
                ShowWindow(m_hSettingsWindow, m_bIsSettingsWindowShown ? SW_HIDE : SW_SHOW);
                SetForegroundWindow(m_hSettingsWindow);
                m_bIsSettingsWindowShown = !m_bIsSettingsWindowShown;
                if (m_bIsSettingsWindowShown) 
                { 
                    InvalidateRect(m_hSettingsWindow, NULL, NULL); 
                }
            }

            break;

        case WM_RBUTTONUP:

            GetCursorPos(&m_trayCursorPos);
            TrayShowMenu();
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;

    case WM_SETTINGS_UPDATED:

        //DispatchSettingsUpdated(LOWORD(wParam));

        break;

    case WM_MENU_QUIT:

        m_runWindow = false;

        break;

    default:

        if (m_bIsSettingsWindowShown && !m_settingsMenu.expired())
        {
            auto menu = m_settingsMenu.lock();
            LRESULT result = menu->HandleWin32Events(hWnd, message, wParam, lParam);
            if (result != 0)
            {
                return result;
            }
            else
            {
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    return 0;
}

INT_PTR CALLBACK DesktopWindowWin32::About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
