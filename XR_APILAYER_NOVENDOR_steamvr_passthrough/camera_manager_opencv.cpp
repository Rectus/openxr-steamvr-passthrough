#include "pch.h"
#include "camera_manager.h"
#include <log.h>
#include "layer.h"
#include <stdlib.h>


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


inline XrMatrix4x4f ToXRMatrix4x4(vr::HmdMatrix44_t& input)
{
    XrMatrix4x4f output =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], input.m[3][0],
        input.m[0][1], input.m[1][1], input.m[2][1], input.m[3][1],
        input.m[0][2], input.m[1][2], input.m[2][2], input.m[3][2],
        input.m[0][3], input.m[1][3], input.m[2][3], input.m[3][3]
    };
    return output;
}

inline XrMatrix4x4f ToXRMatrix4x4(vr::HmdMatrix34_t& input)
{
    XrMatrix4x4f output =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], 0,
        input.m[0][1], input.m[1][1], input.m[2][1], 0,
        input.m[0][2], input.m[1][2], input.m[2][2], 0,
        input.m[0][3], input.m[1][3], input.m[2][3], 1
    };
    return output;
}

inline XrMatrix4x4f ToXRMatrix4x4Inverted(vr::HmdMatrix44_t& input)
{
    XrMatrix4x4f temp =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], input.m[3][0],
        input.m[0][1], input.m[1][1], input.m[2][1], input.m[3][1],
        input.m[0][2], input.m[1][2], input.m[2][2], input.m[3][2],
        input.m[0][3], input.m[1][3], input.m[2][3], input.m[3][3]
    };
    XrMatrix4x4f output;
    XrMatrix4x4f_Invert(&output, &temp);
    return output;
}


inline XrMatrix4x4f ToXRMatrix4x4Inverted(vr::HmdMatrix34_t& input)
{
    XrMatrix4x4f temp =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], 0,
        input.m[0][1], input.m[1][1], input.m[2][1], 0,
        input.m[0][2], input.m[1][2], input.m[2][2], 0,
        input.m[0][3], input.m[1][3], input.m[2][3], 1
    };
    XrMatrix4x4f output;
    XrMatrix4x4f_Invert(&output, &temp);
    return output;
}


CameraManagerOpenCV::CameraManagerOpenCV(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, bool bIsAugmented)
    : m_renderer(renderer)
    , m_renderAPI(renderAPI)
    , m_appRenderAPI(appRenderAPI)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameLayout(EStereoFrameLayout::Mono)
    , m_projectionDistanceFar(5.0f)
    , m_useAlternateProjectionCalc(false)
    , m_videoCapture()
    , m_bIsAugmented(bIsAugmented)
{
    m_renderFrame = std::make_shared<CameraFrame>();
    m_servedFrame = std::make_shared<CameraFrame>();
    m_underConstructionFrame = std::make_shared<CameraFrame>();
    m_renderModels = std::make_shared<std::vector<RenderModel>>();
}

CameraManagerOpenCV::~CameraManagerOpenCV()
{
    DeinitCamera();

    if (m_serveThread.joinable())
    {
        m_bRunThread = false;
        m_serveThread.join();
    }
}

bool CameraManagerOpenCV::InitCamera()
{
    if (m_bCameraInitialized) { return true; }

    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    m_hmdDeviceId = m_openVRManager->GetHMDDeviceId();

    // Prevent long startup times on cams with many modes when using MSMF
    _putenv_s("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0");

    if (cameraConf.RequestCustomFrameSize)
    {
        std::vector<int> props = { cv::CAP_PROP_FRAME_WIDTH, cameraConf.CustomFrameDimensions[0], cv::CAP_PROP_FRAME_HEIGHT,  cameraConf.CustomFrameDimensions[1], cv::CAP_PROP_FPS, cameraConf.CustomFrameRate };
        m_videoCapture.open(cameraConf.Camera0DeviceIndex, cv::CAP_ANY, props);
    }
    else
    {
        // Prioritize FPS is no frame paramters set
        std::vector<int> props = { cv::CAP_PROP_FPS, 1000 };
        m_videoCapture.open(cameraConf.Camera0DeviceIndex, cv::CAP_ANY, props);
    }

    if (!m_videoCapture.isOpened())
    {
        ErrorLog("OpenCV VideoCapture failed to open!\n");
        return false;
    }

    Log("OpenCV Video capture opened using API: %s\n", m_videoCapture.getBackendName());

    if (cameraConf.AutoExposureEnable)
    {
        m_videoCapture.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
    }
    else
    {
        m_videoCapture.set(cv::CAP_PROP_EXPOSURE, cameraConf.ExposureValue);
    }

    UpdateStaticCameraParameters();

    double fps = m_videoCapture.get(cv::CAP_PROP_FPS);

    Log("Camera initalized: %d x %d @ %.1f\n", m_cameraFrameWidth, m_cameraFrameHeight, fps);

    m_bCameraInitialized = true;
    m_bRunThread = true;
    m_lastFrameTimestamp = 0;
    m_bIsPaused = false;

    if (!m_serveThread.joinable())
    {
        m_serveThread = std::thread(&CameraManagerOpenCV::ServeFrames, this);
    }

    return true;
}

