

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
#define _USE_MATH_DEFINES
#include <math.h>
#define OPENVR_BUILD_STATIC
#include "openvr.h"
#include <opencv2/core/matx.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "camera-calibration.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;



enum EStereoFrameLayout
{
    Mono = 0,
    StereoVerticalLayout = 1,
    StereoHorizontalLayout = 2
};

struct CalibrationData
{
    int ChessboardCornersX = 0;
    int ChessboardCornersY = 0;
    float ChessboardSquareSize = 0;
    std::vector<cv::Mat> Frames;
    std::vector< std::vector<cv::Point3f>> RefPoints;
    std::vector< std::vector<cv::Point2f>> CBPoints;
    std::vector<cv::Mat> TrackedDeviceToWorldRotations;
    std::vector<cv::Mat> TrackedDeviceToWorldTranslations;
    std::vector<bool> ValidFrames;
    int SensorWidth = 0;
    int SensorHeight = 0;
    cv::Rect FrameROI;
    int NumValidFrames = 0;
    int NumTakenFrames = 0;
    double CalibrationRMSError = 0.0;
    bool bFisheyeLens = false;
    cv::Mat CameraIntrinsics = cv::Mat(3, 3, CV_64F, cv::Scalar(0));
    std::vector<double> CameraDistortion = cv::Mat(1, 4, CV_64F, cv::Scalar(0));
    bool bHasIntrinsics = false;
    std::vector<double> ExtrinsicsRotation = std::vector<double>(3, 0.0);
    std::vector<double> ExtrinsicsTranslation = std::vector<double>(3, 0.0);

    void ClearFrames()
    {
        Frames.clear();
        RefPoints.clear();
        CBPoints.clear();
        TrackedDeviceToWorldRotations.clear();
        TrackedDeviceToWorldTranslations.clear();
        ValidFrames.clear();
        NumValidFrames = 0;
    }
};

struct StereoExtrinsicsData
{
    std::vector<double> LeftTorightRotation = std::vector<double>(3, 0.0);
    std::vector<double> LeftTorightTranslation = std::vector<double>(3, 0.0);
    double CalibrationRMSError = 0.0;
};


#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 1200

static ComPtr<ID3D11Device> g_pd3dDevice = NULL;
static ComPtr<ID3D11DeviceContext> g_pd3dDeviceContext = NULL;
static ComPtr<IDXGISwapChain> g_pSwapChain = NULL;
static ComPtr<ID3D11RenderTargetView> g_mainRenderTargetView = NULL;
static ComPtr<ID3D11Texture2D> g_cameraFrameUploadTexture = NULL;
static ComPtr<ID3D11Texture2D> g_cameraFrameTexture = NULL;
static ComPtr<ID3D11ShaderResourceView> g_cameraFrameSRV = NULL;
static ComPtr<ID3D11Texture2D> g_calibrationTargetUploadTexture = NULL;
static ComPtr<ID3D11Texture2D> g_calibrationTargetTexture = NULL;
static ComPtr<ID3D11ShaderResourceView> g_calibrationTargetSRV = NULL;

static uint32_t g_frameWidth = 0;
static uint32_t g_frameHeight = 0;
static uint32_t g_frameRate = 0;
static EStereoFrameLayout g_frameLayout = Mono;
static bool g_bFisheyeLens = false;
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;
static bool g_bSwapChainOccluded = false;
static cv::VideoCapture g_videoCapture = cv::VideoCapture();
static cv::Mat g_cameraFrameBuffer;
static vr::TrackedDevicePose_t g_lastTrackedDevicePoses[vr::k_unMaxTrackedDeviceCount];
static uint32_t g_frameWidthGPU = 0;
static uint32_t g_frameHeightGPU = 0;
bool g_bUseOpenVRExtrinsic = false;
bool g_bOpenVRIntialized = false;
int g_openVRDevice = 0;
bool g_bIsSpaceDown = false;

static bool g_bRequestCustomFrameFormat = false;
static int g_requestedFrameSize[2] = { 0 };
static int g_requestedFrameRate = 60;

static int g_chessboardCorners[2] = { 8, 5 };
static float g_chessboardSquareSize = 3.0f;
static int g_numCalibrationFrames = 20;
static bool g_bAutomaticCapture = false;
static float g_automaticCaptureInterval = 5.0;
static bool g_selectedImageChanged = false;
static int g_displayedImage = 0;


