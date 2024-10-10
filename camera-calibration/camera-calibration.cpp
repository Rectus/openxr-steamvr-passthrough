

#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <d3d11.h>
#include <unknwn.h>
#include <wrl.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <string>
#include <format>
#include <vector>
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <mfidl.h>
#include <mfapi.h>
#include <mfreadwrite.h>
#include <strsafe.h>
#include <opencv2/videoio.hpp>

#include "camera-calibration.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;


enum EStereoFrameLayout
{
    Mono = 0,
    StereoVerticalLayout = 1,
    StereoHorizontalLayout = 2
};


#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 800

static ComPtr<ID3D11Device> g_pd3dDevice = NULL;
static ComPtr<ID3D11DeviceContext> g_pd3dDeviceContext = NULL;
static ComPtr<IDXGISwapChain> g_pSwapChain = NULL;
static ComPtr<ID3D11RenderTargetView> g_mainRenderTargetView = NULL;
static ComPtr<ID3D11Texture2D> g_cameraFrameUploadTexture = NULL;
static ComPtr<ID3D11Texture2D> g_cameraFrameTexture = NULL;
static ComPtr<ID3D11ShaderResourceView> g_cameraFrameSRV = NULL;

static uint32_t g_frameWidth = 0;
static uint32_t g_frameHeight = 0;
static uint32_t g_frameRate = 0;
static EStereoFrameLayout g_frameLayout = Mono;
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;
static bool g_bSwapChainOccluded = false;
static cv::VideoCapture g_videoCapture = cv::VideoCapture();
static cv::Mat g_cameraFrameBuffer;

static bool g_bRequestCustomFrameFormat = false;
static int g_requestedFrameSize[2] = { 0 };
static int g_requestedFrameRate = 0;

static int g_checkerboardSquares[2] = { 8, 5 };
static float g_checkerboardSquareSize = 3.0f;
static int g_numCalibrationFrames = 10;
static bool g_bAutomaticCapture = true;
static float g_automaticCaptureInterval = 5.0;