void CameraManagerOpenCV::DeinitCamera()
{
    if (!m_bCameraInitialized) { return; }
    m_bCameraInitialized = false;
    m_bRunThread = false;

    if (m_serveThread.joinable())
    {
        m_serveThread.join();
    }

    m_videoCapture.release();
}

void CameraManagerOpenCV::GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, std::string& API) const
{
    if (m_videoCapture.isOpened())
    {
        width = m_cameraTextureWidth;
        height = m_cameraTextureHeight;
        fps = (float)m_videoCapture.get(cv::CAP_PROP_FPS);
        API = std::format("OpenCV - {}", m_videoCapture.getBackendName());
    }
    else
    {
        width = 0;
        height = 0;
        fps = 0;
        API = "OpenCV - Inactive";
    }
}

void CameraManagerOpenCV::GetDistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManagerOpenCV::GetUndistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraUndistortedTextureWidth;
    height = m_cameraUndistortedTextureHeight;
    bufferSize = m_cameraUndistortedFrameBufferSize;
}

void CameraManagerOpenCV::GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if(m_frameLayout == EStereoFrameLayout::Mono || cameraEye == LEFT_EYE)
    {
        focalLength.x = cameraConf.Camera0_IntrinsicsFocal[0] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
        focalLength.y = cameraConf.Camera0_IntrinsicsFocal[1] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
        center.x = cameraConf.Camera0_IntrinsicsCenter[0] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
        center.y = cameraConf.Camera0_IntrinsicsCenter[1] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
    }
    else
    {
        focalLength.x = cameraConf.Camera1_IntrinsicsFocal[0] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
        focalLength.y = cameraConf.Camera1_IntrinsicsFocal[1] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
        center.x = cameraConf.Camera1_IntrinsicsCenter[0] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
        center.y = cameraConf.Camera1_IntrinsicsCenter[1] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
    }

    if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
    {
        focalLength.y /= 2.0f;
        center.y /= 2.0f;
    }
    else if(m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
    {
        focalLength.x /= 2.0f;
        center.x /= 2.0f;
    } 
}

void CameraManagerOpenCV::GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();
    coeffs.v[0] = cameraConf.Camera0_IntrinsicsDist[0];
    coeffs.v[1] = cameraConf.Camera0_IntrinsicsDist[1];
    coeffs.v[2] = cameraConf.Camera0_IntrinsicsDist[2];
    coeffs.v[3] = cameraConf.Camera0_IntrinsicsDist[3];

    coeffs.v[8] = cameraConf.Camera1_IntrinsicsDist[0];
    coeffs.v[9] = cameraConf.Camera1_IntrinsicsDist[1];
    coeffs.v[10] = cameraConf.Camera1_IntrinsicsDist[2];
    coeffs.v[11] = cameraConf.Camera1_IntrinsicsDist[3];
}

EStereoFrameLayout CameraManagerOpenCV::GetFrameLayout() const
{
    return m_frameLayout;
}

bool CameraManagerOpenCV::IsUsingFisheyeModel() const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();
    return cameraConf.CameraHasFisheyeLens;
}

XrMatrix4x4f CameraManagerOpenCV::GetLeftToRightCameraTransform() const
{
    if (m_frameLayout == EStereoFrameLayout::Mono)
    {
        XrMatrix4x4f ident;
        XrMatrix4x4f_CreateIdentity(&ident);
        return ident;
    }

    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    XrMatrix4x4f result, camera0Pose, camera1Pose, transMatrix, rotMatrix, temp;

    XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.Camera0_Translation[0], cameraConf.Camera0_Translation[1], cameraConf.Camera0_Translation[2]);
    XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera0_Rotation[0], -cameraConf.Camera0_Rotation[1], -cameraConf.Camera0_Rotation[2]);

    XrMatrix4x4f_Multiply(&camera0Pose, &rotMatrix, &transMatrix);

    XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.Camera1_Translation[0], cameraConf.Camera1_Translation[1], cameraConf.Camera1_Translation[2]);
    XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera1_Rotation[0], -cameraConf.Camera1_Rotation[1], -cameraConf.Camera1_Rotation[2]);

    XrMatrix4x4f_Multiply(&camera1Pose, &rotMatrix, &transMatrix);

    XrMatrix4x4f_Invert(&temp, &camera0Pose);
    XrMatrix4x4f_Multiply(&result, &camera1Pose, &temp);

    return result;
}