bool ImageCaptureUI(CalibrationData* calibData, CalibrationData* calibDataStereo, bool bCanCapture, bool& bIsCapturing, bool& bImageCaptureConsumed, bool& bCapturingComplete, int& framesRemaining, float& timeRemaining, bool bIsRightCamera, float deltaTime, bool bCaptureStereo);
void SetFrameGeometry(CalibrationData& calibData, bool bIsRightCamera);
void DrawCameraFrame(CalibrationData& calibData, bool bDrawDistorted, bool bDrawChessboardCorners, int imageIndex);
void DrawStereoFrame(CalibrationData& calibDataLeft, CalibrationData& calibDataRight, bool bDrawDistorted, bool bDrawChessboardCorners, int imageIndex);
bool FindFrameCalibrationPatterns(CalibrationData& calibData, bool bRightCamera);
bool FindFrameCalibrationPatternsStereo(CalibrationData& calibDataLeft, CalibrationData& calibDataRight);
bool CalibrateSingleCamera(CalibrationData& calibData, bool bRightCamera);
bool CalibrateStereo(CalibrationData& calibDataLeft, CalibrationData& calibDataRight, StereoExtrinsicsData& stereoData);
void EnumerateCameras(std::vector<std::string>& deviceList);
bool InitCamera(int deviceIndex);
void CaptureFrame();
void UploadFrame(cv::Mat& frameBuffer);
bool CreateCalibrationTarget(int width, int height, int cornersW, int cornersH);
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
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Passthrough Camera Calibration", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

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
    bool bRightCamera = false;
    bool bIsCapturing = false;
    bool bCapturingCompleteLeft = false;
    bool bCapturingCompleteRight = false;
    bool bCalibrationCompleteLeft = false;
    bool bCalibrationCompleteRight = false;
    bool bIsCapturingStereo = false;
    bool bCapturingCompleteStereo = false;
    bool bCalibrationCompleteStereo = false;
    int framesRemaining = 0;
    float timeRemaining = 0.0f;
    LARGE_INTEGER lastTickTime, tickTime;
    LARGE_INTEGER prefFreq;
    float deltaTime = 0.0f;
    CalibrationData calibDataLeft;
    CalibrationData calibDataRight;
    StereoExtrinsicsData stereoData;

    bool bViewSingleframe = false;
    bool bViewUndistorted = false;
    bool bShowCalibrationTarget = false;
    int lastCalibrationTargetWidth = 0;
    int lastCalibrationTargetHeight = 0;
    int lastCalibrationTargetCornersW = 0;
    int lastCalibrationTargetCornersH = 0;
    bool bCalibrationTargetValid = false;


    QueryPerformanceFrequency(&prefFreq);
    QueryPerformanceCounter(&lastTickTime);

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

        QueryPerformanceCounter(&tickTime);
        deltaTime = (float)(tickTime.QuadPart - lastTickTime.QuadPart);
        deltaTime /= prefFreq.QuadPart;
        lastTickTime = tickTime;


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

        ImGui::BeginChild("Menu", ImVec2(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 470.0f), 0), true);

        
        ImGui::Text("Capture Setup");
        ImGui::Spacing();

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
            calibDataLeft = CalibrationData();
            calibDataRight = CalibrationData();
            g_frameLayout = Mono;
            bCalibrationCompleteLeft = false;
            bCalibrationCompleteRight = false;
        }

        ImGui::Text("%u x %u @ %uHz", g_frameWidth, g_frameHeight, g_frameRate);

        bool bPrevUseCustomFormat = g_bRequestCustomFrameFormat;
        ImGui::Checkbox("Use custom frame format", &g_bRequestCustomFrameFormat);
        ImGui::BeginDisabled(!g_bRequestCustomFrameFormat);
        ImGui::InputInt2("Width x Height", g_requestedFrameSize);
        ImGui::InputInt("Frame Rate", &g_requestedFrameRate);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!bIsCameraActive || !g_bRequestCustomFrameFormat);
        if (ImGui::Button("Apply") || (bIsCameraActive && g_bRequestCustomFrameFormat == false && bPrevUseCustomFormat == true))
        {
            bIsCameraActive = InitCamera(selectedDevice);
            calibDataLeft = CalibrationData();
            calibDataRight = CalibrationData();
            g_frameLayout = Mono;
            bCalibrationCompleteLeft = false;
            bCalibrationCompleteRight = false;
        }
        ImGui::EndDisabled();


        if (bIsCameraActive)
        {
            CaptureFrame();
        }


        ImGui::Separator();
        ImGui::Text("Camera Parameters");
        ImGui::Spacing();

        ImGui::Text("Camera frame layout");
        if (ImGui::RadioButton("Monocular", g_frameLayout == Mono))
        {
            g_frameLayout = Mono;
            SetFrameGeometry(calibDataLeft, false);
        }
        ImGui::SameLine();

        if (ImGui::RadioButton("Stereo Vertical", g_frameLayout == StereoVerticalLayout))
        {
            g_frameLayout = StereoVerticalLayout;
            SetFrameGeometry(calibDataLeft, false);
            SetFrameGeometry(calibDataRight, true);
        }
        ImGui::SameLine();

        if (ImGui::RadioButton("Stereo Horizontal", g_frameLayout == StereoHorizontalLayout))
        {
            g_frameLayout = StereoHorizontalLayout;
            SetFrameGeometry(calibDataLeft, false);
            SetFrameGeometry(calibDataRight, true);
        }
        ImGui::Checkbox("Camera has fisheye lens", &g_bFisheyeLens);

        ImGui::Separator();
        ImGui::Text("Extrinsics");
        ImGui::Spacing();

        ImGui::Text("Calibration type");
        if (ImGui::RadioButton("Enter values manually", !g_bUseOpenVRExtrinsic))
        {
            g_bUseOpenVRExtrinsic = false;
        }
        ImGui::SameLine();

        if (ImGui::RadioButton("Calibrate using tracked device", g_bUseOpenVRExtrinsic))
        {
            g_bUseOpenVRExtrinsic = true;
        }

        ImGui::BeginDisabled(!g_bUseOpenVRExtrinsic || g_bOpenVRIntialized);
        if (ImGui::Button("Connect to SteamVR"))
        {
            vr::EVRInitError error;
            vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Background);
            if (error == vr::VRInitError_None)
            {
                g_bOpenVRIntialized = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::InputInt("Tracked device index", &g_openVRDevice);

        if (g_bOpenVRIntialized && !g_lastTrackedDevicePoses[g_openVRDevice].bPoseIsValid)
        {
            ImGui::Text("SteamVR: Idle");
        }
        else if(g_bOpenVRIntialized && g_lastTrackedDevicePoses[g_openVRDevice].bPoseIsValid)
        {
            ImGui::Text("SteamVR: Object is tracking");
        }

        ImGui::Separator();
        ImGui::Text("Chessboard calibration target");
        ImGui::Spacing();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5);
        ImGui::InputInt2("Number of inner corners", g_chessboardCorners);
        if (g_chessboardCorners[0] < 1) { g_chessboardCorners[0] = 1; }
        if (g_chessboardCorners[1] < 1) { g_chessboardCorners[1] = 1; }
        if (g_chessboardCorners[0] == g_chessboardCorners[1]) { g_chessboardCorners[0] += 1; }

        ImGui::InputFloat("Square side (cm)", &g_chessboardSquareSize);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Show calibration target on screen", &bShowCalibrationTarget);
        
        
        ImGui::Separator();
        ImGui::Text("Calibration");
        ImGui::Spacing();
        

        if (ImGui::BeginTabBar("LeftRightSelection", ImGuiTabBarFlags_None))
        {
            ImGui::BeginDisabled(bIsCapturing);
            if (ImGui::BeginTabItem("Left/Single Camera"))
            {
                bRightCamera = false;
                ImGui::EndTabItem();
            }
            if (g_frameLayout != Mono && ImGui::BeginTabItem("Right Camera"))
            {
                bRightCamera = true;
                ImGui::EndTabItem();
            }
            ImGui::EndDisabled();

            CalibrationData& calibData = bRightCamera ? calibDataRight : calibDataLeft;
            bool& bCapturingComplete = bRightCamera ? bCapturingCompleteRight : bCapturingCompleteLeft;
            bool& bCalibrationComplete = bRightCamera ? bCalibrationCompleteRight : bCalibrationCompleteLeft;

            if (ImageCaptureUI(&calibData, nullptr, bIsCameraActive, bIsCapturing, bCalibrationComplete, bCapturingComplete, framesRemaining, timeRemaining, bRightCamera, deltaTime, false))
            {
                FindFrameCalibrationPatterns(calibData, bRightCamera);
                g_displayedImage = 0;
                g_selectedImageChanged = true;
                bViewSingleframe = true;
            }

            if (ImGui::Button("Calibrate"))
            {
                bCapturingComplete = false;

                if (CalibrateSingleCamera(calibData, bRightCamera))
                {
                    bCalibrationComplete = true;
                    bViewUndistorted = true;
                    bViewSingleframe = true;
                }
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::Text("Result");
            ImGui::Spacing();

            if (bCalibrationComplete)
            {
                ImGui::Text("Calibration complete. RMS error: %f", calibData.CalibrationRMSError);
            }
            else
            {
                ImGui::Text("Calibration pending.");
            }

            ImGui::Spacing();

            ImGui::Text("Intrinsics");

            ImGui::InputDouble("Focal length X", &calibData.CameraIntrinsics.at<double>(0, 0));
            ImGui::InputDouble("Focal length Y", &calibData.CameraIntrinsics.at<double>(1, 1));

            ImGui::InputDouble("Center X", &calibData.CameraIntrinsics.at<double>(0, 2));
            ImGui::InputDouble("Center Y", &calibData.CameraIntrinsics.at<double>(1, 2));

            ImGui::InputDouble("Distortion 1", &calibData.CameraDistortion[0]);
            ImGui::InputDouble("Distortion 2", &calibData.CameraDistortion[1]);
            ImGui::InputDouble("Distortion 3", &calibData.CameraDistortion[2]);
            ImGui::InputDouble("Distortion 4", &calibData.CameraDistortion[3]);

            ImGui::InputInt("Sensor Width", &calibData.SensorWidth);
            ImGui::InputInt("Sensor Height", &calibData.SensorHeight);

            ImGui::Text("Extrinsics");

            ImGui::InputDouble("Rotation X", &calibData.ExtrinsicsRotation[0]);
            ImGui::InputDouble("Rotation Y", &calibData.ExtrinsicsRotation[1]);
            ImGui::InputDouble("Rotation Z", &calibData.ExtrinsicsRotation[2]);

            ImGui::InputDouble("Translation X", &calibData.ExtrinsicsTranslation[0]);
            ImGui::InputDouble("Translation Y", &calibData.ExtrinsicsTranslation[1]);
            ImGui::InputDouble("Translation Z", &calibData.ExtrinsicsTranslation[2]);
            
            ImGui::EndTabBar();
        }
        

        if (g_frameLayout != Mono)
        {
            ImGui::Separator();
            ImGui::Text("Stereo Calibration");
            ImGui::Spacing();

            bool bHasStarted = bIsCapturingStereo;

            if (ImageCaptureUI(&calibDataLeft, &calibDataRight, bCalibrationCompleteLeft && bCalibrationCompleteRight, bIsCapturingStereo, bCalibrationCompleteStereo, bCapturingCompleteStereo, framesRemaining, timeRemaining, bRightCamera, deltaTime, true))
            {
                FindFrameCalibrationPatternsStereo(calibDataLeft, calibDataRight);
                g_displayedImage = 0;
                g_selectedImageChanged = true;
                bViewSingleframe = false;
            }

            if (!bHasStarted && bIsCapturingStereo)
            {
                bViewUndistorted = false;
                bViewSingleframe = false;
            }

            if (ImGui::Button("Calibrate"))
            {
                bCalibrationCompleteStereo = false;

                if (CalibrateStereo(calibDataLeft, calibDataRight, stereoData))
                {
                    bCalibrationCompleteStereo = true;
                    bViewUndistorted = true;
                }
            }
            ImGui::EndDisabled();

            if (bCalibrationCompleteStereo)
            {
                ImGui::Text("Calibration complete. RMS error: %f", stereoData.CalibrationRMSError);
            }
            else
            {
                ImGui::Text("Calibration pending.");
            }

            ImGui::Spacing();

            ImGui::Text("Left to Right Transform");

            ImGui::InputDouble("Rotation X", &stereoData.LeftTorightRotation[0]);
            ImGui::InputDouble("Rotation Y", &stereoData.LeftTorightRotation[1]);
            ImGui::InputDouble("Rotation Z", &stereoData.LeftTorightRotation[2]);

            ImGui::InputDouble("Translation X", &stereoData.LeftTorightTranslation[0]);
            ImGui::InputDouble("Translation Y", &stereoData.LeftTorightTranslation[1]);
            ImGui::InputDouble("Translation Z", &stereoData.LeftTorightTranslation[2]);
        }


        ImGui::EndChild();

        ImGui::SameLine();

        if (bShowCalibrationTarget)
        {
            ImGui::BeginChild("CameraView", ImVec2(std::max(ImGui::GetContentRegionAvail().x * 0.25f, 128.0f), 0), true);
        }
        else
        {
            ImGui::BeginChild("CameraView", ImVec2(0, 0), true);
        }

        ImGui::Text("Camera View");
        if (ImGui::RadioButton("Distorted", !bViewUndistorted && !bViewSingleframe))
        {
            bViewUndistorted = false;
            bViewSingleframe = false;
        }

        if (g_frameLayout != Mono)
        {
            if (ImGui::RadioButton("Distorted Single", !bViewUndistorted && bViewSingleframe))
            {
                bViewUndistorted = false;
                bViewSingleframe = true;
            }
            
        }

        ImGui::BeginDisabled(!bCalibrationCompleteLeft || !bCalibrationCompleteRight);
        if (ImGui::RadioButton("Undistorted", bViewUndistorted && !bViewSingleframe))
        {
            bViewUndistorted = true;
            bViewSingleframe = false;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled((!bCalibrationCompleteLeft && !bRightCamera) || (!bCalibrationCompleteRight && bRightCamera));
        if (g_frameLayout != Mono)
        {
            if (ImGui::RadioButton("Undistorted Single", bViewUndistorted && bViewSingleframe))
            {
                bViewUndistorted = true;
                bViewSingleframe = true;
            }
        }
        ImGui::EndDisabled();

        if (bIsCameraActive)
        {
            if (bViewUndistorted && !bViewSingleframe && bCalibrationCompleteLeft && bCalibrationCompleteRight)
            {
                DrawStereoFrame(calibDataLeft, calibDataRight, false, false, 0);
            }
            else if (bViewUndistorted && bCalibrationCompleteLeft && !bRightCamera)
            {
                DrawCameraFrame(calibDataLeft, false, false, 0);
            }
            else if (bViewUndistorted && bCalibrationCompleteRight && bRightCamera)
            {
                DrawCameraFrame(calibDataRight, false, false, 0);
            }
            else if (!bRightCamera && bCapturingCompleteLeft && !bCalibrationCompleteLeft && calibDataLeft.Frames.size() > 0)
            {
                if (g_selectedImageChanged)
                {
                    DrawCameraFrame(calibDataLeft, true, true, g_displayedImage);
                    g_selectedImageChanged = false;
                }
                else
                {
                    float aspect = (float)calibDataLeft.SensorHeight / (float)calibDataLeft.SensorWidth;
                    float width = std::min(ImGui::GetContentRegionAvail().x, (float)calibDataLeft.SensorWidth);
                    ImVec2 imageSize = ImVec2(width, width * aspect);

                    ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
                }              
            }
            else if (bRightCamera && bCapturingCompleteRight && !bCalibrationCompleteRight && calibDataRight.Frames.size() > 0)
            {
                if (g_selectedImageChanged)
                {
                    DrawCameraFrame(calibDataRight, true, true, g_displayedImage);
                    g_selectedImageChanged = false;
                }
                else
                {
                    float aspect = (float)calibDataRight.SensorHeight / (float)calibDataRight.SensorWidth;
                    float width = std::min(ImGui::GetContentRegionAvail().x, (float)calibDataRight.SensorWidth);
                    ImVec2 imageSize = ImVec2(width, width * aspect);

                    ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
                }
            }
            else if (bCapturingCompleteStereo && !bCalibrationCompleteStereo && calibDataLeft.Frames.size() > 0)
            {
                if (g_selectedImageChanged)
                {
                    DrawStereoFrame(calibDataLeft, calibDataRight, true, true, g_displayedImage);
                    g_selectedImageChanged = false;
                }
                else
                {
                    float aspect = (float)g_frameHeight / (float)g_frameWidth;
                    float width = std::min(ImGui::GetContentRegionAvail().x, (float)g_frameWidth);
                    ImVec2 imageSize = ImVec2(width, width * aspect);

                    ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
                }
            }
            else if(bIsCapturing || bViewSingleframe)
            {
                if (bRightCamera)
                {
                    DrawCameraFrame(calibDataRight, true, false, 0);
                }
                else
                {
                    DrawCameraFrame(calibDataLeft, true, false, 0);
                }
            }
            else
            {
                float aspect = (float)g_frameHeight / (float)g_frameWidth;
                float width = std::min(ImGui::GetContentRegionAvail().x, (float)g_frameWidth);
                ImVec2 imageSize = ImVec2(width, width * aspect);

                if (g_frameWidthGPU != g_frameWidth || g_frameHeightGPU != g_frameHeight)
                {
                    SetupCameraFrameResource(g_frameWidth, g_frameHeight);
                }

                UploadFrame(g_cameraFrameBuffer);
                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
        }



        ImGui::EndChild();

        if (bShowCalibrationTarget)
        {
            ImGui::SameLine();

            ImGui::BeginChild("CalibrationTarget");

            ImVec2 size = ImGui::GetContentRegionAvail();
            int width = (int)size.x;
            int height = (int)size.y;

            if (lastCalibrationTargetWidth != width || lastCalibrationTargetHeight != height
                || lastCalibrationTargetCornersW != g_chessboardCorners[0] 
                || lastCalibrationTargetCornersH != g_chessboardCorners[1])
            {
                if (CreateCalibrationTarget(width, height, g_chessboardCorners[0], g_chessboardCorners[1]))
                {
                    lastCalibrationTargetWidth = width;
                    lastCalibrationTargetHeight = height;
                    lastCalibrationTargetCornersW = g_chessboardCorners[0];
                    lastCalibrationTargetCornersH = g_chessboardCorners[1];
                    bCalibrationTargetValid = true;
                }
                else
                {
                    bCalibrationTargetValid = false;
                }
            }

            if (g_calibrationTargetSRV.Get() && bCalibrationTargetValid)
            {
                ImGui::Image((void*)g_calibrationTargetSRV.Get(), ImVec2((float)width, (float)height));
            }

            ImGui::EndChild();
        }
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


bool ImageCaptureUI(CalibrationData* calibData, CalibrationData* calibDataStereo, bool bCanCapture, bool& bIsCapturing, bool& bImageCaptureConsumed, bool& bCapturingComplete, int& framesRemaining, float& timeRemaining, bool bIsRightCamera, float deltaTime, bool bCaptureStereo)
{
    bool bCaptureJustCompleted = false;
    ImGui::InputInt("Frame count", &g_numCalibrationFrames);
    ImGui::Checkbox("Automatic capture", &g_bAutomaticCapture);
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_bAutomaticCapture);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::InputFloat("Interval (s)", &g_automaticCaptureInterval);
    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginDisabled(!bCanCapture || bIsCapturing);
    if (ImGui::Button("Start capture"))
    {
        bIsCapturing = true;
        bCapturingComplete = false;
        bImageCaptureConsumed = false;
        framesRemaining = g_numCalibrationFrames;
        timeRemaining = g_automaticCaptureInterval;
        calibData->ClearFrames();
        calibData->ChessboardCornersX = g_chessboardCorners[0];
        calibData->ChessboardCornersY = g_chessboardCorners[1];
        calibData->ChessboardSquareSize = g_chessboardSquareSize;
        calibData->bFisheyeLens = g_bFisheyeLens;
        SetFrameGeometry(*calibData, bIsRightCamera);

        if (bCaptureStereo)
        {
            calibDataStereo->ClearFrames();
            calibDataStereo->ChessboardCornersX = g_chessboardCorners[0];
            calibDataStereo->ChessboardCornersY = g_chessboardCorners[1];
            calibDataStereo->ChessboardSquareSize = g_chessboardSquareSize;
            calibDataStereo->bFisheyeLens = g_bFisheyeLens;
            SetFrameGeometry(*calibDataStereo, !bIsRightCamera);
        }
        
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!bIsCapturing);
    if (ImGui::Button("Stop capture"))
    {
        bIsCapturing = false;
    }
    ImGui::EndDisabled();

    bool bDoCaptureFrame = false;

    if (bIsCapturing)
    {
        if (g_bAutomaticCapture)
        {
            

            timeRemaining -= deltaTime;

            if (timeRemaining <= 0.0f)
            {
                bDoCaptureFrame = true;

                timeRemaining = g_automaticCaptureInterval;
            }

            ImGui::Text("%.1f", timeRemaining);
        }
        else
        {
            bool keyState = (GetKeyState(' ') & 0x8000) != 0;
            bool bSpacePressed = !g_bIsSpaceDown && keyState;
            g_bIsSpaceDown = keyState;

            if (ImGui::Button("Capture frame") || bSpacePressed)
            {
                bDoCaptureFrame = true;
            }
            ImGui::SameLine();
            ImGui::Text("Or press spacebar");
        }

        ImGui::Text("Remaining: %d", framesRemaining);
    }

    if (bDoCaptureFrame)
    {
        calibData->Frames.push_back(g_cameraFrameBuffer(calibData->FrameROI).clone());

        if (bCaptureStereo)
        {
            calibDataStereo->Frames.push_back(g_cameraFrameBuffer(calibDataStereo->FrameROI).clone());
        }

        if (g_bUseOpenVRExtrinsic && !bCaptureStereo)
        {
            cv::Mat pose = cv::Mat(3, 4, CV_32F, (void*)&g_lastTrackedDevicePoses[g_openVRDevice].mDeviceToAbsoluteTracking.m[0][0]);
            calibData->TrackedDeviceToWorldRotations.push_back(pose(cv::Rect(0, 0, 3, 3)).clone());
            calibData->TrackedDeviceToWorldTranslations.push_back(pose(cv::Rect(3, 0, 1, 3)).clone());
        }

        if (--framesRemaining <= 0)
        {
            bCaptureJustCompleted = true;
            bIsCapturing = false;
            bCapturingComplete = true;
        }
    }


    ImGui::BeginDisabled(!bCapturingComplete || bImageCaptureConsumed);

    if (bCapturingComplete && !bImageCaptureConsumed)
    {
        ImGui::Text("Capture successful, %d/%d frames valid", calibData->NumValidFrames, calibData->NumTakenFrames);

        if (calibData->Frames.size() > 0)
        {
            ImGui::Text("Image: %d / %d", g_displayedImage + 1, calibData->Frames.size());
        }
    }

    if (ImGui::Button(" < ###PrevImage"))
    {
        g_selectedImageChanged = true;
        if (--g_displayedImage < 0)
        {
            g_displayedImage = 0;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reject Image") && g_displayedImage < calibData->Frames.size())
    {
        calibData->Frames.erase(calibData->Frames.begin() + g_displayedImage);
        calibData->RefPoints.erase(calibData->RefPoints.begin() + g_displayedImage);
        calibData->CBPoints.erase(calibData->CBPoints.begin() + g_displayedImage);
        calibData->ValidFrames.erase(calibData->ValidFrames.begin() + g_displayedImage);
        if (g_bUseOpenVRExtrinsic && !bCaptureStereo)
        {
            calibData->TrackedDeviceToWorldRotations.erase(calibData->TrackedDeviceToWorldRotations.begin() + g_displayedImage);
            calibData->TrackedDeviceToWorldTranslations.erase(calibData->TrackedDeviceToWorldTranslations.begin() + g_displayedImage);
        }
        if (bCaptureStereo)
        {
            calibDataStereo->Frames.erase(calibDataStereo->Frames.begin() + g_displayedImage);
            calibDataStereo->RefPoints.erase(calibDataStereo->RefPoints.begin() + g_displayedImage);
            calibDataStereo->CBPoints.erase(calibDataStereo->CBPoints.begin() + g_displayedImage);
            calibDataStereo->ValidFrames.erase(calibDataStereo->ValidFrames.begin() + g_displayedImage);
        }

        if (--g_displayedImage < 0) { g_displayedImage = 0; }
        if (calibData->Frames.size() < 1)
        {
            bCapturingComplete = false;
        }
        g_selectedImageChanged = true;
    }

    ImGui::SameLine();

    if (ImGui::Button(" > ###NextImage"))
    {
        g_selectedImageChanged = true;
        if (++g_displayedImage >= calibData->Frames.size())
        {
            g_displayedImage--;
        }
    }

    return bCaptureJustCompleted;
}




void SetFrameGeometry(CalibrationData& calibData, bool bIsRightCamera)
{
    if (g_frameLayout == Mono)
    {
        calibData.SensorHeight = g_frameHeight;
        calibData.SensorWidth = g_frameWidth;
        calibData.FrameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight);
    }
    else if (g_frameLayout == StereoHorizontalLayout)
    {
        if (bIsRightCamera)
        {
            calibData.FrameROI = cv::Rect(g_frameWidth / 2, 0, g_frameWidth / 2, g_frameHeight);
        }
        else
        {
            calibData.FrameROI = cv::Rect(0, 0, g_frameWidth / 2, g_frameHeight);
        }

        calibData.SensorHeight = g_frameHeight;
        calibData.SensorWidth = g_frameWidth / 2;
    }
    else if (g_frameLayout == StereoVerticalLayout)
    {
        if (bIsRightCamera)
        {
            calibData.FrameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight / 2);
        }
        else
        {
            calibData.FrameROI = cv::Rect(0, g_frameHeight / 2, g_frameWidth, g_frameHeight / 2);
        }

        calibData.SensorHeight = g_frameHeight / 2;
        calibData.SensorWidth = g_frameWidth;
    }
}



void DrawCameraFrame(CalibrationData& calibData, bool bDrawDistorted, bool bDrawChessboardCorners, int imageIndex)
{
    float aspect = (float)calibData.SensorHeight / (float)calibData.SensorWidth;
    float width = std::min(ImGui::GetContentRegionAvail().x, (float)calibData.SensorWidth);
    ImVec2 imageSize = ImVec2(width, width * aspect);

    if (g_frameWidthGPU != calibData.SensorWidth || g_frameHeightGPU != calibData.SensorHeight)
    {
        SetupCameraFrameResource(calibData.SensorWidth, calibData.SensorHeight);
    }

    if (!bDrawDistorted)
    {
        cv::Mat undistorted;
        if (calibData.bFisheyeLens)
        {
            cv::Mat P, map1, map2;
            cv::fisheye::estimateNewCameraMatrixForUndistortRectify(calibData.CameraIntrinsics, calibData.CameraDistortion, g_cameraFrameBuffer(calibData.FrameROI).size(), cv::Matx33d::eye(), P, 1);
            cv::fisheye::initUndistortRectifyMap(calibData.CameraIntrinsics, calibData.CameraDistortion, cv::Matx33d::eye(), P, g_cameraFrameBuffer(calibData.FrameROI).size(), CV_16SC2, map1, map2);
            cv::remap(g_cameraFrameBuffer(calibData.FrameROI), undistorted, map1, map2, cv::INTER_LINEAR);
        }
        else
        {
            cv::undistort(g_cameraFrameBuffer(calibData.FrameROI), undistorted, calibData.CameraIntrinsics, calibData.CameraDistortion);
        }


        /*if (g_bUseOpenVRExtrinsic)
        {
            cv::Mat pose = cv::Mat(3, 4, CV_32F, (void*)&g_lastTrackedDevicePoses[g_openVRDevice].mDeviceToAbsoluteTracking.m[0][0]);
            cv::Mat rot;
            cv::Rodrigues(pose(cv::Rect(0, 0, 3, 3)).t(), rot);
            
            cv::drawFrameAxes(undistorted, calibData.CameraIntrinsics, calibData.CameraDistortion, rot, pose(cv::Rect(3, 0, 1, 3)) * -1.0, 0.5);
        }*/


        UploadFrame(undistorted);

        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }  
    else if (bDrawChessboardCorners)
    {
        cv::Mat overlaidImage = calibData.Frames[imageIndex].clone();
        cv::drawChessboardCorners(overlaidImage, cv::Size(calibData.ChessboardCornersX, calibData.ChessboardCornersY), calibData.CBPoints[imageIndex], calibData.ValidFrames[imageIndex]);


        UploadFrame(overlaidImage);
        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }   
    else
    {
        cv::Mat frame = g_cameraFrameBuffer(calibData.FrameROI);
        UploadFrame(frame);
        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }
}

void DrawStereoFrame(CalibrationData& calibDataLeft, CalibrationData& calibDataRight, bool bDrawDistorted, bool bDrawChessboardCorners, int imageIndex)
{
    float aspect = (float)g_frameHeight / (float)g_frameWidth;
    float width = std::min(ImGui::GetContentRegionAvail().x, (float)g_frameWidth);
    ImVec2 imageSize = ImVec2(width, width * aspect);

    if (g_frameWidthGPU != g_frameWidth || g_frameHeightGPU != g_frameHeight)
    {
        SetupCameraFrameResource(g_frameWidth, g_frameHeight);
    }

    if (!bDrawDistorted)
    {
        cv::Mat undistorted = cv::Mat(g_frameHeight, g_frameWidth, CV_8UC3);
        if (calibDataLeft.bFisheyeLens)
        {
            cv::Mat P, map1, map2;
            cv::fisheye::estimateNewCameraMatrixForUndistortRectify(calibDataLeft.CameraIntrinsics, calibDataLeft.CameraDistortion, g_cameraFrameBuffer(calibDataLeft.FrameROI).size(), cv::Matx33d::eye(), P, 1);
            cv::fisheye::initUndistortRectifyMap(calibDataLeft.CameraIntrinsics, calibDataLeft.CameraDistortion, cv::Matx33d::eye(), P, g_cameraFrameBuffer(calibDataLeft.FrameROI).size(), CV_16SC2, map1, map2);
            cv::remap(g_cameraFrameBuffer(calibDataLeft.FrameROI), undistorted(calibDataLeft.FrameROI), map1, map2, cv::INTER_LINEAR);

            cv::fisheye::estimateNewCameraMatrixForUndistortRectify(calibDataRight.CameraIntrinsics, calibDataRight.CameraDistortion, g_cameraFrameBuffer(calibDataRight.FrameROI).size(), cv::Matx33d::eye(), P, 1);
            cv::fisheye::initUndistortRectifyMap(calibDataRight.CameraIntrinsics, calibDataRight.CameraDistortion, cv::Matx33d::eye(), P, g_cameraFrameBuffer(calibDataRight.FrameROI).size(), CV_16SC2, map1, map2);
            cv::remap(g_cameraFrameBuffer(calibDataRight.FrameROI), undistorted(calibDataRight.FrameROI), map1, map2, cv::INTER_LINEAR);
        }
        else
        {
            cv::undistort(g_cameraFrameBuffer(calibDataLeft.FrameROI), undistorted(calibDataLeft.FrameROI), calibDataLeft.CameraIntrinsics, calibDataLeft.CameraDistortion);

            cv::undistort(g_cameraFrameBuffer(calibDataRight.FrameROI), undistorted(calibDataRight.FrameROI), calibDataRight.CameraIntrinsics, calibDataRight.CameraDistortion);
        }

        UploadFrame(undistorted);

        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }
    else if (bDrawChessboardCorners)
    {
        cv::Mat composite = cv::Mat(g_frameHeight, g_frameWidth, CV_8UC3);

        calibDataLeft.Frames[imageIndex].copyTo(composite(calibDataLeft.FrameROI));
        cv::drawChessboardCorners(composite(calibDataLeft.FrameROI), cv::Size(calibDataLeft.ChessboardCornersX, calibDataLeft.ChessboardCornersY), calibDataLeft.CBPoints[imageIndex], calibDataLeft.ValidFrames[imageIndex]);

        calibDataRight.Frames[imageIndex].copyTo(composite(calibDataRight.FrameROI));
        cv::drawChessboardCorners(composite(calibDataRight.FrameROI), cv::Size(calibDataRight.ChessboardCornersX, calibDataRight.ChessboardCornersY), calibDataRight.CBPoints[imageIndex], calibDataRight.ValidFrames[imageIndex]);


        UploadFrame(composite);
        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }
    else
    {
        cv::Mat frame = g_cameraFrameBuffer;
        UploadFrame(frame);
        ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
    }
}


bool FindFrameCalibrationPatterns(CalibrationData& calibData, bool bRightCamera)
{
    cv::Size checkerDims = cv::Size(calibData.ChessboardCornersX, calibData.ChessboardCornersY);

    cv::Size winSize = cv::Size(11, 11);
    cv::Size zeroZone = cv::Size(-1, -1);
    cv::TermCriteria cbTermCriteria = cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1);
    

    calibData.RefPoints.clear();
    calibData.CBPoints.clear();
    calibData.ValidFrames.clear();
    calibData.NumValidFrames = 0;
    calibData.NumTakenFrames = 0;
    calibData.CalibrationRMSError = 0.0;

    std::vector<cv::Point3f> cbRef;
    for (int i = 0; i < calibData.ChessboardCornersY; i++)
    {
        for (int j = 0; j < calibData.ChessboardCornersX; j++)
        {
            float squareSide = calibData.ChessboardSquareSize / 100.0f; // Size in meters
            cbRef.push_back(cv::Point3f((float)j * squareSide, (float)i * squareSide, 0));
        }
    }

    for (cv::Mat& image : calibData.Frames)
    {
        cv::Mat grayScale;
        cv::cvtColor(image, grayScale, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners;
        if (cv::findChessboardCorners(grayScale, checkerDims, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE || cv::CALIB_CB_FAST_CHECK))
        {
            cv::cornerSubPix(grayScale, corners, winSize, zeroZone, cbTermCriteria);
            calibData.ValidFrames.push_back(true);
            calibData.NumValidFrames++;
        }
        else
        {
            calibData.ValidFrames.push_back(false);
        }

        calibData.RefPoints.push_back(cbRef);
        calibData.CBPoints.push_back(corners);
    }

    calibData.NumTakenFrames = (int)calibData.Frames.size();

    for (int i = (int)calibData.Frames.size() - 1; i >= 0; i--)
    {
        if (!calibData.ValidFrames[i])
        {
            calibData.Frames.erase(calibData.Frames.begin() + i);
            calibData.CBPoints.erase(calibData.CBPoints.begin() + i);
            calibData.RefPoints.erase(calibData.RefPoints.begin() + i);
            calibData.ValidFrames.erase(calibData.ValidFrames.begin() + i);
            if (g_bUseOpenVRExtrinsic)
            {
                calibData.TrackedDeviceToWorldRotations.erase(calibData.TrackedDeviceToWorldRotations.begin() + i);
                calibData.TrackedDeviceToWorldTranslations.erase(calibData.TrackedDeviceToWorldTranslations.begin() + i);
            }
        }
    }

    if (calibData.RefPoints.empty())
    {
        return false;
    }

    return true;
}

bool FindFrameCalibrationPatternsStereo(CalibrationData& calibDataLeft, CalibrationData& calibDataRight)
{
    cv::Size checkerDims = cv::Size(calibDataLeft.ChessboardCornersX, calibDataLeft.ChessboardCornersY);

    cv::Size winSize = cv::Size(11, 11);
    cv::Size zeroZone = cv::Size(-1, -1);
    cv::TermCriteria cbTermCriteria = cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1);


    calibDataLeft.RefPoints.clear();
    calibDataLeft.CBPoints.clear();
    calibDataLeft.ValidFrames.clear();
    calibDataLeft.NumValidFrames = 0;
    calibDataLeft.NumTakenFrames = 0;

    calibDataRight.RefPoints.clear();
    calibDataRight.CBPoints.clear();
    calibDataRight.ValidFrames.clear();
    calibDataRight.NumValidFrames = 0;
    calibDataRight.NumTakenFrames = 0;

    std::vector<cv::Point3f> cbRef;
    for (int i = 0; i < calibDataLeft.ChessboardCornersY; i++)
    {
        for (int j = 0; j < calibDataLeft.ChessboardCornersX; j++)
        {
            float squareSide = calibDataLeft.ChessboardSquareSize / 100.0f; // Size in meters
            cbRef.push_back(cv::Point3f((float)j * squareSide, (float)i * squareSide, 0));
        }
    }

    for (cv::Mat& image : calibDataLeft.Frames)
    {
        cv::Mat grayScale;
        cv::cvtColor(image, grayScale, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners;
        if (cv::findChessboardCorners(grayScale, checkerDims, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE || cv::CALIB_CB_FAST_CHECK))
        {
            cv::cornerSubPix(grayScale, corners, winSize, zeroZone, cbTermCriteria);
            calibDataLeft.ValidFrames.push_back(true);
            calibDataLeft.NumValidFrames++;
        }
        else
        {
            calibDataLeft.ValidFrames.push_back(false);
        }

        calibDataLeft.RefPoints.push_back(cbRef);
        calibDataLeft.CBPoints.push_back(corners);
    }

    for (cv::Mat& image : calibDataRight.Frames)
    {
        cv::Mat grayScale;
        cv::cvtColor(image, grayScale, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners;
        if (cv::findChessboardCorners(grayScale, checkerDims, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE || cv::CALIB_CB_FAST_CHECK))
        {
            cv::cornerSubPix(grayScale, corners, winSize, zeroZone, cbTermCriteria);
            calibDataRight.ValidFrames.push_back(true);
            calibDataRight.NumValidFrames++;
        }
        else
        {
            calibDataRight.ValidFrames.push_back(false);
        }

        calibDataRight.RefPoints.push_back(cbRef);
        calibDataRight.CBPoints.push_back(corners);
    }

    calibDataLeft.NumTakenFrames = (int)calibDataLeft.Frames.size();
    calibDataRight.NumTakenFrames = (int)calibDataRight.Frames.size();

    for (int i = (int)calibDataLeft.Frames.size() - 1; i >= 0; i--)
    {
        if (!calibDataLeft.ValidFrames[i] || !calibDataRight.ValidFrames[i])
        {
            calibDataLeft.Frames.erase(calibDataLeft.Frames.begin() + i);
            calibDataLeft.CBPoints.erase(calibDataLeft.CBPoints.begin() + i);
            calibDataLeft.RefPoints.erase(calibDataLeft.RefPoints.begin() + i);
            calibDataLeft.ValidFrames.erase(calibDataLeft.ValidFrames.begin() + i);
            
            calibDataRight.Frames.erase(calibDataRight.Frames.begin() + i);
            calibDataRight.CBPoints.erase(calibDataRight.CBPoints.begin() + i);
            calibDataRight.RefPoints.erase(calibDataRight.RefPoints.begin() + i);
            calibDataRight.ValidFrames.erase(calibDataRight.ValidFrames.begin() + i);
        }
    }

    if (calibDataLeft.RefPoints.empty())
    {
        return false;
    }

    return true;
}

inline double RadToDeg(double r)
{
    return r * 180.0 / M_PI;
}

inline double DegToRad(double d)
{
    return d * M_PI / 180.0;
}

std::vector<double> RotationToEuler(cv::Mat& R)
{
    double sy = sqrt(R.at<double>(0, 0) * R.at<double>(0, 0) + R.at<double>(1, 0) * R.at<double>(1, 0));

    double x, y, z;
    if (sy > 1e-6)
    {
        x = atan2(R.at<double>(2, 1), R.at<double>(2, 2));
        y = atan2(-R.at<double>(2, 0), sy);
        z = atan2(R.at<double>(1, 0), R.at<double>(0, 0));
    }
    else
    {
        x = atan2(-R.at<double>(1, 2), R.at<double>(1, 1));
        y = atan2(-R.at<double>(2, 0), sy);
        z = 0;
    }

    return { RadToDeg(x), RadToDeg(y), RadToDeg(z) };
}

cv::Mat EulerToRotationMatrix(std::vector<double>& angles)
{
    double rx = DegToRad(angles[0]);
    double ry = DegToRad(angles[1]);
    double rz = DegToRad(angles[2]);

    cv::Mat X = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, cos(rx), -sin(rx), 0, sin(rx), cos(rx));
    cv::Mat Y = (cv::Mat_<double>(3, 3) << cos(ry), 0, sin(ry), 0, 1, 0, -sin(ry), 0, cos(ry));
    cv::Mat Z = (cv::Mat_<double>(3, 3) << cos(rz), -sin(rz), 0, sin(rz), cos(rz), 0, 0, 0, 1);

    return Z * Y * X;
}


bool CalibrateSingleCamera(CalibrationData& calibData, bool bRightCamera)
{ 
    cv::Mat K;
    std::vector<double> D;
    std::vector<cv::Mat> rvecs, tvecs;

    if (calibData.Frames.size() < 1)
    {
        return false;
    }

    if (calibData.bFisheyeLens)
    {
        cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, DBL_EPSILON);
        int flags = cv::fisheye::CALIB_FIX_SKEW | cv::fisheye::CALIB_CHECK_COND;

        // CALIB_USE_INTRINSIC_GUESS always fails on the fisheye model for some reason.
        /*if (calibData.bHasIntrinsics)
        {
            K = calibData.CameraIntrinsics.clone();
            flags |= cv::fisheye::CALIB_USE_INTRINSIC_GUESS;
        }*/
   
        try
        {
            calibData.CalibrationRMSError = cv::fisheye::calibrate(calibData.RefPoints, calibData.CBPoints, cv::Size(calibData.SensorWidth, calibData.SensorHeight), K, D, rvecs, tvecs, flags, calibTermCriteria);
        }
        catch (const cv::Exception& e)
        {
            return false;
        }
    }
    else
    {
        cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, DBL_EPSILON);
        int flags = cv::CALIB_FIX_K3;

        if (calibData.bHasIntrinsics)
        {
            K = calibData.CameraIntrinsics.clone();
            flags |= cv::CALIB_USE_INTRINSIC_GUESS;
        }
 
        try
        {
            calibData.CalibrationRMSError = cv::calibrateCamera(calibData.RefPoints, calibData.CBPoints, cv::Size(calibData.SensorWidth, calibData.SensorHeight), K, D, rvecs, tvecs, flags, calibTermCriteria);
        }
        catch (const cv::Exception& e)
        {
            return false;
        }
    }

    calibData.CameraIntrinsics = K.clone();
    calibData.CameraDistortion.resize(4);
    std::copy(D.begin(), D.end(), calibData.CameraDistortion.begin());
    calibData.bHasIntrinsics = true;

    if (g_bUseOpenVRExtrinsic)
    {
        // Rotate around 180 degrees to match OpenVR angles somehow
        cv::Mat rot = (cv::Mat_<float>(3, 3) << 1, 0, 0, 0, -1, 0, 0, 0, -1);
        for (int i = 0; i < calibData.TrackedDeviceToWorldRotations.size(); i++)
        {
            calibData.TrackedDeviceToWorldRotations[i] = calibData.TrackedDeviceToWorldRotations[i] * rot;
        }

        cv::Mat outRotation, outTranslation;
        cv::calibrateHandEye(calibData.TrackedDeviceToWorldRotations, calibData.TrackedDeviceToWorldTranslations, rvecs, tvecs, outRotation, outTranslation, cv::CALIB_HAND_EYE_TSAI);

        //outRotation = outRotation.t();
        calibData.ExtrinsicsRotation = RotationToEuler(outRotation);
        calibData.ExtrinsicsRotation[0] *= -1.0;

        outTranslation.at<double>(0, 0) = outTranslation.at<double>(0, 0) * -1.0;
        outTranslation.at<double>(1, 0) = outTranslation.at<double>(1, 0) * -1.0;
        calibData.ExtrinsicsTranslation = outTranslation;
    }


    return true;
}