void EnumerateCameras(std::vector<std::string>& deviceList);
bool InitCamera(int deviceIndex);
void CaptureFrame();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
bool SetupCameraFrameResource(uint32_t width, uint32_t height);
LRESULT WINAPI WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    // Prevent long startup times on cams with many modes when using MSMF
    _putenv_s("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0");

    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, L"Passthrough Camera Calibration", NULL };
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PASSTHROUGH_ICON));

    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Webcam Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

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

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice.Get(), g_pd3dDeviceContext.Get());

    int32_t selectedDevice = -1;
    bool bIsCameraActive = false;
    bool bIsCapturing = false;
    int framesRemaining = 0;
    float timeRemaining = 0.0f;
    LARGE_INTEGER lastTickTime;
    LARGE_INTEGER prefFreq;

    QueryPerformanceFrequency(&prefFreq);

    std::vector<std::string> deviceList;
    EnumerateCameras(deviceList);

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

        if (g_bSwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_bSwapChainOccluded = false;

        if (g_resizeWidth != 0 && g_resizeHeight != 0)
        {
            g_mainRenderTargetView.Reset();
            g_pSwapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = 0;
            g_resizeHeight = 0;
            CreateRenderTarget();
        }


        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        //style.WindowPadding = ImVec2(20.0f, 20.0f);
        //style.FramePadding = ImVec2(16.0f, 10.0f);
        //ImVec2 tabButtonSize(170, 35);

        ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::BeginChild("Menu", ImVec2(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 450.0f), 0));

        

        std::string comboPreview = "No device";
        int32_t prevSelected = selectedDevice;

        if (deviceList.size() > selectedDevice)
        {
            comboPreview.assign(std::format("[{}] {}", selectedDevice, deviceList[selectedDevice]));
        }

        if (ImGui::BeginCombo("Camera", comboPreview.c_str()))
        {
            for (uint32_t i = 0; i < deviceList.size(); i++)
            {
                std::string comboValue = std::format("[{}] {}", i, deviceList[i]);

                const bool bIsSelected = (selectedDevice == i);
                if (ImGui::Selectable(comboValue.c_str(), bIsSelected))
                {
                    selectedDevice = i;
                }

                if (bIsSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (deviceList.size() > 0 && (prevSelected != selectedDevice || !bIsCameraActive))
        {
            bIsCameraActive = InitCamera(selectedDevice);
        }

        ImGui::Text("%u x %u @ %uHz", g_frameWidth, g_frameHeight, g_frameRate);

        ImGui::Checkbox("Use custom frame format", &g_bRequestCustomFrameFormat);

        ImGui::InputInt2("Width x Height", g_requestedFrameSize);
        ImGui::InputInt("Frame Rate", &g_requestedFrameRate);
        ImGui::BeginDisabled(!bIsCameraActive);
        if (ImGui::Button("Apply"))
        {
            bIsCameraActive = InitCamera(selectedDevice);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();

        ImGui::Text("Camera frame layout");
        if (ImGui::RadioButton("Monocular", g_frameLayout == Mono))
        {
            g_frameLayout = Mono;
        }

        if (ImGui::RadioButton("Stereo Vertical", g_frameLayout == StereoVerticalLayout))
        {
            g_frameLayout = StereoVerticalLayout;
        }

        if (ImGui::RadioButton("Stereo Horizontal", g_frameLayout == StereoHorizontalLayout))
        {
            g_frameLayout = StereoHorizontalLayout;
        }

        ImGui::Spacing();

        ImGui::Text("Checkerboard calibration target");
        ImGui::InputInt2("Number of squares", g_checkerboardSquares);
        ImGui::InputFloat("Square side (cm)", &g_checkerboardSquareSize);

        ImGui::Spacing();

        ImGui::Text("Calibration");
        ImGui::InputInt("Frame count", &g_numCalibrationFrames);
        ImGui::Checkbox("Enable automatic capture", &g_bAutomaticCapture);
        ImGui::InputFloat("Capture interval", &g_automaticCaptureInterval);

        ImGui::Spacing();

        ImGui::BeginDisabled(!bIsCameraActive || bIsCapturing);
        if (ImGui::Button("Start capture"))
        {
            bIsCapturing = true;
            framesRemaining = g_numCalibrationFrames;
            timeRemaining = g_automaticCaptureInterval;
            QueryPerformanceCounter(&lastTickTime);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!bIsCapturing);
        if (ImGui::Button("Stop capture"))
        {
            bIsCapturing = false;
        }
        ImGui::EndDisabled();

        if (bIsCapturing)
        {
            if (g_bAutomaticCapture)
            {
                LARGE_INTEGER tickTime;
                QueryPerformanceCounter(&tickTime);

                float deltaTime = (float)(tickTime.QuadPart - lastTickTime.QuadPart);
                //deltaTime *= 1000.0f;
                deltaTime /= prefFreq.QuadPart;

                lastTickTime = tickTime;

                timeRemaining -= deltaTime;

                if (timeRemaining <= 0.0f)
                {

                    if (--framesRemaining <= 0)
                    {
                        bIsCapturing = false;
                    }

                    timeRemaining = g_automaticCaptureInterval;
                }

                ImGui::Text("%.1f", timeRemaining);
            }

            ImGui::Text("Remaining: %d", framesRemaining);
        }


        ImGui::EndChild();

        ImGui::SameLine();


        ImGui::BeginChild("CameraView");

        if (bIsCameraActive)
        {
            CaptureFrame();

            float aspect = (float)g_frameHeight / (float)g_frameWidth;
            float width = std::min(ImGui::GetContentRegionAvail().x, (float)g_frameWidth);
            ImVec2 imageSize = ImVec2(width, width * aspect);

            ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
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

    if (g_videoCapture.isOpened())
    {
        g_videoCapture.release();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

void EnumerateCameras(std::vector<std::string>& deviceList)
{
    uint32_t deviceCount = 0;
    IMFAttributes* attributes = NULL;
    IMFActivate** devices = NULL;

    deviceList.clear();

    HRESULT result = MFCreateAttributes(&attributes, 1);

    if (SUCCEEDED(result))
    {
        result = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }

    if (SUCCEEDED(result))
    {
        result = MFEnumDeviceSources(attributes, &devices, &deviceCount);
    }

    if (SUCCEEDED(result))
    {
        for (uint32_t i = 0; i < deviceCount; i++)
        {
            WCHAR* displayName = NULL;
            char buffer[128] = { 0 };

            UINT32 nameLength;
            HRESULT result = devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &displayName, &nameLength);

            if (SUCCEEDED(result) && displayName)
            {
                WideCharToMultiByte(CP_UTF8, 0, displayName, nameLength, buffer, 127, NULL, NULL);
                deviceList.push_back(buffer);
            }
            else
            {
                deviceList.push_back("ERROR");
            }
            CoTaskMemFree(displayName);
        }
    }

    for (uint32_t i = 0; i < deviceCount; i++)
    {
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
}


bool InitCamera(int deviceIndex)
{
    if (g_videoCapture.isOpened())
    {
        g_videoCapture.release();
    }

    if (g_bRequestCustomFrameFormat)
    {
        std::vector<int> props = { cv::CAP_PROP_FRAME_WIDTH, g_requestedFrameSize[0], cv::CAP_PROP_FRAME_HEIGHT,  g_requestedFrameSize[1], cv::CAP_PROP_FPS, g_requestedFrameRate };
        g_videoCapture.open(deviceIndex, cv::CAP_ANY, props);
    }
    else
    {
        // Prioritize FPS is no frame paramters set
        std::vector<int> props = { cv::CAP_PROP_FPS, 1000 };
        g_videoCapture.open(deviceIndex, cv::CAP_ANY, props);
    }

    if (!g_videoCapture.isOpened())
    {
        return false;
    }


    //Log("OpenCV Video capture opened using API: %s\n", m_videoCapture.getBackendName());

    /*if (cameraConf.AutoExposureEnable)
    {
        g_videoCapture.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
    }
    else
    {
        g_videoCapture.set(cv::CAP_PROP_EXPOSURE, cameraConf.ExposureValue);
    }*/


    g_frameWidth = (uint32_t)g_videoCapture.get(cv::CAP_PROP_FRAME_WIDTH);
    g_frameHeight = (uint32_t)g_videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT);
    g_frameRate = (uint32_t)g_videoCapture.get(cv::CAP_PROP_FPS);


    if (!SetupCameraFrameResource(g_frameWidth, g_frameHeight))
    {
        return false;
    }

    return true;
}


void CaptureFrame()
{
    if (!g_videoCapture.isOpened())
    {
        return;
    }

    if (g_videoCapture.read(g_cameraFrameBuffer))
    {
        D3D11_MAPPED_SUBRESOURCE res = {};
        g_pd3dDeviceContext->Map(g_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);

        cv::Mat uploadBuffer = cv::Mat(g_cameraFrameBuffer.rows, g_cameraFrameBuffer.cols, CV_8UC4, res.pData);

        int from_to[] = { 0,0, 1,1, 2,2, -1,3 };
        cv::mixChannels(&g_cameraFrameBuffer, 1, &uploadBuffer, 1, from_to, uploadBuffer.channels());

        g_pd3dDeviceContext->Unmap(g_cameraFrameUploadTexture.Get(), 0);

        g_pd3dDeviceContext->CopyResource(g_cameraFrameTexture.Get(), g_cameraFrameUploadTexture.Get());
    }
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
    g_mainRenderTargetView.Reset();
    g_pSwapChain.Reset();
    g_pd3dDeviceContext.Reset();
    g_pd3dDevice.Reset();
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

bool SetupCameraFrameResource(uint32_t width, uint32_t height)
{
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.ArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.CPUAccessFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = textureDesc.Format;
    srvDesc.Texture2D.MipLevels = 1;


    D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
    uploadTextureDesc.BindFlags = 0;
    uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
    uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(g_pd3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &g_cameraFrameUploadTexture)))
    {
        return false;
    }

    if (FAILED(g_pd3dDevice->CreateTexture2D(&textureDesc, nullptr, &g_cameraFrameTexture)))
    {
        return false;
    }
    if (FAILED(g_pd3dDevice->CreateShaderResourceView(g_cameraFrameTexture.Get(), &srvDesc, &g_cameraFrameSRV)))
    {
        return false;
    }

    return true;
}



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    {
        return true;
    }

    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_resizeWidth = (UINT)LOWORD(lParam);
            g_resizeHeight = (UINT)HIWORD(lParam);
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
    return ::DefWindowProc(hWnd, message, wParam, lParam);
}