void CameraManagerOpenCV::UpdateStaticCameraParameters()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    m_frameLayout = cameraConf.CameraFrameLayout;

    m_cameraTextureWidth = (int)m_videoCapture.get(cv::CAP_PROP_FRAME_WIDTH);
    m_cameraTextureHeight = (int)m_videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT);
    m_cameraFrameBufferSize = m_cameraTextureWidth * m_cameraTextureHeight * 4;

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }

    m_cameraUndistortedTextureWidth = m_cameraTextureWidth;
    m_cameraUndistortedTextureHeight = m_cameraTextureHeight;
    m_cameraUndistortedFrameBufferSize = m_cameraFrameBufferSize;


    if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
    {
            m_cameraFrameWidth = m_cameraTextureWidth;
            m_cameraFrameHeight = m_cameraTextureHeight / 2;

            m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth;
            m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight / 2;
    }
    else if(m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
    {
        m_cameraFrameWidth = m_cameraTextureWidth / 2;
        m_cameraFrameHeight = m_cameraTextureHeight;

        m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth / 2;
        m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight;
    }
    else // Mono
    {
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight;

        m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth;
        m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight;
    }

    vr::HmdMatrix44_t vrHMDProjectionLeft = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Left, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar * 2.0f);
    m_HMDViewToProjectionLeft = ToXRMatrix4x4(vrHMDProjectionLeft);

    vr::HmdMatrix34_t vrHMDViewLeft = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Left);
    m_viewToHMDLeft = ToXRMatrix4x4(vrHMDViewLeft);
    m_HMDToViewLeft = ToXRMatrix4x4Inverted(vrHMDViewLeft);

    vr::HmdMatrix44_t vrHMDProjectionRight = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Right, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar * 2.0f);
    m_HMDViewToProjectionRight = ToXRMatrix4x4(vrHMDProjectionRight);

    vr::HmdMatrix34_t vrHMDViewRight = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Right);
    m_viewToHMDRight = ToXRMatrix4x4(vrHMDViewRight);
    m_HMDToViewRight = ToXRMatrix4x4Inverted(vrHMDViewRight);
}

bool CameraManagerOpenCV::GetCameraFrame(std::shared_ptr<CameraFrame>& frame)
{
    if (!m_bCameraInitialized) { return false; }

    std::unique_lock<std::mutex> lock(m_serveMutex, std::try_to_lock);
    if (lock.owns_lock() && m_servedFrame->bIsValid)
    {
        m_renderFrame->bIsValid = false;
        m_renderFrame->frameTextureResource = nullptr;
        m_renderFrame.swap(m_servedFrame);

        frame = m_renderFrame;
        return true;
    }
    else if (m_renderFrame->bIsValid)
    {
        frame = m_renderFrame;
        return true;
    }

    return false;
}

