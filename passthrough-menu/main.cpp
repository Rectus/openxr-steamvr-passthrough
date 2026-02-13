

#include "pch.h"
#include "main.h"

#include <shellapi.h>
#include <shlobj_core.h>
#include <pathcch.h>
#include <dwmapi.h>
#include <Uxtheme.h>
#include <filesystem>

#include "desktop_window_win32.h"
#include "dashboard_overlay.h"
#include "settings_menu.h"
#include "menu_ipc_server.h"




// Directory under AppData to write config.
#define CONFIG_FILE_DIR L"\\OpenXR SteamVR Passthrough\\"
#define CONFIG_FILE_NAME L"config.ini"

#define LOG_FILE_NAME "XR_APILAYER_NOVENDOR_steamvr_passthrough_menu.txt"

#define MUTEX_APP_KEY L"Global\\XR_APILAYER_NOVENDOR_steamvr_passthrough_menu"

namespace steamvr_passthrough 
{
    namespace logging 
    {
        std::ofstream logStream;
    }
}



int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int       nCmdShow)
{

    bool bOpenWindow = true;
    bool bOpenedByLayer = false;
    bool bExitOnClose = true;
    int numArgs;
    LPWSTR* arglist = CommandLineToArgvW(GetCommandLineW(), &numArgs);

    for (int i = 0; i < numArgs; i++)
    {
        if (_wcsnicmp(arglist[i], L"--help", 6) == 0 ||
            _wcsnicmp(arglist[i], L"-h", 2) == 0)
        {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) 
            {
                FILE* stream;
                freopen_s(&stream, "CONOUT$", "w", stdout);
                printf("OpenXR SteamVR Passthrough - Configuration menu\n\n");
                printf("Usage:\n");
                printf("Running the application without any arguments will open the configuration menu.\n");
                printf("-h | --help\t\tPrint this message.\n");
                printf("--minimized\t\tLaunches minimized to notification field.\n");
                printf("--closetotray\t\tStays open in the notification field when the window is closed.\n");
                printf("--fromlayer\t\tUsed internally when launched by the OpenXR API layer.\n");
                printf("\n");
            }
            return 0;
        }   
        if (_wcsnicmp(arglist[i], L"--closetotray", 13) == 0)
        {
            bExitOnClose = false;
        }
        if (_wcsnicmp(arglist[i], L"--minimized", 11) == 0)
        {
            bOpenWindow = false;
        }
        if (_wcsnicmp(arglist[i], L"--fromlayer", 11) == 0)
        {
            bOpenedByLayer = true;
            bOpenWindow = false;
            bExitOnClose = false;
        }
    }

    // Set a mutex to prevent multiple instances of the application.
    HANDLE hInstanceMutex = CreateMutexW(NULL, TRUE, MUTEX_APP_KEY);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        DesktopWindowWin32::HandleOldAppInstance(hInstance, bOpenWindow, !bOpenedByLayer, !bExitOnClose);
        return 1;
    }

    if (!logStream.is_open())
    {
#pragma warning(disable: 4996) // for getenv
        std::string logFile = (std::filesystem::path(getenv("LOCALAPPDATA")) / LOG_FILE_NAME).string();
#pragma warning(default: 4996)
        logStream.open(logFile, std::ios_base::ate);
    }


    std::shared_ptr<ConfigManager> configManager;
    bool bIsInitialConfig = false;

    {
        PWSTR path;
        std::wstring filePath(PATHCCH_MAX_CCH, L'\0');

        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
        lstrcpyW((PWSTR)filePath.c_str(), path);
        CoTaskMemFree(path);

        PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_DIR);
        CreateDirectoryW((PWSTR)filePath.data(), NULL);
        PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_NAME);

        configManager = std::make_shared<ConfigManager>(filePath, true);
        bIsInitialConfig = configManager->ReadConfigFile(); 
    }


    std::shared_ptr<DesktopWindowWin32> window = std::make_shared<DesktopWindowWin32>(hInstance, bExitOnClose, bOpenedByLayer);

    if (!window->InitWindow(bOpenWindow, nCmdShow))
    {
        return 1;
    }

    std::shared_ptr<OpenVRManager> openVRManager = std::make_shared<OpenVRManager>();
    std::shared_ptr<MenuIPCServer> IPCServer = std::make_shared<MenuIPCServer>();

    std::shared_ptr<SettingsMenu> menu = std::make_shared<SettingsMenu>(configManager, openVRManager, window, IPCServer);
    IPCServer->RegisterReader(menu);

    if (!menu->InitMenu())
    {
        window.reset();
        IPCServer.reset();
        return 1;
    }

    window->SetMenu(menu);

    std::shared_ptr<DashboardOverlay> overlay = nullptr;

    if (openVRManager->IsRuntimeIntialized())
    {
        overlay = std::make_shared<DashboardOverlay>(configManager, openVRManager);
    }

    Log("Menu started.\n");

    MSG quitMessage = {};
  

    // Main message loop:
    while (window->IsWindowRunning())
    {
        bool bHandledMessages = false;

        if (window->HandleMessages(quitMessage))
        {
            bHandledMessages = true;
        }

        if (!window->IsWindowRunning()) { break; }
        

        if (!bHandledMessages)
        {
            if (window->IsVisible())
            {
                std::this_thread::yield();
                //std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    Log("Shutting down...\n");

    menu->DeinitMenu();
    menu.reset();

    window->DeinitWindow();
    window.reset();

    IPCServer.reset();
    configManager.reset();

    return (int)quitMessage.wParam;
}
