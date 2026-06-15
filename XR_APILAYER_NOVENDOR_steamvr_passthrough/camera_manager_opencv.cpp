
#include "pch.h"

#include "camera_manager.h"
#include "layer_structs.h"
#include <stdlib.h>
#include "mathutil.h"
#include "perfutil.h"




CameraManagerOpenCV::CameraManagerOpenCV(std::shared_ptr<IPassthroughRenderer> inlineRenderer, std::shared_ptr<AsyncRenderer> asyncRenderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, EProjectionMode projectionMode, bool bIsAugmented)
    : m_inlineRenderer(inlineRenderer)
    , m_asyncRenderer(asyncRenderer)
    , m_renderAPI(renderAPI)
    , m_appRenderAPI(appRenderAPI)
    , m_projectionMode(projectionMode)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameLayout(EStereoFrameLayout::FrameLayout_Mono)
    , m_projectionDistanceFar(5.0f)
    , m_useAlternateProjectionCalc(false)
    , m_videoCapture()
    , m_bIsAugmented(bIsAugmented)
    , m_gpuFrameQueue(4)
    , m_cpuFrameQueue(3)
{
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
        g_logger->error("OpenCV VideoCapture failed to open!");
        return false;
    }

    g_logger->info("OpenCV Video capture opened using API: {}", m_videoCapture.getBackendName());

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

    g_logger->info("Camera initalized: {} x {} @ {:.1f}", m_cameraFrameWidth, m_cameraFrameHeight, fps);

    m_bCameraInitialized = true;
    m_bRunThread = true;
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

EPassthroughCameraState CameraManagerOpenCV::GetCameraState() const
{
    // TODO: Errors and waiting not tracked currently.
    if (!m_bCameraInitialized)
    {
        return CameraState_Uninitialized;
    }
    else if(m_bIsPaused)
    {
        return CameraState_Idle;
    }
    else
    {
        return CameraState_Active;
    }
}

void CameraManagerOpenCV::GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, ECameraProvider& provider, bool& bIsActive) const
{
    if (m_videoCapture.isOpened())
    {
        width = m_cameraTextureWidth;
        height = m_cameraTextureHeight;
        fps = (float)m_videoCapture.get(cv::CAP_PROP_FPS);
        provider = CameraProvider_OpenCV;
        bIsActive = true;
    }
    else
    {
        width = 0;
        height = 0;
        fps = 0;
        provider = CameraProvider_OpenCV;
        bIsActive = false;
    }
}

void CameraManagerOpenCV::GetDistortedTextureSize(uint32_t& width, uint32_t& height) const
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
}

void CameraManagerOpenCV::GetDistortedFrameSize(uint32_t& width, uint32_t& height) const
{
    width = m_cameraFrameWidth;
    height = m_cameraFrameHeight;
}

void CameraManagerOpenCV::GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if(m_frameLayout == EStereoFrameLayout::FrameLayout_Mono || cameraEye == RenderEye_Left)
    {
        focalLength.x = cameraConf.Camera0_IntrinsicsFocal[0] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
        focalLength.y = cameraConf.Camera0_IntrinsicsFocal[1] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
        center.x = cameraConf.Camera0_IntrinsicsCenter[0] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
        center.y = cameraConf.Camera0_IntrinsicsCenter[1] / (float)cameraConf.Camera0_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
    }
    else
    {
        focalLength.x = cameraConf.Camera1_IntrinsicsFocal[0] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
        focalLength.y = cameraConf.Camera1_IntrinsicsFocal[1] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
        center.x = cameraConf.Camera1_IntrinsicsCenter[0] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
        center.y = cameraConf.Camera1_IntrinsicsCenter[1] / (float)cameraConf.Camera1_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
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

    if (cameraConf.CameraForceDistortionMode == CameraDistortionMode_Fisheye) { return true; }
    if (cameraConf.CameraForceDistortionMode == CameraDistortionMode_RegularLens) { return false; }

    return cameraConf.CameraHasFisheyeLens;
}