void CameraManagerOpenCV::ServeFrames()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    

    if (!m_videoCapture.isOpened()) { return; }

    bool bHasFrame = false;
    uint32_t lastFrameSequence = 0;
    LARGE_INTEGER startFrameRetrievalTime;
    vr::TrackedDevicePose_t trackedDevicePoseArray[vr::k_unMaxTrackedDeviceCount];
    cv::Mat frameBuffer;
    LARGE_INTEGER exposureTime, perfCounterfreq;
    QueryPerformanceFrequency(&perfCounterfreq);

    while (m_bRunThread && m_videoCapture.isOpened())
    {
        if (m_bIsPaused)
        { 
            std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);
            continue; 
        }

        // Wait for the old frame struct to be available in case someone is still reading from it.
        std::unique_lock writeLock(m_underConstructionFrame->readWriteMutex);

        startFrameRetrievalTime = StartPerfTimer();

        Config_Main& mainConf = m_configManager->GetConfig_Main();
        Config_Camera& cameraConf = m_configManager->GetConfig_Camera();
            
        if (m_configManager->CheckCameraParamChangesPending())
        {
            if (cameraConf.AutoExposureEnable)
            {
                m_videoCapture.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
            }
            else
            {
                m_videoCapture.set(cv::CAP_PROP_EXPOSURE, cameraConf.ExposureValue);
            }
        }


        if (!m_videoCapture.grab())
        {
            ErrorLog("Failed to grab VideoCapture.\n");
            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);
            continue;
        }

        // Frame latency is approximated from when grab() returns.
        QueryPerformanceCounter(&exposureTime);

        vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, -cameraConf.FrameDelayOffset, trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount);

        uint64_t delayOffsetTicks = (uint64_t)(cameraConf.FrameDelayOffset * perfCounterfreq.QuadPart);
        m_underConstructionFrame->header.ulFrameExposureTime = (exposureTime.QuadPart - delayOffsetTicks);

        if (!m_videoCapture.retrieve(frameBuffer))
        {
            ErrorLog("Failed to retrieve VideoCapture.\n");
            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);
            continue;
        }

        if (!m_bRunThread) { return; }

        if (m_underConstructionFrame->frameBuffer.get() == nullptr)
        {
            m_underConstructionFrame->frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
        }

        cv::Mat paddedBuffer = cv::Mat(frameBuffer.rows, frameBuffer.cols, CV_8UC4, (void*)m_underConstructionFrame->frameBuffer->data());

        int from_to[] = { 0,2, 1,1, 2,0, -1,3 };
        cv::mixChannels(&frameBuffer, 1, &paddedBuffer, 1, from_to, frameBuffer.channels());

        m_underConstructionFrame->bHasFrameBuffer = true;

        bHasFrame = true;
 
        m_underConstructionFrame->header.nFrameSequence = ++m_lastFrameTimestamp;
        m_underConstructionFrame->header.nWidth = m_cameraTextureWidth;
        m_underConstructionFrame->header.nHeight = m_cameraTextureHeight;
        m_underConstructionFrame->header.nBytesPerPixel = 4;

        
        if (cameraConf.UseTrackedDevice)
        {
            int trackedDeviceIndex = 0;
            char buffer[128] = { 0 };
            for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
            {
                if (!vrSystem->IsTrackedDeviceConnected(i))
                {
                    break;
                }
                vrSystem->GetStringTrackedDeviceProperty(i, vr::Prop_SerialNumber_String, buffer, 127);
                if (strncmp(buffer, cameraConf.TrackedDeviceSerialNumber.data(), cameraConf.TrackedDeviceSerialNumber.size()) == 0)
                {
                    trackedDeviceIndex = i;
                    break;
                }

            }
            m_underConstructionFrame->header.trackedDevicePose = trackedDevicePoseArray[trackedDeviceIndex];
        }
        else
        {
            vr::HmdMatrix34_t pose = { 0 };
            pose.m[0][0] = 1.0;
            pose.m[1][1] = 1.0;
            pose.m[2][2] = 1.0;
            m_underConstructionFrame->header.trackedDevicePose.mDeviceToAbsoluteTracking = pose;
        }

        m_underConstructionFrame->bIsValid = true;
        m_underConstructionFrame->frameLayout = m_frameLayout;


        if (mainConf.ProjectToRenderModels)
        {
            UpdateRenderModels();
        }
        m_underConstructionFrame->renderModels = m_renderModels;


        XrMatrix4x4f trackedDevicePose = ToXRMatrix4x4(m_underConstructionFrame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

        XrMatrix4x4f camera0ToWorld, camera1ToWorld, camera0Pose, camera1Pose, transMatrix, rotMatrix, temp;

        XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.Camera0_Translation[0], cameraConf.Camera0_Translation[1], cameraConf.Camera0_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera0_Rotation[0], -cameraConf.Camera0_Rotation[1], -cameraConf.Camera0_Rotation[2]);

        XrMatrix4x4f_Multiply(&temp, &rotMatrix, &transMatrix);
        XrMatrix4x4f_Invert(&camera0Pose, &temp);

        if (m_frameLayout == EStereoFrameLayout::Mono)
        {
            XrMatrix4x4f_Multiply(&camera0ToWorld, &trackedDevicePose, &camera0Pose);

            m_underConstructionFrame->cameraViewToWorldLeft = camera0ToWorld;
            m_underConstructionFrame->cameraViewToWorldRight = camera0ToWorld;
        }
        else
        {
            XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.Camera1_Translation[0], cameraConf.Camera1_Translation[1], cameraConf.Camera1_Translation[2]);
            XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera1_Rotation[0], -cameraConf.Camera1_Rotation[1], -cameraConf.Camera1_Rotation[2]);

            XrMatrix4x4f_Multiply(&temp, &rotMatrix, &transMatrix);
            XrMatrix4x4f_Invert(&camera1Pose, &temp);

            // Apply offset calibration to camera positions.
            camera0Pose.m[12] *= mainConf.DepthOffsetCalibration;
            camera0Pose.m[13] *= mainConf.DepthOffsetCalibration;
            camera0Pose.m[14] *= mainConf.DepthOffsetCalibration;

            XrMatrix4x4f_Multiply(&camera0ToWorld, &trackedDevicePose, &camera0Pose);

            camera1Pose.m[12] *= mainConf.DepthOffsetCalibration;
            camera1Pose.m[13] *= mainConf.DepthOffsetCalibration;
            camera1Pose.m[14] *= mainConf.DepthOffsetCalibration;

            XrMatrix4x4f_Multiply(&camera1ToWorld, &trackedDevicePose, &camera1Pose);

            m_underConstructionFrame->cameraViewToWorldLeft = camera0ToWorld;
            m_underConstructionFrame->cameraViewToWorldRight = camera1ToWorld;
        }
   

        {
            std::lock_guard<std::mutex> lock(m_serveMutex);

            m_servedFrame.swap(m_underConstructionFrame);
        }

        m_averageFrameRetrievalTime = UpdateAveragePerfTime(m_frameRetrievalTimes, EndPerfTimer(startFrameRetrievalTime), 20);
    }
}


