

#include "pch.h"
#include "main.h"
#include "version.h"

#include <shellapi.h>
#include <dwmapi.h>
#include <Uxtheme.h>

#include "pathutil.h"
#include "desktop_window_win32.h"
#include "settings_menu.h"
#include "menu_ipc_server.h"

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/dup_filter_sink.h"
#include "spdlog/sinks/msvc_sink.h"


// Directory under AppData to write config.
#define CONFIG_FILE_DIR "\\OpenXR SteamVR Passthrough"
#define CONFIG_FILE_NAME "\\config.ini"
#define WINDOW_CONFIG_FILE_NAME "\\imgui.ini"
#define LOG_FILE_DIR "\\OpenXR SteamVR Passthrough"
#define LOG_FILE_NAME "\\menu.log"

#define MUTEX_APP_KEY L"Global\\XR_APILAYER_NOVENDOR_steamvr_passthrough_menu"


std::shared_ptr<spdlog::logger> g_logger;
std::shared_ptr<spdlog_imgui_buffer_sink_mt> g_logRingbuffer;


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

    {
        std::shared_ptr<spdlog::sinks::dup_filter_sink_mt> duplicateFilter = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(std::chrono::minutes(10));

        g_logRingbuffer = std::make_shared<spdlog_imgui_buffer_sink_mt>(500);
        g_logRingbuffer->set_pattern("%v");
        duplicateFilter->add_sink(g_logRingbuffer);

        std::string logFilePath = GetLocalAppData() + LOG_FILE_DIR + LOG_FILE_NAME;
        CreateDirectoryPath(GetLocalAppData() + LOG_FILE_DIR);

        duplicateFilter->add_sink(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true));
        duplicateFilter->add_sink(std::make_shared<spdlog::sinks::msvc_sink_mt>());

        g_logger = std::make_shared<spdlog::logger>("menu", duplicateFilter);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        g_logger->flush_on(spdlog::level::err);
        spdlog::flush_every(std::chrono::seconds(10));

        g_logger->info("Starting Passthrough Wenu version {}", VersionString);
        g_logger->info("Logging to {}", logFilePath);
    }


    std::shared_ptr<ConfigManager> configManager;
    bool bIsInitialConfig = false;
    {
        std::string filePath = GetRoamingAppData() + CONFIG_FILE_DIR + CONFIG_FILE_NAME;
        g_logger->info("Reading config file from {}", filePath);
        configManager = std::make_shared<ConfigManager>(filePath, true);
        bIsInitialConfig = configManager->ReadConfigFile(); 
    }


    std::shared_ptr<DesktopWindowWin32> window = std::make_shared<DesktopWindowWin32>(hInstance, bExitOnClose, bOpenedByLayer);

    if (!window->InitWindow(bOpenWindow, nCmdShow))
    {
        return 1;
    }

    std::shared_ptr<MenuIPCServer> IPCServer = std::make_shared<MenuIPCServer>();

    std::shared_ptr<SettingsMenu> menu;
    {
        std::string windowConfigPath = GetRoamingAppData() + CONFIG_FILE_DIR + WINDOW_CONFIG_FILE_NAME;
        menu = std::make_shared<SettingsMenu>(configManager, window, IPCServer, windowConfigPath);
    }

    IPCServer->RegisterReader(menu);

    if (!menu->InitMenu())
    {
        window.reset();
        IPCServer.reset();
        return 1;
    }

    window->SetMenu(menu);

    g_logger->info("Menu started");

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
        
        bool bOverlayDrawn = false;
        if (!window->IsVisible())
        {
            // Menu updated from the mesage handler when the window is open, otherwise from here for the overlay.
            bOverlayDrawn = menu->TickMenu();
        }

        if (!bHandledMessages && !bOverlayDrawn)
        {
            if (window->IsVisible())
            {
                std::this_thread::yield();
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    g_logger->info("Shutting down...");

    menu.reset();

    window->DeinitWindow();
    window.reset();

    IPCServer.reset();
    configManager.reset();

    return (int)quitMessage.wParam;
}