bool CalibrateStereo(CalibrationData& calibDataLeft, CalibrationData& calibDataRight, StereoExtrinsicsData& stereoData)
{
    cv::Matx<double, 3, 3> rotationLeftToRight;
    cv::Vec<double, 3> translationLeftToRight;

    if (calibDataLeft.Frames.size() < 1)
    {
        return false;
    }

    if (calibDataLeft.bFisheyeLens)
    {
        cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, DBL_EPSILON);
        int flags = cv::CALIB_FIX_INTRINSIC;

        try
        {
            stereoData.CalibrationRMSError = cv::fisheye::stereoCalibrate(calibDataLeft.RefPoints, calibDataLeft.CBPoints, calibDataRight.CBPoints, calibDataLeft.CameraIntrinsics, calibDataLeft.CameraDistortion, calibDataRight.CameraIntrinsics, calibDataRight.CameraDistortion, cv::Size(calibDataLeft.SensorWidth, calibDataLeft.SensorHeight), rotationLeftToRight, translationLeftToRight, cv::noArray(), cv::noArray(), flags, calibTermCriteria);
        }
        catch (const cv::Exception& e)
        {
            return false;
        }
    }
    else
    {
        cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, DBL_EPSILON);
        int flags = cv::CALIB_FIX_INTRINSIC;

        try
        {
            stereoData.CalibrationRMSError = cv::stereoCalibrate(calibDataLeft.RefPoints, calibDataLeft.CBPoints, calibDataRight.CBPoints, calibDataLeft.CameraIntrinsics, calibDataLeft.CameraDistortion, calibDataRight.CameraIntrinsics, calibDataRight.CameraDistortion, cv::Size(calibDataLeft.SensorWidth, calibDataLeft.SensorHeight), rotationLeftToRight, translationLeftToRight, cv::noArray(), cv::noArray(), flags, calibTermCriteria);
        }
        catch (const cv::Exception& e)
        {
            return false;
        }
    }

    cv::Vec<double, 3> prevWorldTranslationLeft = cv::Vec<double, 3>(calibDataLeft.ExtrinsicsTranslation[0], calibDataLeft.ExtrinsicsTranslation[1], calibDataLeft.ExtrinsicsTranslation[2]);
    cv::Vec<double, 3> prevWorldTranslationRight = cv::Vec<double, 3>(calibDataRight.ExtrinsicsTranslation[0], calibDataRight.ExtrinsicsTranslation[1], calibDataRight.ExtrinsicsTranslation[2]);

    cv::Mat prevWorldRotationLeft = EulerToRotationMatrix(calibDataLeft.ExtrinsicsRotation);

    cv::Mat worldToPrevL = cv::Mat::eye(4, 4, CV_64F);
    cv::Mat prevLToWorld = cv::Mat::eye(4, 4, CV_64F);
    cv::hconcat(prevWorldRotationLeft, prevWorldTranslationLeft, worldToPrevL(cv::Rect(0, 0, 4, 3)));
    
    cv::invert(worldToPrevL, prevLToWorld, cv::DECOMP_SVD);

    cv::Vec<double, 4> homoCoord = cv::Vec<double, 4>(translationLeftToRight[0], translationLeftToRight[1], translationLeftToRight[2], 1);

    cv::Mat rightTrans = prevLToWorld * homoCoord;

    rightTrans.at<double>(0, 0) = rightTrans.at<double>(0, 0) * -1.0;
    rightTrans.at<double>(1, 0) = rightTrans.at<double>(1, 0) * -1.0;
    rightTrans.at<double>(2, 0) = rightTrans.at<double>(2, 0) * -1.0;
    calibDataRight.ExtrinsicsTranslation = rightTrans(cv::Rect(0,0,1, 3));

    cv::Mat combinedRot = prevWorldRotationLeft * rotationLeftToRight;
    calibDataRight.ExtrinsicsRotation = RotationToEuler(combinedRot);
    //calibDataRight.ExtrinsicsRotation[0] *= -1.0;

    cv::Mat rot = cv::Mat(rotationLeftToRight);
    stereoData.LeftTorightRotation = RotationToEuler(rot);
    cv::Mat trans = cv::Mat(translationLeftToRight);
    stereoData.LeftTorightTranslation = trans;

    return true;
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

    g_videoCapture.read(g_cameraFrameBuffer);

    if (!g_videoCapture.grab())
    {
        return;
    }

    if (g_bOpenVRIntialized)
    {
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, -0.0075f, g_lastTrackedDevicePoses, g_openVRDevice + 1);
    }

    g_videoCapture.retrieve(g_cameraFrameBuffer); 
}