void CameraManagerOpenCV::UpdateRenderModels()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    vr::IVRRenderModels* vrRenderModels = m_openVRManager->GetVRRenderModels();

    for (int i = 1; i < vr::k_unMaxTrackedDeviceCount; i++)
    {
        if (!vrSystem->IsTrackedDeviceConnected(i))
        {
            break;
        }

        std::string modelName;
        modelName.resize(vr::k_unMaxPropertyStringSize);

        vr::TrackedPropertyError error;
        uint32_t numBytes = m_openVRManager->GetVRSystem()->GetStringTrackedDeviceProperty(i, vr::Prop_RenderModelName_String, modelName.data(), vr::k_unMaxPropertyStringSize, &error);

        bool bFoundModel = false;

        for (RenderModel model : *m_renderModels)
        {
            if (model.deviceId == i)
            {
                if (strncmp(model.modelName.data(), modelName.data(), vr::k_unMaxPropertyStringSize) != 0)
                {
                    vr::RenderModel_t* newModel;

                    if (vrRenderModels->LoadRenderModel_Async(modelName.data(), &newModel) == vr::VRRenderModelError_None)
                    {
                        model.modelName = modelName.data();
                        MeshCreateRenderModel(model.mesh, newModel);
                    }
                }

                bFoundModel = true;
                break;
            }
        }

        if (!bFoundModel)
        {
            vr::RenderModel_t* newModel;

            if (vrRenderModels->LoadRenderModel_Async(modelName.data(), &newModel) == vr::VRRenderModelError_None)
            {
                RenderModel rm;
                rm.deviceId = i;
                rm.modelName = modelName;
                MeshCreateRenderModel(rm.mesh, newModel);
                m_renderModels->push_back(rm);
            }
        }
    }
}


// Constructs a matrix from the roomscale origin to the HMD eye pose.
XrMatrix4x4f CameraManagerOpenCV::GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

    XrMatrix4x4f output, pose, viewToTracking, trackingToStage, refSpacePose;

    int viewNum = eye == LEFT_EYE ? 0 : 1;

    XrVector3f scale = { 1, 1, 1 };

    // The application provided HMD pose used to make sure reprojection works correctly.
    XrMatrix4x4f_CreateTranslationRotationScale(&pose, &layer.views[viewNum].pose.position, &layer.views[viewNum].pose.orientation, &scale);
    XrMatrix4x4f_Invert(&viewToTracking, &pose);

    // Apply any pose the application might have configured in its reference spaces.
    XrMatrix4x4f_CreateTranslationRotationScale(&pose, &refSpaceInfo.poseInReferenceSpace.position, &refSpaceInfo.poseInReferenceSpace.orientation, &scale);
    XrMatrix4x4f_Invert(&refSpacePose, &pose);


    if (refSpaceInfo.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
    {
        vr::HmdMatrix34_t mat = vrSystem->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();
        trackingToStage = ToXRMatrix4x4Inverted(mat);

        XrMatrix4x4f_Multiply(&pose, &refSpacePose, &trackingToStage);
        XrMatrix4x4f_Multiply(&output, &viewToTracking, &pose);
    }
    else
    {
        XrMatrix4x4f_Multiply(&output, &viewToTracking, &refSpacePose);
    }

    return output;
}