XrMatrix4x4f CameraManagerOpenCV::GetLeftToRightCameraTransform() const
{
    if (m_frameLayout == EStereoFrameLayout::FrameLayout_Mono)
    {
        XrMatrix4x4f ident;
        XrMatrix4x4f_CreateIdentity(&ident);
        return ident;
    }

    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    XrMatrix4x4f result, camera0Pose, camera1Pose, transMatrix, rotMatrix, temp;

    XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.Camera0_Translation[0], -cameraConf.Camera0_Translation[1], -cameraConf.Camera0_Translation[2]);
    XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera0_Rotation[0], -cameraConf.Camera0_Rotation[1], -cameraConf.Camera0_Rotation[2]);

    XrMatrix4x4f_Multiply(&camera0Pose, &rotMatrix, &transMatrix);

    XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.Camera1_Translation[0], -cameraConf.Camera1_Translation[1], -cameraConf.Camera1_Translation[2]);
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
        g_logger->error("Invalid frame size received: Width = {}, Height = {}, Size = {}", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }


    if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
    {
            m_cameraFrameWidth = m_cameraTextureWidth;
            m_cameraFrameHeight = m_cameraTextureHeight / 2;
    }
    else if(m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal)
    {
        m_cameraFrameWidth = m_cameraTextureWidth / 2;
        m_cameraFrameHeight = m_cameraTextureHeight;
    }
    else // Mono
    {
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight;
    }
}

FramePtr<CameraGPUFrame> CameraManagerOpenCV::AcquireCameraGPUFrame()
{
    if (!m_bCameraInitialized) { return FramePtr<CameraGPUFrame>(); }

    return m_gpuFrameQueue.AcquireRead();
}

void CameraManagerOpenCV::ReleaseCameraGPUFrame(std::shared_ptr<CameraGPUFrame> frame)
{
    m_gpuFrameQueue.ReleaseRead(frame);
}

FramePtr<CameraCPUFrame> CameraManagerOpenCV::AcquireCameraCPUFrame()
{
    if (!m_bCameraInitialized) { return FramePtr<CameraCPUFrame>(); }

    return m_cpuFrameQueue.AcquireRead();
}

