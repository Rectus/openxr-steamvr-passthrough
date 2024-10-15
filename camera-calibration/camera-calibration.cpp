

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
    std::vector<bool> ValidFrames;
    int NumValidFrames = 0;
    int NumTakenFrames = 0;
    double CalibrationRMSError = 0.0;

    void clear()
    {
        Frames.clear();
        RefPoints.clear();
        CBPoints.clear();
        ValidFrames.clear();
        NumValidFrames = 0;
    }
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
static uint32_t g_frameWidthGPU = 0;
static uint32_t g_frameHeightGPU = 0;

static cv::Mat g_leftCameraIntrinsics = cv::Mat(3, 3, CV_64F);
static std::vector<double> g_leftCameraDistortion = cv::Mat(1, 4, CV_64F);

static cv::Mat g_rightCameraIntrinsics = cv::Mat(3, 3, CV_64F);
static std::vector<double> g_rightCameraDistortion = cv::Mat(1, 4, CV_64F);


static bool g_bRequestCustomFrameFormat = false;
static int g_requestedFrameSize[2] = { 0 };
static int g_requestedFrameRate = 0;

static int g_chessboardCorners[2] = { 8, 5 };
static float g_chessboardSquareSize = 3.0f;
static int g_numCalibrationFrames = 10;
static bool g_bAutomaticCapture = false;
static float g_automaticCaptureInterval = 5.0;