void CameraManagerOpenCV::UpdateProjectionMatrix(std::shared_ptr<CameraFrame>& frame)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;


    Config_Main& mainConf = m_configManager->GetConfig_Main();

    if (mainConf.ProjectionDistanceFar * 1.5f != m_projectionDistanceFar)
    {
        // Push back far plane by 1.5x to account for the flat projection plane of the passthrough frame.
        m_projectionDistanceFar = mainConf.ProjectionDistanceFar * 1.5f;


        XrFovf fov = {-1.0f, 1.0f, 1.0f, -1.0f};
        XrMatrix4x4f projectionMatrix;
        XrMatrix4x4f_CreateProjectionFov(&projectionMatrix, GRAPHICS_D3D, fov, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar);

        XrVector2f focalLength, center;
        GetIntrinsics(ERenderEye::LEFT_EYE, focalLength, center);

        projectionMatrix.m[0] = 2.0f * focalLength.x / (float)m_cameraTextureWidth;
        projectionMatrix.m[5] = 2.0f * focalLength.y / (float)m_cameraTextureHeight;
        projectionMatrix.m[8] = (1.0f - 2.0f * (center.x / (float)m_cameraTextureWidth));
        projectionMatrix.m[9] = (1.0f - 2.0f * (center.y / (float)m_cameraTextureHeight));


        XrMatrix4x4f scaleMatrix, offsetMatrix, transMatrix;

        if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
        {
            XrMatrix4x4f_CreateScale(&scaleMatrix, 0.5f, 1.0f, 1.0f);
            XrMatrix4x4f_CreateTranslation(&offsetMatrix, -0.5f, 0.0f, 0.0f);
            XrMatrix4x4f_Multiply(&transMatrix, &offsetMatrix, &scaleMatrix);
        }
        else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            XrMatrix4x4f_CreateScale(&scaleMatrix, 1.0f, 0.5f, 1.0f);
            XrMatrix4x4f_CreateTranslation(&offsetMatrix, 0.0f, 0.5f, 0.0f);
            XrMatrix4x4f_Multiply(&transMatrix, &offsetMatrix, &scaleMatrix);
        }
        else
        {
            XrMatrix4x4f_CreateIdentity(&transMatrix);
        }

        XrMatrix4x4f projectionMatrixInv;
        XrMatrix4x4f_Invert(&projectionMatrixInv, &projectionMatrix);

        XrMatrix4x4f_Multiply(&m_cameraProjectionInvFarLeft, &projectionMatrixInv, &transMatrix);

        if (bIsStereo)
        {
            XrMatrix4x4f projectionMatrix, projectionMatrixInv;
            XrMatrix4x4f_CreateProjectionFov(&projectionMatrix, GRAPHICS_D3D, fov, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar);
            XrMatrix4x4f_Invert(&projectionMatrixInv, &projectionMatrix);

            XrMatrix4x4f_Multiply(&m_cameraProjectionInvFarRight, &projectionMatrixInv, &transMatrix);
        }
    }
}