void CameraManagerOpenCV::ServeFrames()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    

    if (!m_videoCapture.isOpened()) { return; }

    uint32_t frameSequence = 0;
    vr::TrackedDevicePose_t trackedDevicePoseArray[vr::k_unMaxTrackedDeviceCount];
    cv::Mat frameBuffer;
    uint64_t tickFreq = GetSytemTickFrequency();

    while (m_bRunThread && m_videoCapture.isOpened())
    {
        if (m_bIsPaused)
        { 
            std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);
            continue; 
        }

        m_cpuFrameTimer.StartPerfTimer();

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
            g_logger->error("Failed to grab VideoCapture!");
            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);
            continue;
        }

        FramePtr<CameraCPUFrame> cpuFrame = m_cpuFrameQueue.AcquireWrite();
        if (!cpuFrame.HasFrame())
        {
            g_logger->warn("Camera CPU frame underrun!");
            continue;
        }

        // Frame latency is approximated from when grab() returns.
        uint64_t currentTime = GetCurrentTimeSytemTicks();

        vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, -cameraConf.FrameDelayOffset, trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount);

        uint64_t delayOffsetTicks = (uint64_t)(cameraConf.FrameDelayOffset * tickFreq);
        cpuFrame->FrameExposureTimestamp = (currentTime - delayOffsetTicks);

        if (!m_videoCapture.retrieve(frameBuffer))
        {
            g_logger->error("Failed to retrieve VideoCapture!");
            continue;
        }

        if (!m_bRunThread) { return; }

        if (cpuFrame->FrameBuffer.get() == nullptr)
        {
            cpuFrame->FrameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
        }

        cv::Mat paddedBuffer = cv::Mat(frameBuffer.rows, frameBuffer.cols, CV_8UC4, (void*)cpuFrame->FrameBuffer->data());

        int from_to[] = { 0,2, 1,1, 2,0, -1,3 };
        cv::mixChannels(&frameBuffer, 1, &paddedBuffer, 1, from_to, frameBuffer.channels());

        cpuFrame->bIsRaw = true;
        cpuFrame->RawFrameFormat = FrameFormat_RGBX32;
        cpuFrame->RawFrameDataBytes = m_cameraFrameBufferSize;
        cpuFrame->RawFrameSize = { (uint32_t)frameBuffer.cols, (uint32_t)frameBuffer.rows };


        if (m_configManager->CheckFrameTextureDumpPending())
        {
            DumpCameraFrameTexture(cpuFrame->FrameBuffer, m_cameraTextureWidth, m_cameraTextureHeight, "OpenCV");
        }
      
        XrMatrix4x4f trackedDevicePose;

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
                if (strncmp(buffer, cameraConf.TrackedDeviceSerialNumber, MAX_CAMERA_SERIAL_NUMBER_SIZE) == 0)
                {
                    trackedDeviceIndex = i;
                    break;
                }

            }
            trackedDevicePose = ToXRMatrix4x4(trackedDevicePoseArray[trackedDeviceIndex].mDeviceToAbsoluteTracking);
        }
        else
        {
            vr::HmdMatrix34_t pose = { 0 };
            pose.m[0][0] = 1.0;
            pose.m[1][1] = 1.0;
            pose.m[2][2] = 1.0;
            trackedDevicePose = ToXRMatrix4x4(pose);
        }
    

        XrMatrix4x4f camera0ToWorld, camera1ToWorld, camera0Pose, camera1Pose, transMatrix, rotMatrix, temp;

        XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.Camera0_Translation[0], -cameraConf.Camera0_Translation[1], -cameraConf.Camera0_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera0_Rotation[0], -cameraConf.Camera0_Rotation[1], -cameraConf.Camera0_Rotation[2]);

        XrMatrix4x4f_Multiply(&temp, &rotMatrix, &transMatrix);
        XrMatrix4x4f_Invert(&camera0Pose, &temp);

        if (m_frameLayout == EStereoFrameLayout::FrameLayout_Mono)
        {
            XrMatrix4x4f_Multiply(&camera0ToWorld, &trackedDevicePose, &camera0Pose);

            cpuFrame->CameraViewToWorldLeft = camera0ToWorld;
            cpuFrame->CameraViewToWorldRight = camera0ToWorld;
        }
        else
        {
            XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.Camera1_Translation[0], -cameraConf.Camera1_Translation[1], -cameraConf.Camera1_Translation[2]);
            XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.Camera1_Rotation[0], -cameraConf.Camera1_Rotation[1], -cameraConf.Camera1_Rotation[2]);

            XrMatrix4x4f_Multiply(&temp, &rotMatrix, &transMatrix);
            XrMatrix4x4f_Invert(&camera1Pose, &temp);

            XrMatrix4x4f_Multiply(&camera0ToWorld, &trackedDevicePose, &camera0Pose);
            XrMatrix4x4f_Multiply(&camera1ToWorld, &trackedDevicePose, &camera1Pose);

            cpuFrame->CameraViewToWorldLeft = camera0ToWorld;
            cpuFrame->CameraViewToWorldRight = camera1ToWorld;
        }

        cpuFrame->bIsValid = true;
        cpuFrame->FrameLayout = m_frameLayout;
        cpuFrame->FrameSize = { m_cameraTextureWidth, m_cameraTextureHeight };
   
        cpuFrame->FrameSequence = frameSequence;
        frameSequence = (frameSequence + 1) % 16;

        m_cpuFrameTimer.EndPerfTimer();

        cpuFrame.CommitWriteAndAcquireRead();
        CopyCPUFrameToGPU(cpuFrame.GetSharedPointer());
    }
}


void CameraManagerOpenCV::CopyCPUFrameToGPU(std::shared_ptr<CameraCPUFrame> inFrame)
{
    m_gpuFrameTimer.StartPerfTimer();

    std::shared_ptr<AsyncRenderer> asyncRenderer = m_asyncRenderer.lock();
    if (!asyncRenderer.get())
    {
        return;
    }

    FramePtr<CameraGPUFrame> gpuFrame = m_gpuFrameQueue.AcquireWrite();
    if (!gpuFrame.HasFrame())
    {
        g_logger->warn("Camera GPU frame underrun!");
        return;
    }

    if (asyncRenderer->CopyAndDecodeCameraFrame(inFrame, &gpuFrame->FrameTextureResource))
    {
        gpuFrame->bIsValid = true;
        gpuFrame->FrameLayout = m_frameLayout;
        gpuFrame->FrameSequence = inFrame->FrameSequence;
        gpuFrame->FrameExposureTimestamp = inFrame->FrameExposureTimestamp;
        gpuFrame->CameraLeft.ViewToWorld = inFrame->CameraViewToWorldLeft;
        gpuFrame->CameraRight.ViewToWorld = inFrame->CameraViewToWorldRight;
        gpuFrame->bColorsPreadjusted = m_configManager->CheckEnableAsyncColorAdjustment();
        gpuFrame->bisRectifiedFrame = false;
        gpuFrame->FrameSize = inFrame->FrameSize;

        gpuFrame.CommitWrite();
    }
    else
    {
        gpuFrame->bIsValid = false;
    }

    m_gpuFrameTimer.EndPerfTimer();
}