bool FindFrameCalibrationPatterns(CalibrationData& calibData, bool bRightCamera);
bool CalibrateSingleCamera(CalibrationData& calibData, bool bRightCamera, int width, int height);
void EnumerateCameras(std::vector<std::string>& deviceList);
bool InitCamera(int deviceIndex);
void CaptureFrame();
void UploadFrame(cv::Mat& frameBuffer);
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
    bool selectedImageChanged = false;
    int displayedImage = 0;
    bool bCalibrationCompleteLeft = false;
    bool bCalibrationCompleteRight = false;
    int framesRemaining = 0;
    float timeRemaining = 0.0f;
    LARGE_INTEGER lastTickTime;
    LARGE_INTEGER prefFreq;
    CalibrationData calibDataLeft;
    CalibrationData calibDataRight;
    cv::Rect frameROI;
    
    cv::Mat overlaidImage;

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

        ImGui::BeginChild("Menu", ImVec2(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 470.0f), 0));

        
        ImGui::Separator();
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
            frameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight);
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
            frameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight);
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
        }
        ImGui::SameLine();

        if (ImGui::RadioButton("Stereo Vertical", g_frameLayout == StereoVerticalLayout))
        {
            g_frameLayout = StereoVerticalLayout;
        }
        ImGui::SameLine();

        if (ImGui::RadioButton("Stereo Horizontal", g_frameLayout == StereoHorizontalLayout))
        {
            g_frameLayout = StereoHorizontalLayout;
        }
        ImGui::Checkbox("Camera has fisheye lens", &g_bFisheyeLens);

        ImGui::Separator();
        ImGui::Text("Chessboard calibration target");
        ImGui::Spacing();

        
        ImGui::InputInt2("Number of inner corners", g_chessboardCorners);
        ImGui::InputFloat("Square side (cm)", &g_chessboardSquareSize);

        
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

            if (g_frameLayout == Mono)
            {
                frameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight);
            }
            else if (g_frameLayout == StereoHorizontalLayout)
            {
                if (bRightCamera)
                {
                    frameROI = cv::Rect(g_frameWidth / 2, 0, g_frameWidth / 2, g_frameHeight);
                }
                else
                {
                    frameROI = cv::Rect(0, 0, g_frameWidth / 2, g_frameHeight);
                }
            }
            else if (g_frameLayout == StereoVerticalLayout)
            {
                if (bRightCamera)
                {
                    frameROI = cv::Rect(0, 0, g_frameWidth, g_frameHeight / 2);
                }
                else
                {
                    frameROI = cv::Rect(0, g_frameHeight / 2, g_frameWidth, g_frameHeight / 2);
                }
            }
            
            ImGui::InputInt("Frame count", &g_numCalibrationFrames);
            ImGui::Checkbox("Automatic capture", &g_bAutomaticCapture);
            ImGui::SameLine();
            ImGui::BeginDisabled(!g_bAutomaticCapture);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            ImGui::InputFloat("Interval (s)", &g_automaticCaptureInterval);
            ImGui::EndDisabled();

            ImGui::Spacing();

            ImGui::BeginDisabled(!bIsCameraActive || bIsCapturing);
            if (ImGui::Button("Start capture"))
            {
                bIsCapturing = true;
                bCapturingComplete = false;
                bCalibrationComplete = false;
                framesRemaining = g_numCalibrationFrames;
                timeRemaining = g_automaticCaptureInterval;
                calibData.clear();
                calibData.ChessboardCornersX = g_chessboardCorners[0];
                calibData.ChessboardCornersY = g_chessboardCorners[1];
                calibData.ChessboardSquareSize = g_chessboardSquareSize;

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
                    deltaTime /= prefFreq.QuadPart;

                    lastTickTime = tickTime;

                    timeRemaining -= deltaTime;

                    if (timeRemaining <= 0.0f)
                    {
                        calibData.Frames.push_back(g_cameraFrameBuffer(frameROI).clone());

                        if (--framesRemaining <= 0)
                        {
                            FindFrameCalibrationPatterns(calibData, bRightCamera);
                            bIsCapturing = false;
                            bCapturingComplete = true;
                            displayedImage = 0;
                            selectedImageChanged = true;
                        }

                        timeRemaining = g_automaticCaptureInterval;
                    }

                    ImGui::Text("%.1f", timeRemaining);
                }
                else
                {
                    if (ImGui::Button("Capture frame"))
                    {
                        calibData.Frames.push_back(g_cameraFrameBuffer(frameROI).clone());

                        if (--framesRemaining <= 0)
                        {
                            FindFrameCalibrationPatterns(calibData, bRightCamera);
                            bIsCapturing = false;
                            bCapturingComplete = true;
                            displayedImage = 0;
                            selectedImageChanged = true;
                        }
                    }
                }

                ImGui::Text("Remaining: %d", framesRemaining);
            }

            ImGui::BeginDisabled(!bCapturingComplete || bCalibrationComplete);

            if (bCapturingComplete && !bCalibrationComplete)
            {
                ImGui::Text("Capture successful, %d/%d frames valid", calibData.NumValidFrames, calibData.NumTakenFrames);

                if (calibData.Frames.size() > 0)
                {
                    ImGui::Text("Image: %d / %d", displayedImage + 1, calibData.Frames.size());
                }
            }

            if (ImGui::Button(" < ###PrevImage"))
            {
                selectedImageChanged = true;
                if (--displayedImage < 0)
                {            
                    displayedImage = 0;
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Reject Image") && displayedImage < calibData.Frames.size())
            {
                calibData.Frames.erase(calibData.Frames.begin() + displayedImage);
                calibData.RefPoints.erase(calibData.RefPoints.begin() + displayedImage);
                calibData.CBPoints.erase(calibData.CBPoints.begin() + displayedImage);
                calibData.ValidFrames.erase(calibData.ValidFrames.begin() + displayedImage);
                if (--displayedImage < 0) { displayedImage = 0; }
                if (calibData.Frames.size() < 1)
                {
                    bCapturingComplete = false;
                }
                selectedImageChanged = true;
            }

            ImGui::SameLine();

            if (ImGui::Button(" > ###NextImage"))
            {
                selectedImageChanged = true;
                if (++displayedImage >= calibData.Frames.size())
                {           
                    displayedImage--;
                }
            }


            if (ImGui::Button("Calibrate"))
            {
                bCapturingComplete = false;

                if (CalibrateSingleCamera(calibData, bRightCamera, frameROI.width, frameROI.height))
                {
                    bCalibrationComplete = true;
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

            if (bRightCamera)
            {
                ImGui::InputDouble("Focal length X", &g_rightCameraIntrinsics.at<double>(0, 0));
                ImGui::InputDouble("Focal length Y", &g_rightCameraIntrinsics.at<double>(1, 1));

                ImGui::InputDouble("Center X", &g_rightCameraIntrinsics.at<double>(0, 2));
                ImGui::InputDouble("Center Y", &g_rightCameraIntrinsics.at<double>(1, 2));

                ImGui::InputDouble("Distortion 1", &g_rightCameraDistortion[0]);
                ImGui::InputDouble("Distortion 2", &g_rightCameraDistortion[1]);
                ImGui::InputDouble("Distortion 3", &g_rightCameraDistortion[2]);
                ImGui::InputDouble("Distortion 4", &g_rightCameraDistortion[3]);
            }
            else
            {
                ImGui::InputDouble("Focal length X", &g_leftCameraIntrinsics.at<double>(0, 0));
                ImGui::InputDouble("Focal length Y", &g_leftCameraIntrinsics.at<double>(1, 1));

                ImGui::InputDouble("Center X", &g_leftCameraIntrinsics.at<double>(0, 2));
                ImGui::InputDouble("Center Y", &g_leftCameraIntrinsics.at<double>(1, 2));

                ImGui::InputDouble("Distortion 1", &g_leftCameraDistortion[0]);
                ImGui::InputDouble("Distortion 2", &g_leftCameraDistortion[1]);
                ImGui::InputDouble("Distortion 3", &g_leftCameraDistortion[2]);
                ImGui::InputDouble("Distortion 4", &g_leftCameraDistortion[3]);
            }
            
            ImGui::EndTabBar();
        }



        ImGui::EndChild();

        ImGui::SameLine();


        ImGui::BeginChild("CameraView");

        if (bIsCameraActive)
        {
            float aspect = (float)frameROI.height / (float)frameROI.width;
            float width = std::min(ImGui::GetContentRegionAvail().x, (float)frameROI.width);
            ImVec2 imageSize = ImVec2(width, width * aspect);

            ImVec2 UV0 = ImVec2((float)frameROI.x / (float)g_frameWidth, (float)frameROI.y / (float)g_frameHeight);
            ImVec2 UV1 = ImVec2((float)(frameROI.x + frameROI.width) / (float)g_frameWidth, (float)(frameROI.y + frameROI.height) / (float)g_frameHeight);

            if (bCalibrationCompleteLeft && !bRightCamera)
            {
                cv::Mat undistorted;
                if (g_bFisheyeLens)
                {
                    cv::Mat P, map1, map2;
                    cv::fisheye::estimateNewCameraMatrixForUndistortRectify(g_leftCameraIntrinsics, g_leftCameraDistortion, g_cameraFrameBuffer(frameROI).size(), cv::Matx33d::eye(), P, 1);
                    cv::fisheye::initUndistortRectifyMap(g_leftCameraIntrinsics, g_leftCameraDistortion, cv::Matx33d::eye(), P, g_cameraFrameBuffer(frameROI).size(), CV_16SC2, map1, map2);
                    cv::remap(g_cameraFrameBuffer(frameROI), undistorted, map1, map2, cv::INTER_LINEAR);
                }
                else
                {         
                    cv::undistort(g_cameraFrameBuffer(frameROI), undistorted, g_leftCameraIntrinsics, g_leftCameraDistortion);
                }

                if (g_frameWidthGPU != frameROI.width || g_frameHeightGPU != frameROI.height)
                {
                    SetupCameraFrameResource(frameROI.width, frameROI.height);
                }

                UploadFrame(undistorted);

                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
            else if (bCalibrationCompleteRight && bRightCamera)
            {
                cv::Mat undistorted;
                if (g_bFisheyeLens)
                {
                    cv::Mat P, map1, map2;
                    cv::fisheye::estimateNewCameraMatrixForUndistortRectify(g_rightCameraIntrinsics, g_rightCameraDistortion, g_cameraFrameBuffer(frameROI).size(), cv::Matx33d::eye(), P, 1);
                    cv::fisheye::initUndistortRectifyMap(g_rightCameraIntrinsics, g_rightCameraDistortion, cv::Matx33d::eye(), P, g_cameraFrameBuffer(frameROI).size(), CV_16SC2, map1, map2);
                    cv::remap(g_cameraFrameBuffer(frameROI), undistorted, map1, map2, cv::INTER_LINEAR);
                }
                else
                {
                    cv::undistort(g_cameraFrameBuffer(frameROI), undistorted, g_rightCameraIntrinsics, g_rightCameraDistortion);
                }

                if (g_frameWidthGPU != frameROI.width || g_frameHeightGPU != frameROI.height)
                {
                    SetupCameraFrameResource(frameROI.width, frameROI.height);
                }

                UploadFrame(undistorted);

                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
            else if (!bRightCamera && bCapturingCompleteLeft && !bCalibrationCompleteLeft && calibDataLeft.Frames.size() > 0)
            {
                if (selectedImageChanged)
                {
                    overlaidImage = calibDataLeft.Frames[displayedImage].clone();
                    cv::drawChessboardCorners(overlaidImage, cv::Size(calibDataLeft.ChessboardCornersX, calibDataLeft.ChessboardCornersY), calibDataLeft.CBPoints[displayedImage], calibDataLeft.ValidFrames[displayedImage]);

                    selectedImageChanged = false;
                }

                if (g_frameWidthGPU != frameROI.width || g_frameHeightGPU != frameROI.height)
                {
                    SetupCameraFrameResource(frameROI.width, frameROI.height);
                }

                UploadFrame(overlaidImage);
                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
            else if (bRightCamera && bCapturingCompleteRight && !bCalibrationCompleteRight && calibDataRight.Frames.size() > 0)
            {
                if (selectedImageChanged)
                {
                    overlaidImage = calibDataRight.Frames[displayedImage].clone();
                    cv::drawChessboardCorners(overlaidImage, cv::Size(calibDataRight.ChessboardCornersX, calibDataRight.ChessboardCornersY), calibDataRight.CBPoints[displayedImage], calibDataRight.ValidFrames[displayedImage]);

                    selectedImageChanged = false;
                }

                if (g_frameWidthGPU != frameROI.width || g_frameHeightGPU != frameROI.height)
                {
                    SetupCameraFrameResource(frameROI.width, frameROI.height);
                }

                UploadFrame(overlaidImage);
                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
            else if(bIsCapturing)
            {
                if (g_frameWidthGPU != frameROI.width || g_frameHeightGPU != frameROI.height)
                {
                    SetupCameraFrameResource(frameROI.width, frameROI.height);
                }

                cv::Mat frame = g_cameraFrameBuffer(frameROI);
                UploadFrame(frame);
                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize);
            }
            else
            {
                float aspect2 = (float)g_frameHeight / (float)g_frameWidth;
                float width2 = std::min(ImGui::GetContentRegionAvail().x, (float)g_frameWidth);
                ImVec2 imageSize2 = ImVec2(width2, width2 * aspect2);

                if (g_frameWidthGPU != g_frameWidth || g_frameHeightGPU != g_frameHeight)
                {
                    SetupCameraFrameResource(g_frameWidth, g_frameHeight);
                }

                UploadFrame(g_cameraFrameBuffer);
                ImGui::Image((void*)g_cameraFrameSRV.Get(), imageSize2);
            }
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
            cbRef.push_back(cv::Point3f((float)j * calibData.ChessboardSquareSize, (float)i * calibData.ChessboardSquareSize, 0));
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

    calibData.NumTakenFrames = calibData.Frames.size();

    for (int i = calibData.Frames.size() - 1; i >= 0; i--)
    {
        if (!calibData.ValidFrames[i])
        {
            calibData.Frames.erase(calibData.Frames.begin() + i);
            calibData.CBPoints.erase(calibData.CBPoints.begin() + i);
            calibData.RefPoints.erase(calibData.RefPoints.begin() + i);
            calibData.ValidFrames.erase(calibData.ValidFrames.begin() + i);
        }
    }

    if (calibData.RefPoints.empty())
    {
        return false;
    }
}


bool CalibrateSingleCamera(CalibrationData& calibData, bool bRightCamera, int width, int height)
{ 
    cv::Mat K;
    std::vector<double> D;
    std::vector<cv::Mat> rvecs, tvecs;

    if (calibData.Frames.size() < 1)
    {
        return false;
    }

    if (g_bFisheyeLens)
    {
        try
        {
            cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, DBL_EPSILON);
            calibData.CalibrationRMSError = cv::fisheye::calibrate(calibData.RefPoints, calibData.CBPoints, cv::Size(width, height), K, D, rvecs, tvecs, cv::fisheye::CALIB_FIX_SKEW | cv::fisheye::CALIB_CHECK_COND, calibTermCriteria);
        }
        catch (const cv::Exception& e)
        {
            const char* err_msg = e.what();
            std::cerr << "exception caught: " << err_msg << std::endl;
            return false;
        }
    }
    else
    {
        cv::TermCriteria calibTermCriteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, DBL_EPSILON);
        calibData.CalibrationRMSError = cv::calibrateCamera(calibData.RefPoints, calibData.CBPoints, cv::Size(width, height), K, D, rvecs, tvecs, cv::CALIB_FIX_K3, calibTermCriteria);

    }

    if (bRightCamera)
    {
        g_rightCameraIntrinsics = K.clone();
        g_rightCameraDistortion.resize(4);
        std::copy(D.begin(), D.end(), g_rightCameraDistortion.begin());
    }
    else
    {
        g_leftCameraIntrinsics = K.clone();
        g_leftCameraDistortion.resize(4);
        std::copy(D.begin(), D.end(), g_leftCameraDistortion.begin());
    }   

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

    g_videoCapture.read(g_cameraFrameBuffer);
}

void UploadFrame(cv::Mat& frameBuffer)
{
    if (!frameBuffer.empty())
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