void CameraManagerOpenCV::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, std::shared_ptr<DepthFrame> depthFrame, const XrCompositionLayerProjection& layer, float timeToPhotons, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    UpdateProjectionMatrix(frame);

    CalculateFrameProjectionForEye(LEFT_EYE, frame, layer, refSpaceInfo, distortionParams);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, layer, refSpaceInfo, distortionParams);

    if (depthFrame->bIsFirstRender)
    {
        depthFrame->prevDispWorldToCameraProjectionLeft = m_lastDispWorldToCameraProjectionLeft;
        depthFrame->prevDispWorldToCameraProjectionRight = m_lastDispWorldToCameraProjectionRight;
        depthFrame->prevDisparityViewToWorldLeft = m_lastDisparityViewToWorldLeft;
        depthFrame->prevDisparityViewToWorldRight = m_lastDisparityViewToWorldRight;

        m_lastDispWorldToCameraProjectionLeft = frame->worldToCameraProjectionLeft;
        m_lastDispWorldToCameraProjectionRight = frame->worldToCameraProjectionRight;
        m_lastDisparityViewToWorldLeft = depthFrame->disparityViewToWorldLeft;
        m_lastDisparityViewToWorldRight = depthFrame->disparityViewToWorldRight;
    }

    if (frame->header.nFrameSequence != m_lastFrameSequence)
    {
        frame->prevCameraProjectionToWorldLeft = m_lastCameraProjectionToWorldLeft;
        frame->prevWorldToCameraProjectionLeft = m_lastWorldToCameraProjectionLeft;
        frame->prevCameraProjectionToWorldRight = m_lastCameraProjectionToWorldRight;
        frame->prevWorldToCameraProjectionRight = m_lastWorldToCameraProjectionRight;

        m_lastCameraProjectionToWorldLeft = frame->cameraProjectionToWorldLeft;
        m_lastWorldToCameraProjectionLeft = frame->worldToCameraProjectionLeft;
        m_lastCameraProjectionToWorldRight = frame->cameraProjectionToWorldRight;
        m_lastWorldToCameraProjectionRight = frame->worldToCameraProjectionRight;

        m_lastFrameSequence = frame->header.nFrameSequence;

        frame->bIsFirstRender = true;
    }
    else
    {
        // Previous HMD frame was rendered from the same camera frame
        frame->prevCameraProjectionToWorldLeft = frame->cameraProjectionToWorldLeft;
        frame->prevWorldToCameraProjectionLeft = frame->worldToCameraProjectionLeft;
        frame->prevCameraProjectionToWorldRight = frame->cameraProjectionToWorldRight;
        frame->prevWorldToCameraProjectionRight = frame->worldToCameraProjectionRight;

        frame->bIsFirstRender = false;
    }

    frame->prevWorldToHMDProjectionLeft = m_lastWorldToHMDProjectionLeft;
    frame->prevWorldToHMDProjectionRight = m_lastWorldToHMDProjectionRight;

    m_lastWorldToHMDProjectionLeft = frame->worldToHMDProjectionLeft;
    m_lastWorldToHMDProjectionRight = frame->worldToHMDProjectionRight;


    if (m_configManager->GetConfig_Main().ProjectToRenderModels)
    {
        LARGE_INTEGER time, freq;
        QueryPerformanceCounter(&time);
        QueryPerformanceFrequency(&freq);

        float exposureRelativeTime = -(float)(time.QuadPart - frame->header.ulFrameExposureTime);
        exposureRelativeTime /= ((float)freq.QuadPart);

        vr::TrackedDevicePose_t trackedDevicePoseArray[vr::k_unMaxTrackedDeviceCount];

        m_openVRManager->GetVRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, exposureRelativeTime, trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount);

        for (RenderModel& model : *m_renderModels.get())
        {
            model.meshToWorldTransform = ToXRMatrix4x4(trackedDevicePoseArray[model.deviceId].mDeviceToAbsoluteTracking);
        }
    }
}