void UploadFrame(cv::Mat& frameBuffer)
{
    if (!frameBuffer.empty() && g_cameraFrameUploadTexture != nullptr && g_cameraFrameTexture != nullptr)
    {
        D3D11_MAPPED_SUBRESOURCE res = {};
        g_pd3dDeviceContext->Map(g_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);

        cv::Mat uploadBuffer = cv::Mat(frameBuffer.rows, frameBuffer.cols, CV_8UC4, res.pData);

        int from_to[] = { 0,0, 1,1, 2,2, -1,3 };
        cv::mixChannels(&frameBuffer, 1, &uploadBuffer, 1, from_to, uploadBuffer.channels());

        g_pd3dDeviceContext->Unmap(g_cameraFrameUploadTexture.Get(), 0);

        g_pd3dDeviceContext->CopyResource(g_cameraFrameTexture.Get(), g_cameraFrameUploadTexture.Get());
    }
}


bool CreateCalibrationTarget(int width, int height, int cornersW, int cornersH)
{
    int border = 20;

    if (width < border * 2 || height < border / 2)
    {
        return false;
    }

    int boardWidth = width - (border * 2);
    int boardHeight = height - (border * 2);

    int squareSize = std::min(boardWidth / (cornersW + 1), boardHeight / (cornersH + 1));

    cv::Mat calibrationTarget(height, width, CV_8UC4, cv::Scalar(255, 255, 255, 255));
    uint8_t color = 0;

    for (int i = 0; i < (cornersH + 1); i++)
    {
        color = ~color;
        for (int j = 0; j < (cornersW + 1); j++)
        {
            cv::Mat ROI = calibrationTarget(cv::Rect(border + j * squareSize, border + i * squareSize, squareSize, squareSize));
            ROI.setTo(cv::Scalar(color, color, color, 1.0));
            color = ~color;
        }
        if (cornersW % 2 == 0)
        {
            color = ~color;
        }
    }

    D3D11_SUBRESOURCE_DATA subresData = {};
    subresData.pSysMem = calibrationTarget.data;
    subresData.SysMemPitch = width * 4;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.ArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.CPUAccessFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = textureDesc.Format;
    srvDesc.Texture2D.MipLevels = 1;

    if (FAILED(g_pd3dDevice->CreateTexture2D(&textureDesc, &subresData, &g_calibrationTargetTexture)))
    {
        return false;
    }
    if (FAILED(g_pd3dDevice->CreateShaderResourceView(g_calibrationTargetTexture.Get(), &srvDesc, &g_calibrationTargetSRV)))
    {
        return false;
    }

    return true;
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
    textureDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
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

    g_frameWidthGPU = width;
    g_frameHeightGPU = height;

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
