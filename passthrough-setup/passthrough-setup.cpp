

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <winreg.h>
#include <string>
#include <format>

#include "resource.h"


using Microsoft::WRL::ComPtr;

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 200

#define OPENVR_DLL_NAME L"openvr_api.dll"
#define API_LAYER_DLL_NAME L"XR_APILAYER_NOVENDOR_steamvr_passthrough.dll"
#define API_LAYER_JSON_NAME L"XR_APILAYER_NOVENDOR_steamvr_passthrough.json"
#define OPENXR_API_LAYER_REG_KEY L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit"


static ComPtr<ID3D11Device> g_pd3dDevice = NULL;
static ComPtr<ID3D11DeviceContext> g_pd3dDeviceContext = NULL;
static ComPtr<IDXGISwapChain> g_pSwapChain = NULL;
static ComPtr<ID3D11RenderTargetView> g_mainRenderTargetView = NULL;


bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, L"Passthrough Setup", NULL };
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PASSTHROUGH_ICON));

    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Passthrough API Layer Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice.Get(), g_pd3dDeviceContext.Get());

    bool bUpdateState = true;
    bool bEnableInstallButton = false;
    bool bEnableUninstallButton = false;
    bool bInstallButtonPressed = false;
    bool bUninstallButtonPressed = false;
    bool bRegistryAccessError = false;
    bool bKeyFound = false;
    bool bMultipleKeysFound = false;
    bool bInvalidKeyFound = false;

    std::string registryErrorMessage = "";

    WCHAR currentDir[MAX_PATH] = { 0 };
    WCHAR openVRDLLPath[MAX_PATH] = { 0 };
    WCHAR apiLayerDLLPath[MAX_PATH] = { 0 };
    WCHAR apiLayerJSONPath[MAX_PATH] = { 0 };
    bool bFoundOpenVRDLL = true;
    bool bFoundAPILayerDLL = true;
    bool bFoundAPILayerJSON = true;

    if (GetCurrentDirectoryW(MAX_PATH, currentDir) < 1){ return 1; }
    if (PathCchCombine(openVRDLLPath, MAX_PATH, currentDir, OPENVR_DLL_NAME) != S_OK){ return 1; }
    if (PathCchCombine(apiLayerDLLPath, MAX_PATH, currentDir, API_LAYER_DLL_NAME) != S_OK) { return 1; }
    if (PathCchCombine(apiLayerJSONPath, MAX_PATH, currentDir, API_LAYER_JSON_NAME) != S_OK) { return 1; }


    bool bRun = true;
    while (bRun)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                bRun = false;
            }
        }
        if (!bRun)
        {
            break;
        }

        if (bUpdateState)
        {
            bKeyFound = false;
            bRegistryAccessError = false;
            bMultipleKeysFound = false;
            bInvalidKeyFound = false;
            bEnableInstallButton = false;
            bEnableUninstallButton = false;
        }


        if (bInstallButtonPressed)
        {
            HKEY key;
            LSTATUS ret;
            ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, OPENXR_API_LAYER_REG_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &key, NULL);
            if (ret == ERROR_SUCCESS)
            { 
                DWORD keyIndex = 0;
                int numValues = 0;
                
                while (true)
                {
                    WCHAR keyName[255] = { 0 };
                    DWORD keySize = 255;
                    ret = RegEnumValueW(key, keyIndex++, keyName, &keySize, NULL, NULL, NULL, NULL);

                    if (ret == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    else if (ret != ERROR_SUCCESS)
                    {
                        registryErrorMessage = std::format("Failed to enumerate keys, {0}", ret);
                        bRegistryAccessError = true;
                        break;
                    }
                    else if (StrCmpNIW(apiLayerJSONPath, keyName, MAX_PATH) == 0)
                    {
                        numValues++;
                        bKeyFound = true;

                        if (numValues > 1)
                        {
                            ret = RegDeleteValueW(key, keyName);
                            if (ret != ERROR_SUCCESS)
                            {
                                registryErrorMessage = std::format("Failed to delete key, {0}", ret);
                                bRegistryAccessError = true;
                                break;
                            }

                            keyIndex = 0;
                            numValues = 0;
                        }
                    }
                    else
                    {
                        // Check that this is a duplicate entry is for this API layer and not another layer.
                        int offset = keySize - lstrlenW(API_LAYER_JSON_NAME);
                        if (offset >= 0 && StrCmpNIW(API_LAYER_JSON_NAME, &keyName[offset], offset) == 0)
                        {
                            ret = RegDeleteValueW(key, keyName);
                            if (ret != ERROR_SUCCESS)
                            {
                                registryErrorMessage = std::format("Failed to delete key, {0}", ret);
                                bRegistryAccessError = true;
                                break;
                            }

                            keyIndex = 0;
                            numValues = 0;
                        }
                    }

                }

                if (!bKeyFound)
                {
                    const BYTE val[4] = { 0 };
                    ret = RegSetValueExW(key, apiLayerJSONPath, NULL, REG_DWORD, val, sizeof(val));
                    if (ret != ERROR_SUCCESS)
                    {
                        registryErrorMessage = std::format("Failed to set key, {0}", ret);
                        bRegistryAccessError = true;
                    }
                }

                RegCloseKey(key);
            }
            else
            {
                registryErrorMessage = std::format("Failed to open key, {0}", ret);
                bRegistryAccessError = true;
            }

            bInstallButtonPressed = false;
        }
        else if (bUninstallButtonPressed)
        {
            HKEY key;
            LSTATUS ret;
            ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OPENXR_API_LAYER_REG_KEY, 0, KEY_READ | KEY_WRITE, &key);
            if (ret == ERROR_SUCCESS)
            {
                DWORD keyIndex = 0;
                
                while (true)
                {
                    WCHAR keyName[255] = { 0 };
                    DWORD keySize = 255;

                    ret = RegEnumValueW(key, keyIndex++, keyName, &keySize, NULL, NULL, NULL, NULL);
                    if (ret == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    else if (ret != ERROR_SUCCESS)
                    {
                        registryErrorMessage = std::format("Failed to enumerate keys, {0}", ret);
                        bRegistryAccessError = true;
                        break;
                    }
                    else
                    {
                        // Check that this is an entry is for this API layer and not another layer.
                        int offset = keySize - lstrlenW(API_LAYER_JSON_NAME);
                        if (offset >= 0 && StrCmpNIW(API_LAYER_JSON_NAME, &keyName[offset], offset) == 0)
                        {
                            ret = RegDeleteValueW(key, keyName);
                            if (ret != ERROR_SUCCESS)
                            {
                                registryErrorMessage = std::format("Failed to delete key, {0}", ret);
                                bRegistryAccessError = true;
                                break;
                            }

                            keyIndex = 0;
                        }
                    }

                }

                RegCloseKey(key);
            }
            else
            {
                registryErrorMessage = std::format("Failed to open key, {0}", ret);
                bRegistryAccessError = true;
            }

            bUninstallButtonPressed = false;
        }


        if (bUpdateState)
        {
            bFoundOpenVRDLL = PathFileExistsW(openVRDLLPath);
            bFoundAPILayerDLL = PathFileExistsW(apiLayerDLLPath);
            bFoundAPILayerJSON = PathFileExistsW(apiLayerJSONPath);
            
            HKEY key;
            LSTATUS ret;

            ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OPENXR_API_LAYER_REG_KEY, 0, KEY_READ, &key);
            if (ret == ERROR_SUCCESS)
            { 
                DWORD keyIndex = 0;
                int numValues = 0;
                
                while(true)
                {
                    WCHAR keyName[255] = { 0 };
                    DWORD keySize = 255;

                    ret = RegEnumValueW(key, keyIndex++, keyName, &keySize, NULL, NULL, NULL, NULL);
                    if (ret == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    else if (ret != ERROR_SUCCESS)
                    {
                        registryErrorMessage = std::format("Failed to enumerate keys, {0}", ret);
                        bRegistryAccessError = true;
                        break;
                    }
                    else if (StrCmpNIW(apiLayerJSONPath, keyName, MAX_PATH) == 0)
                    {
                        numValues++;
                        bKeyFound = true;
                        bEnableUninstallButton = true;
                    }
                    else
                    {
                        // Check that this is a duplicate entry is for this API layer and not another layer.
                        int offset = keySize - lstrlenW(API_LAYER_JSON_NAME);
                        if (offset >= 0 && StrCmpNIW(API_LAYER_JSON_NAME, &keyName[offset], offset) == 0)
                        {
                            numValues++;
                            bInvalidKeyFound = true;
                            bEnableInstallButton = true;
                            bEnableUninstallButton = true;
                        }
                    }
                }

                if (numValues > 1)
                {
                    bMultipleKeysFound = true;
                    bEnableInstallButton = true;
                    bEnableUninstallButton = true;
                }
                else if (numValues < 1)
                {
                    bEnableInstallButton = true;
                }

                RegCloseKey(key);
            }
            else if((ret == ERROR_FILE_NOT_FOUND) || (ret == ERROR_PATH_NOT_FOUND))
            {
                bEnableInstallButton = true;
            }
            else
            {         
                registryErrorMessage = std::format("Failed to open key, {0}", ret);
                bRegistryAccessError = true;
            }

            if (!bFoundOpenVRDLL || !bFoundAPILayerDLL || !bFoundAPILayerJSON)
            {
                bEnableInstallButton = false;
            }


            bUpdateState = false;
        }


        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.WindowPadding = ImVec2( 20.0f, 20.0f );
        style.FramePadding = ImVec2(16.0f, 10.0f);

        ImGui::Begin("Passthrough Setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::BeginChild("Buttons", ImVec2(180, 0));
        
        if (!bEnableInstallButton) { ImGui::BeginDisabled(); }
        if (ImGui::Button("Install API Layer"))
        {
            bUpdateState = true;
            bInstallButtonPressed = true;
        }
        if (!bEnableInstallButton) { ImGui::EndDisabled(); }

        if (!bEnableUninstallButton) { ImGui::BeginDisabled(); }
        if (ImGui::Button("Uninstall API Layer"))
        {
            bUpdateState = true;
            bUninstallButtonPressed = true;
        }
        if (!bEnableUninstallButton) { ImGui::EndDisabled(); }

        if (ImGui::Button("Recheck Status"))
        {
            bUpdateState = true;
        }

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Status");

        if(!bFoundOpenVRDLL)
        {
            ImGui::Text("openvr_api.dll missing from current directory!");
        }
        if (!bFoundAPILayerDLL)
        {
            ImGui::Text("XR_APILAYER_NOVENDOR_steamvr_passthrough.dll missing from current directory!");
        }
        if (!bFoundAPILayerJSON)
        {
            ImGui::Text("XR_APILAYER_NOVENDOR_steamvr_passthrough.json missing from current directory!");
        }
        if (bRegistryAccessError)
        {
            ImGui::Text("Error accesing the Windows Registry: %s", registryErrorMessage.c_str());
            ImGui::Text("Please make sure the application is run with administator permissions.");
        }

        ImGui::Text("");

        if ((!bFoundOpenVRDLL || !bFoundAPILayerDLL || !bFoundAPILayerJSON) && (bMultipleKeysFound || bInvalidKeyFound))
        {
            ImGui::Text("Files missing and the install condition is invalid!"); 
            ImGui::Text("Press the uninstall button and redownload the layer.");
        }
        else if (!bFoundOpenVRDLL || !bFoundAPILayerDLL || !bFoundAPILayerJSON)
        {
            ImGui::Text("Files missing! Please redownload the layer.");
        }
        else if (bMultipleKeysFound)
        {
            ImGui::Text("Multiple install locations found! Press the install button to fix.");
        }
        else if (bInvalidKeyFound)
        {
            ImGui::Text("Invalid install location found! Press the install button to fix.");
        }
        else if (bKeyFound)
        {
            ImGui::Text("The API layer is installed.");
        }
        else
        {
            ImGui::Text("The API layer is not installed.");
        }
        
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();

        const float clear_color_with_alpha[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, g_mainRenderTargetView.GetAddressOf(), NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView.Get(), clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}


bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
    {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain.Get()) { g_pSwapChain.Reset(); }
    if (g_pd3dDeviceContext.Get()) { g_pd3dDeviceContext.Reset(); }
    if (g_pd3dDevice.Get()) { g_pd3dDevice.Reset(); }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    }
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView.Get()) { g_mainRenderTargetView->Release(); g_mainRenderTargetView.Reset(); }
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
        {
            return 0;
        }
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