void CameraManagerOpenCV::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t cameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    XrMatrix4x4f hmdWorldToView = GetHMDWorldToViewMatrix(eye, layer, refSpaceInfo);

    XrVector3f* projectionOriginWorld = (eye == LEFT_EYE) ? &frame->projectionOriginWorldLeft : &frame->projectionOriginWorldRight;
    XrMatrix4x4f* cameraViewToWorld = (eye == LEFT_EYE || !bIsStereo) ? &frame->cameraViewToWorldLeft : &frame->cameraViewToWorldRight;
    XrMatrix4x4f hmdViewToWorld;
    XrMatrix4x4f_Invert(&hmdViewToWorld, &hmdWorldToView);
    XrVector3f inPos{ 0,0,0 };
    XrMatrix4x4f_TransformVector3f(projectionOriginWorld, cameraViewToWorld, &inPos);


    float nearZ = NEAR_PROJECTION_DISTANCE;
    float farZ = m_projectionDistanceFar;

    const XrCompositionLayerDepthInfoKHR* depthInfo = nullptr;

    if (m_configManager->GetConfig_Depth().DepthReadFromApplication)
    {
        depthInfo = (const XrCompositionLayerDepthInfoKHR*)layer.views[(eye == LEFT_EYE) ? 0 : 1].next;

        while (depthInfo != nullptr)
        {
            if (depthInfo->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
            {
                break;
            }
            depthInfo = (const XrCompositionLayerDepthInfoKHR*)depthInfo->next;
        }

        // Match the near and far plane with application
        if (depthInfo)
        {
            // Handle reversed depth
            if (depthInfo->farZ < depthInfo->nearZ)
            {
                nearZ = depthInfo->farZ;
                farZ = depthInfo->nearZ;
                frame->bHasReversedDepth = true;
            }
            else
            {
                nearZ = depthInfo->nearZ;
                farZ = depthInfo->farZ;
                frame->bHasReversedDepth = false;
            }
        }
    }

    XrMatrix4x4f hmdProjection;
    XrMatrix4x4f_CreateProjectionFov(&hmdProjection, GRAPHICS_D3D, layer.views[(eye == LEFT_EYE) ? 0 : 1].fov, nearZ, farZ);

    // Handle infinite and reversed Z - XrMatrix4x4f_CreateProjectionFov sets it up wrong.
    if (depthInfo && (farZ == (std::numeric_limits<float>::max)() || !std::isfinite(farZ)))
    {
        hmdProjection.m[10] = 0;
        hmdProjection.m[14] = nearZ;
    }
    else if (depthInfo)
    {
        hmdProjection.m[10] = -(depthInfo->farZ * depthInfo->maxDepth - depthInfo->nearZ * depthInfo->minDepth) / (depthInfo->farZ - depthInfo->nearZ);
        hmdProjection.m[14] = -(depthInfo->farZ * depthInfo->nearZ * (depthInfo->maxDepth - depthInfo->minDepth)) / (depthInfo->farZ - depthInfo->nearZ);
    }

    XrMatrix4x4f* worldToHMDMatrix = (eye == LEFT_EYE) ? &frame->worldToHMDProjectionLeft : &frame->worldToHMDProjectionRight;

    XrMatrix4x4f_Multiply(worldToHMDMatrix, &hmdProjection, &hmdWorldToView);

    std::shared_lock readLock(distortionParams.readWriteMutex);

    XrMatrix4x4f frameProjection;
    XrMatrix4x4f_Transpose(&frameProjection, (cameraId == 0) ? &distortionParams.cameraProjectionLeft : &distortionParams.cameraProjectionRight);

    // Adjust camera space projection matrix to project to clip space.
    frameProjection.m[0] = 2.0f * frameProjection.m[0] / (float)m_cameraFrameWidth;
    frameProjection.m[5] = -2.0f * frameProjection.m[5] / (float)m_cameraFrameHeight;
    frameProjection.m[8] = (1.0f - 2.0f * frameProjection.m[8] / (float)m_cameraFrameWidth);
    frameProjection.m[9] = (1.0f - 2.0f * frameProjection.m[9] / (float)m_cameraFrameHeight);

    frameProjection.m[10] = -m_projectionDistanceFar / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
    frameProjection.m[11] = -1.0f;
    frameProjection.m[12] = 0.0f;// 2.0f * frameProjection.m[12] / (float)m_cameraFrameWidth; // This would add the raight to left transform.
    frameProjection.m[13] = 0.0f;
    frameProjection.m[14] = -(m_projectionDistanceFar * NEAR_PROJECTION_DISTANCE) / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
    frameProjection.m[15] = 0.0f;

    XrMatrix4x4f frameProjectionInverse;
    XrMatrix4x4f_Invert(&frameProjectionInverse, &frameProjection);

    XrMatrix4x4f leftCameraFromTrackingPose;
    XrMatrix4x4f_Invert(&leftCameraFromTrackingPose, &frame->cameraViewToWorldLeft);

    if (eye == LEFT_EYE)
    {
        XrMatrix4x4f rectifiedRotation = distortionParams.rectifiedRotationLeft;

        // The right eye rotation matrices work for some reason with the y(?) axis rotation reversed.
        rectifiedRotation.m[4] *= -1;
        rectifiedRotation.m[6] *= -1;


        XrMatrix4x4f rectifiedRotationInverse;
        XrMatrix4x4f_Transpose(&rectifiedRotationInverse, &rectifiedRotation);

        XrMatrix4x4f tempMatrix;
        XrMatrix4x4f_Multiply(&tempMatrix, &rectifiedRotationInverse, &leftCameraFromTrackingPose);
        XrMatrix4x4f_Multiply(&frame->worldToCameraProjectionLeft, &frameProjection, &tempMatrix);

        XrMatrix4x4f_Multiply(&tempMatrix, &rectifiedRotation, &frameProjectionInverse);
        XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldLeft, &frame->cameraViewToWorldLeft, &tempMatrix);
    }
    else
    {
        XrMatrix4x4f rightCameraFromTrackingPose;
        XrMatrix4x4f_Invert(&rightCameraFromTrackingPose, &frame->cameraViewToWorldRight);

        XrMatrix4x4f rectifiedRotation = distortionParams.rectifiedRotationRight;

        XrMatrix4x4f rectifiedRotationInverse;
        XrMatrix4x4f_Transpose(&rectifiedRotationInverse, &rectifiedRotation);

        XrMatrix4x4f tempMatrix;
        XrMatrix4x4f_Multiply(&tempMatrix, &rectifiedRotationInverse, &rightCameraFromTrackingPose);
        XrMatrix4x4f_Multiply(&frame->worldToCameraProjectionRight, &frameProjection, &tempMatrix);


        if (bIsStereo)
        {
            XrMatrix4x4f_Multiply(&tempMatrix, &rectifiedRotation, &frameProjectionInverse);
            XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &frame->cameraViewToWorldRight, &tempMatrix);
        }
        else
        {
            XrMatrix4x4f_Multiply(&tempMatrix, &distortionParams.rectifiedRotationLeft, &frameProjectionInverse);
            XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &frame->cameraViewToWorldLeft, &tempMatrix);
        }
    }
}
