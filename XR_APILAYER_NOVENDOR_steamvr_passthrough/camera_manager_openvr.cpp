
#include "pch.h"
#include "camera_manager.h"
#include "layer_structs.h"
#include "mathutil.h"
#include "perfutil.h"



CameraManagerOpenVR::CameraManagerOpenVR(std::shared_ptr<IPassthroughRenderer> inlineRenderer, std::shared_ptr<AsyncRenderer> asyncRenderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, EProjectionMode projectionMode)
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
    , m_gpuFrameQueue(5)
    , m_cpuFrameQueue(5)
{
}

CameraManagerOpenVR::~CameraManagerOpenVR()
{
    DeinitCamera();

    m_bRunThread = false;

    if (m_serveThread.joinable())
    {
        m_serveThread.join();
    }

    if (m_serveThreadBlockQueue.joinable())
    {
        m_serveThreadBlockQueue.join();
    }
}

bool CameraManagerOpenVR::InitCamera()
{
    if (m_bCameraInitialized) { return true; }

    Config_Main& mainConf = m_configManager->GetConfig_Main();
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    m_bCameraFailed = false;

    m_hmdDeviceId = m_openVRManager->GetHMDDeviceId();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera) 
    {
        g_logger->error("SteamVR Tracked Camera interface error!");
        return false; 
    }

    bool bHasCamera = false;
    vr::EVRTrackedCameraError error = trackedCamera->HasCamera(m_hmdDeviceId, &bHasCamera);
    if (error != vr::VRTrackedCameraError_None)
    {
        g_logger->error("Error %i checking camera on device {}", static_cast<int32_t>(error), m_hmdDeviceId);
        m_bCameraFailed = true;
        return false;
    }
    else if(!bHasCamera)
    {
        g_logger->error("No passthrough camera found!");
        m_bCameraFailed = true;
        return false;
    }

    UpdateStaticCameraParameters();

    vr::EVRTrackedCameraError cameraError = trackedCamera->AcquireVideoStreamingService(m_hmdDeviceId, &m_cameraHandle);

    if (cameraError != vr::VRTrackedCameraError_None)
    {
        g_logger->info("AcquireVideoStreamingService error {} on device {}", static_cast<int32_t>(error), m_hmdDeviceId);
        m_bCameraFailed = true;
        return false;
    }

    m_bCameraInitialized = true;
    m_bRunThread = true;
    m_bIsPaused = false;

    m_bUseBlockQueue = (cameraConf.OpenVR_UseBlockQueueForDepth &&
        m_projectionMode != Projection_StereoReconstruction) ||
        (cameraConf.OpenVR_UseBlockQueueForDepth && cameraConf.OpenVR_UseBlockQueueForColor);

    if (!m_serveThread.joinable())
    {
        m_serveThread = std::thread(&CameraManagerOpenVR::ServeFrames, this);
    }

    if (m_bUseBlockQueue && !m_serveThreadBlockQueue.joinable())
    {
        m_serveThreadBlockQueue = std::thread(&CameraManagerOpenVR::ServeBlockQueueFrames, this);
    }

    return true;
}

void CameraManagerOpenVR::DeinitCamera()
{
    if (!m_bCameraInitialized) { return; }
    m_bCameraInitialized = false;
    m_bRunThread = false;

    if (m_serveThread.joinable())
    {
        m_serveThread.join();
    }

    if (m_serveThreadBlockQueue.joinable())
    {
        m_serveThreadBlockQueue.join();
    }

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (trackedCamera)
    {
        vr::EVRTrackedCameraError error = trackedCamera->ReleaseVideoStreamingService(m_cameraHandle);

        if (error != vr::VRTrackedCameraError_None)
        {
            g_logger->info("ReleaseVideoStreamingService error {}", static_cast<int32_t>(error));
        }
    }
}

EPassthroughCameraState CameraManagerOpenVR::GetCameraState() const
{
    if (m_bCameraFailed)
    {
        return CameraState_Error;
    }
    if (!m_bCameraInitialized)
    {
        return CameraState_Uninitialized;
    }
    else if (m_bIsPaused)
    {
        return CameraState_Idle;
    }
    else if (m_bWaitingForCamera || !m_bPoseAvailable)
    {
        return CameraState_Waiting;
    }
    else
    {
        return CameraState_Active;
    }
}

void CameraManagerOpenVR::GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, ECameraProvider& provider, bool& bIsActive) const
{
    if (m_bCameraInitialized)
    {
        width = m_cameraTextureWidth;
        height = m_cameraTextureHeight;
        fps = 0;
        provider = ECameraProvider::CameraProvider_OpenVR;
        bIsActive = true;
    }
    else
    {
        width = 0;
        height = 0;
        fps = 0;
        provider = ECameraProvider::CameraProvider_OpenVR;
        bIsActive = false;
    }
}

void CameraManagerOpenVR::GetDistortedTextureSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManagerOpenVR::GetUndistortedTextureSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraUndistortedTextureWidth;
    height = m_cameraUndistortedTextureHeight;
    bufferSize = m_cameraUndistortedFrameBufferSize;
}

void CameraManagerOpenVR::GetDistortedFrameSize(uint32_t& width, uint32_t& height) const
{
    width = m_cameraFrameWidth;
    height = m_cameraFrameHeight;
}

void CameraManagerOpenVR::GetUndistortedFrameSize(uint32_t& width, uint32_t& height) const
{
    width = m_cameraUndistortedFrameWidth;
    height = m_cameraUndistortedFrameHeight;
}

void CameraManagerOpenVR::GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if (!cameraConf.OpenVRCustomCalibration)
    {
        uint32_t cameraIndex = 0;

        if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal)
        {
            cameraIndex = (cameraEye == RenderEye_Left) ? 0 : 1;
        }
        else if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
        {
            // The camera indices are reversed for vertical layouts.
            cameraIndex = (cameraEye == RenderEye_Left) ? 1 : 0;
        }

        vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

        // Use the OpenVR undistorted intrinsics as a baseline for the custom undistortion.
        vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraIntrinsics(m_hmdDeviceId, cameraIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, (vr::HmdVector2_t*)&focalLength, (vr::HmdVector2_t*)&center);
        if (cameraError != vr::VRTrackedCameraError_None)
        {
            g_logger->error("GetCameraIntrinsics error %i on device Id %i\n", static_cast<int32_t>(cameraError), m_hmdDeviceId);
        }

        // Multiply the values by the distorted/undistorted ratio, since we need the values for the distorted resolution.
        float xRatio = ((float)m_cameraFrameWidth) / m_cameraUndistortedFrameWidth;
        float yRatio = ((float)m_cameraFrameHeight) / m_cameraUndistortedFrameHeight;

        // For some reason the focal length is incorrect for enlarged undistorted frames, at least on the Vive.
        //focalLength.x *= xRatio;
        //focalLength.y *= yRatio;

        center.x *= xRatio;
        center.y *= yRatio;
    }
    else
    {
        if (m_frameLayout == EStereoFrameLayout::FrameLayout_Mono || 
            (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal && cameraEye == RenderEye_Left) || 
            (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical && cameraEye == RenderEye_Right))
        {
            focalLength.x = cameraConf.OpenVR_Camera0_IntrinsicsFocal[0] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
            focalLength.y = cameraConf.OpenVR_Camera0_IntrinsicsFocal[1] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
            center.x = cameraConf.OpenVR_Camera0_IntrinsicsCenter[0] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
            center.y = cameraConf.OpenVR_Camera0_IntrinsicsCenter[1] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
        }
        else
        {
            focalLength.x = cameraConf.OpenVR_Camera1_IntrinsicsFocal[0] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
            focalLength.y = cameraConf.OpenVR_Camera1_IntrinsicsFocal[1] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
            center.x = cameraConf.OpenVR_Camera1_IntrinsicsCenter[0] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[0] * m_cameraFrameWidth;
            center.y = cameraConf.OpenVR_Camera1_IntrinsicsCenter[1] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[1] * m_cameraFrameHeight;
        }
    }
}

void CameraManagerOpenVR::GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if (!cameraConf.OpenVRCustomCalibration)
    {
        vr::TrackedPropertyError error;
        uint32_t numBytes = m_openVRManager->GetVRSystem()->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraDistortionCoefficients_Float_Array, vr::k_unFloatPropertyTag, &coeffs, 16 * sizeof(double), &error);
        if (error != vr::TrackedProp_Success || numBytes == 0)
        {
            g_logger->error("Failed to get tracked camera distortion coefficients, error [{}]", static_cast<int32_t>(error));
        }
    }
    else
    {
        if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
        {
            coeffs.v[0] = cameraConf.OpenVR_Camera1_IntrinsicsDist[0];
            coeffs.v[1] = cameraConf.OpenVR_Camera1_IntrinsicsDist[1];
            coeffs.v[2] = cameraConf.OpenVR_Camera1_IntrinsicsDist[2];
            coeffs.v[3] = cameraConf.OpenVR_Camera1_IntrinsicsDist[3];

            coeffs.v[8] = cameraConf.OpenVR_Camera0_IntrinsicsDist[0];
            coeffs.v[9] = cameraConf.OpenVR_Camera0_IntrinsicsDist[1];
            coeffs.v[10] = cameraConf.OpenVR_Camera0_IntrinsicsDist[2];
            coeffs.v[11] = cameraConf.OpenVR_Camera0_IntrinsicsDist[3];
        }
        else
        {
            coeffs.v[0] = cameraConf.OpenVR_Camera0_IntrinsicsDist[0];
            coeffs.v[1] = cameraConf.OpenVR_Camera0_IntrinsicsDist[1];
            coeffs.v[2] = cameraConf.OpenVR_Camera0_IntrinsicsDist[2];
            coeffs.v[3] = cameraConf.OpenVR_Camera0_IntrinsicsDist[3];

            coeffs.v[8] = cameraConf.OpenVR_Camera1_IntrinsicsDist[0];
            coeffs.v[9] = cameraConf.OpenVR_Camera1_IntrinsicsDist[1];
            coeffs.v[10] = cameraConf.OpenVR_Camera1_IntrinsicsDist[2];
            coeffs.v[11] = cameraConf.OpenVR_Camera1_IntrinsicsDist[3];
        }
    }
}

EStereoFrameLayout CameraManagerOpenVR::GetFrameLayout() const
{
    return m_frameLayout;
}

bool CameraManagerOpenVR::IsUsingFisheyeModel() const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if (cameraConf.CameraForceDistortionMode == CameraDistortionMode_Fisheye) { return true; }
    if (cameraConf.CameraForceDistortionMode == CameraDistortionMode_RegularLens) { return false; }
    if (cameraConf.CameraForceDistortionMode == CameraDistortionMode_NoDistortion) { return false; }

    if (cameraConf.OpenVRCustomCalibration)
    {
        return cameraConf.OpenVR_CameraHasFisheyeLens;
    }
    
    vr::EVRDistortionFunctionType distTypes[4] = {};

    vr::TrackedPropertyError error;
    uint32_t numBytes = m_openVRManager->GetVRSystem()->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraDistortionFunction_Int32_Array, vr::k_unInt32PropertyTag, &distTypes, 4 * sizeof(int32_t), &error);
    if (error != vr::TrackedProp_Success || numBytes == 0)
    {
        g_logger->error("Failed to get tracked camera distortion types, error [{}]", static_cast<int32_t>(error));
        return false;
    }

    if (m_frameLayout != FrameLayout_Mono && distTypes[0] != distTypes[1])
    {
        g_logger->error("Error: Mismatched camera distortion functions are not supported {} != {}", static_cast<int32_t>(distTypes[0]), static_cast<int32_t>(distTypes[1]));
        return false;
    }

    // Both options in the enum are f-theta fisheye models. Assuming the None model is just a f-tan(theta) pinhole camera.
    return distTypes[0] == vr::VRDistortionFunctionType_FTheta || 
        distTypes[0] == vr::VRDistortionFunctionType_Extended_FTheta;
}

XrMatrix4x4f CameraManagerOpenVR::GetLeftToRightCameraTransform() const
{
    return m_cameraLeftToRightPose;
}

void CameraManagerOpenVR::GetTrackedCameraEyePoses(XrMatrix4x4f& LeftPose, XrMatrix4x4f& RightPose, bool bForceOpenVRValue)
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if (!cameraConf.OpenVRCustomCalibration || bForceOpenVRValue)
    {
        vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

        vr::HmdMatrix34_t Buffer[2];
        vr::TrackedPropertyError error;
        bool bGotLeftCamera = true;
        bool bGotRightCamera = true;

        uint32_t numBytes = vrSystem->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &Buffer, sizeof(Buffer), &error);
        if (error != vr::TrackedProp_Success || numBytes == 0)
        {
            g_logger->error("Failed to get tracked camera pose array, error [{}]", static_cast<int32_t>(error));
            bGotLeftCamera = false;
            bGotRightCamera = false;
        }

        if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal)
        {
            LeftPose = ToXRMatrix4x4(Buffer[0]);
            RightPose = ToXRMatrix4x4(Buffer[1]);
        }
        else if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
        {
            // Vertical layouts have the right camera at index 0.
            LeftPose = ToXRMatrix4x4(Buffer[1]);
            RightPose = ToXRMatrix4x4(Buffer[0]);

            // Hack to remove scaling from Vive Pro Eye matrix.
            LeftPose.m[5] = abs(LeftPose.m[5]);
            LeftPose.m[10] = abs(LeftPose.m[10]);
        }
        else
        {
            LeftPose = ToXRMatrix4x4(Buffer[0]);
            RightPose = ToXRMatrix4x4(Buffer[0]);
        }
    }
    else
    {
        XrMatrix4x4f camera0Pose, camera1Pose, transMatrix, rotMatrix;

        XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.OpenVR_Camera0_Translation[0], -cameraConf.OpenVR_Camera0_Translation[1], -cameraConf.OpenVR_Camera0_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.OpenVR_Camera0_Rotation[0], -cameraConf.OpenVR_Camera0_Rotation[1], -cameraConf.OpenVR_Camera0_Rotation[2]);

        XrMatrix4x4f_Multiply(&camera0Pose, &rotMatrix, &transMatrix);

        XrMatrix4x4f_CreateTranslation(&transMatrix, -cameraConf.OpenVR_Camera1_Translation[0], -cameraConf.OpenVR_Camera1_Translation[1], -cameraConf.OpenVR_Camera1_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.OpenVR_Camera1_Rotation[0], -cameraConf.OpenVR_Camera1_Rotation[1], -cameraConf.OpenVR_Camera1_Rotation[2]);

        XrMatrix4x4f_Multiply(&camera1Pose, &rotMatrix, &transMatrix); 

        if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal)
        {
            XrMatrix4x4f_Invert(&LeftPose, &camera0Pose);
            XrMatrix4x4f_Invert(&RightPose, &camera1Pose);
        }
        else if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
        {
            XrMatrix4x4f_Invert(&LeftPose, &camera1Pose);
            XrMatrix4x4f_Invert(&RightPose, &camera0Pose);
        }
        else 
        {
            XrMatrix4x4f_Invert(&LeftPose, &camera0Pose);
            XrMatrix4x4f_Invert(&RightPose, &camera0Pose);
        }
    }
}

void CameraManagerOpenVR::UpdateStaticCameraParameters()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, vr::VRTrackedCameraFrameType_Distorted, &m_cameraTextureWidth, &m_cameraTextureHeight, &m_cameraFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        g_logger->error("CameraFrameSize error {} on device Id {}", static_cast<int32_t>(cameraError), m_hmdDeviceId);
    }

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        g_logger->error("Invalid frame size received: Width = {}, Height = {}, Size = {}", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }

    cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &m_cameraUndistortedTextureWidth, &m_cameraUndistortedTextureHeight, &m_cameraUndistortedFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        g_logger->error("CameraFrameSize error {} on device Id {}", static_cast<int32_t>(cameraError), m_hmdDeviceId);
    }

    if (m_cameraUndistortedTextureWidth == 0 || m_cameraUndistortedTextureHeight == 0 || m_cameraUndistortedFrameBufferSize == 0)
    {
        g_logger->error("Invalid frame size received: Width = {}, Height = {}, Size = {}", m_cameraUndistortedTextureWidth, m_cameraUndistortedTextureHeight, m_cameraUndistortedFrameBufferSize);
    }

    vr::TrackedPropertyError propError;

    int32_t layout = (vr::EVRTrackedCameraFrameLayout)vrSystem->GetInt32TrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraFrameLayout_Int32, &propError);

    if (propError != vr::TrackedProp_Success)
    {
        g_logger->error("GetTrackedCameraEyePoses error {}", static_cast<int32_t>(propError));
    }

    if ((layout & vr::EVRTrackedCameraFrameLayout_Stereo) != 0)
    {
        if ((layout & vr::EVRTrackedCameraFrameLayout_VerticalLayout) != 0)
        {
            m_frameLayout = EStereoFrameLayout::FrameLayout_StereoVertical;
            m_cameraFrameWidth = m_cameraTextureWidth;
            m_cameraFrameHeight = m_cameraTextureHeight / 2;

            m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth;
            m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight / 2;
        }
        else
        {
            m_frameLayout = EStereoFrameLayout::FrameLayout_StereoHorizontal;
            m_cameraFrameWidth = m_cameraTextureWidth / 2;
            m_cameraFrameHeight = m_cameraTextureHeight;

            m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth / 2;
            m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight;
        }
    }
    else
    {
        m_frameLayout = EStereoFrameLayout::FrameLayout_Mono;
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight;

        m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth;
        m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight;
    }

    GetTrackedCameraEyePoses(m_cameraToHMDLeft, m_cameraToHMDRight, false);
    XrMatrix4x4f_Invert(&m_HMDToCameraLeft, &m_cameraToHMDLeft);
    XrMatrix4x4f_Invert(&m_HMDToCameraRight, &m_cameraToHMDRight);

    XrMatrix4x4f_Multiply(&m_cameraLeftToRightPose, &m_HMDToCameraRight, &m_cameraToHMDLeft);
}


FramePtr<CameraGPUFrame> CameraManagerOpenVR::AcquireCameraGPUFrame()
{
    if (!m_bCameraInitialized) { return FramePtr<CameraGPUFrame>(); }

    return m_gpuFrameQueue.AcquireRead();
}

FramePtr<CameraCPUFrame>  CameraManagerOpenVR::AcquireCameraCPUFrame()
{
    if (!m_bCameraInitialized) { return FramePtr<CameraCPUFrame>(); }

    return m_cpuFrameQueue.AcquireRead();
}



void CameraManagerOpenVR::ServeFrames()
{
    Config_Main& mainConf = m_configManager->GetConfig_Main();
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera)
    {
        return;
    }

    ComPtr<ID3D11Device> d3dInteropDevice;

    if (m_renderAPI == RenderAPI_Vulkan) // For the legacy Vulkan renderer.
    {
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &d3dInteropDevice, NULL, NULL);
    }

    m_bWaitingForCamera = true;
    uint32_t lastFrameSequence = 0;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        if (m_bIsPaused) { continue; }

        m_bUseBlockQueue = (cameraConf.OpenVR_UseBlockQueueForDepth &&
            m_projectionMode != Projection_StereoReconstruction) ||
            (cameraConf.OpenVR_UseBlockQueueForDepth && cameraConf.OpenVR_UseBlockQueueForColor);

        bool bUseBlockQueueColor = cameraConf.OpenVR_UseBlockQueueForDepth && 
            cameraConf.OpenVR_UseBlockQueueForColor && 
            m_projectionMode != Projection_RoomView2D;

        if (m_bUseBlockQueue && !m_serveThreadBlockQueue.joinable())
        {
            m_serveThreadBlockQueue = std::thread(&CameraManagerOpenVR::ServeBlockQueueFrames, this);
        }

        if (bUseBlockQueueColor)
        {
            continue;
        }
        

        vr::CameraVideoStreamFrameHeader_t frameHeader{};

        while (true)
        {
            m_gpuFrameTimer.StartPerfTimer();

            vr::EVRTrackedCameraFrameType frameType = m_projectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, nullptr, 0, &frameHeader, sizeof(vr::CameraVideoStreamFrameHeader_t));

            if (error == vr::VRTrackedCameraError_None)
            {
                float frameLatencyMS = m_gpuFrameTimer.GetStartTimeDiffMS(frameHeader.ulFrameExposureTime);

                if (frameLatencyMS > FRAME_TIMEOUT_MS) // We were served a too old frame to be useful.
                {
                    m_bWaitingForCamera = true;
                }
                else if (m_bWaitingForCamera) // Always accept the first frame offered if we were timed out.
                {
                    break;
                }
                else if (frameHeader.nFrameSequence != lastFrameSequence) // Normal wait for the next frame.
                {
                    break;
                }
            }
            else if (error == vr::VRTrackedCameraError_NoFrameAvailable)
            {
                m_bWaitingForCamera = true;
            }
            else
            {
                g_logger->error("GetVideoStreamFrameBuffer-header error {}", static_cast<int32_t>(error));
            }



            if (!m_bRunThread) { return; }

            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);

            if (!m_bRunThread) { return; }
        }

        if (!m_bRunThread) { return; }



        FramePtr<CameraGPUFrame> gpuFrame = m_gpuFrameQueue.AcquireWrite();
        if (!gpuFrame.HasFrame())
        {
            continue;
        }

        vr::EVRTrackedCameraFrameType frameType = m_projectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

        if (m_renderAPI == RenderAPI_Direct3D11)
        {
            std::shared_ptr<IPassthroughRenderer> renderer = m_inlineRenderer.lock();

            if (!renderer.get())
            {
                continue;
            }

            std::shared_lock accessLock(renderer->GetAccessMutex(), std::try_to_lock);
            if (!accessLock.owns_lock())
            {
                continue;
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, frameType, renderer->GetRenderDevice(), &gpuFrame->FrameTextureResource, nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                g_logger->error("GetVideoStreamTextureD3D11 error {}", static_cast<int32_t>(error));
                continue;
            }
        }
        else if (m_renderAPI == RenderAPI_Vulkan)
        {
            ID3D11ShaderResourceView* srv;

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, frameType, d3dInteropDevice.Get(), (void**)&srv, nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                g_logger->error("GetVideoStreamTextureD3D11 error {}", static_cast<int32_t>(error));
                continue;
            }

            ComPtr<IDXGIResource> dxgiRes;
            ID3D11Resource* res;
            srv->GetResource(&res);
            res->QueryInterface(IID_PPV_ARGS(&dxgiRes));
            dxgiRes->GetSharedHandle(&gpuFrame->FrameTextureResource);
        }

        m_bWaitingForCamera = false;
        lastFrameSequence = frameHeader.nFrameSequence;

        gpuFrame->bIsValid = true;
        gpuFrame->FrameLayout = m_frameLayout;
        gpuFrame->bColorsPreadjusted = false;
        gpuFrame->FrameSequence = frameHeader.nFrameSequence;
        gpuFrame->FrameExposureTimestamp = frameHeader.ulFrameExposureTime;
        gpuFrame->FrameSize[0] = frameHeader.nWidth;
        gpuFrame->FrameSize[1] = frameHeader.nHeight;
        gpuFrame->bisRectifiedFrame = frameType != vr::VRTrackedCameraFrameType_Distorted;

        XrMatrix4x4f headToTrackingPose;
        if (!GetHMDPoseForTime(headToTrackingPose, frameHeader.ulFrameExposureTime))
        {
            m_bPoseAvailable = false;
            continue;
        }
        m_bPoseAvailable = true;

        XrMatrix4x4f viewToWorldLeft, viewToWorldRight;
        XrMatrix4x4f_Multiply(&viewToWorldLeft, &headToTrackingPose, &m_cameraToHMDLeft);
        XrMatrix4x4f_Multiply(&viewToWorldRight, &headToTrackingPose, &m_cameraToHMDRight);

        gpuFrame->CameraViewToWorldLeft = viewToWorldLeft;
        gpuFrame->CameraViewToWorldRight = viewToWorldRight;

        // Camera projection matrix for Room View mode
        if (m_projectionMode == Projection_RoomView2D)
        {
            XrMatrix4x4f worldToCameraProjectionLeft;
            XrMatrix4x4f_Multiply(&worldToCameraProjectionLeft, &viewToWorldLeft, &m_cameraProjectionInvLeft);
            XrMatrix4x4f_Invert(&gpuFrame->WorldToCameraProjectionLeft, &worldToCameraProjectionLeft);

            XrMatrix4x4f worldToCameraProjectionRight;
            if (m_frameLayout != EStereoFrameLayout::FrameLayout_Mono)
            {
                XrMatrix4x4f_Multiply(&worldToCameraProjectionRight, &viewToWorldRight, &m_cameraProjectionInvRight);
            }
            else
            {
                XrMatrix4x4f_Multiply(&worldToCameraProjectionRight, &viewToWorldLeft, &m_cameraProjectionInvLeft);
            }
            XrMatrix4x4f_Invert(&gpuFrame->WorldToCameraProjectionRight, &worldToCameraProjectionRight);
        }

        m_gpuFrameTimer.EndPerfTimer();


        

        if (m_bUseBlockQueue || m_projectionMode != Projection_StereoReconstruction)
        {
            gpuFrame.CommitWrite();
            continue;
        }

        m_cpuFrameTimer.StartPerfTimer();

        FramePtr<CameraCPUFrame> cpuFrame = m_cpuFrameQueue.AcquireWrite();
        if (!cpuFrame.HasFrame())
        {
            continue;
        }

        bool bGotCPUFrame = false;

        // Get the CPU frame for depth reconstruction.
        if(m_appRenderAPI == RenderAPI_Direct3D11 || m_appRenderAPI == RenderAPI_Direct3D12)
        {
            if (cpuFrame->FrameBuffer.get() == nullptr || cpuFrame->FrameBuffer->size() < m_cameraFrameBufferSize)
            {
                cpuFrame->FrameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, cpuFrame->FrameBuffer->data(), (uint32_t)cpuFrame->FrameBuffer->size(), nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                g_logger->error("GetVideoStreamFrameBuffer error {}", static_cast<int32_t>(error));
            }
            else
            {
                bGotCPUFrame = true;
            }
        }
        // GetVideoStreamFrameBuffer crashes when used with Vulkan and OpenGL apps,
        // for those APIs we manually copy it from the GPU texture instead.
        else if (m_renderAPI == RenderAPI_Direct3D11 && gpuFrame->FrameTextureResource != nullptr)
        {
            if (cpuFrame->FrameBuffer.get() == nullptr || cpuFrame->FrameBuffer->size() < m_cameraFrameBufferSize)
            {
                cpuFrame->FrameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            std::shared_ptr<IPassthroughRenderer> renderer = m_inlineRenderer.lock();

            if (renderer.get())
            {
                std::shared_lock accessLock(renderer->GetAccessMutex(), std::try_to_lock);
                if (accessLock.owns_lock())
                {
                    bGotCPUFrame = renderer->DownloadTextureToCPU(gpuFrame->FrameTextureResource, frameHeader.nWidth, frameHeader.nHeight, (uint32_t)cpuFrame->FrameBuffer->size(), cpuFrame->FrameBuffer->data());
                }
            }
        }

        gpuFrame.CommitWrite();

        if (bGotCPUFrame)
        {
            cpuFrame->bIsValid = true;
            cpuFrame->bIsRaw = false;
            cpuFrame->RawFrameDataBytes = 0;
            cpuFrame->FrameExposureTimestamp = frameHeader.ulFrameExposureTime;
            cpuFrame->FrameLayout = m_frameLayout;
            cpuFrame->FrameSequence = lastFrameSequence;
            cpuFrame->FrameSize[0] = m_cameraTextureWidth;
            cpuFrame->FrameSize[1] = m_cameraTextureHeight;
            cpuFrame->CameraViewToWorldLeft = viewToWorldLeft;
            cpuFrame->CameraViewToWorldRight = viewToWorldRight;

            if (m_configManager->CheckFrameTextureDumpPending())
            {
                DumpCameraFrameTexture(cpuFrame->FrameBuffer, m_cameraTextureWidth, m_cameraTextureHeight, "OpenVR");
            }

            cpuFrame.CommitWrite();

            m_cpuFrameTimer.EndPerfTimer();
        }
    }
}



void CameraManagerOpenVR::ServeBlockQueueFrames()
{
    Config_Main& mainConf = m_configManager->GetConfig_Main();
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    vr::IVRBlockQueue* vrBlockQueue = m_openVRManager->GetVRBlockQueue();
    vr::IVRPaths* vrPaths = m_openVRManager->GetVRPaths();

    if (!vrBlockQueue || !vrPaths)
    {
        return;
    }


    vr::PropertyContainerHandle_t rawFrameQueue = 0;

    vr::EBlockQueueError queueError = vrBlockQueue->Connect(&rawFrameQueue, "/lighthouse/camera/raw_frames");
    if (queueError != vr::EBlockQueueError_BlockQueueError_None)
    {
        g_logger->error("Error connecting to camera block queue {}", static_cast<int32_t>(queueError));
        return;
    }

    ECameraFrameFormat frameFormat = FrameFormat_Unknown;
    int32_t rawFrameWidth = 0;
    int32_t rawFrameHeight = 0;

    {   
        vr::ETrackedPropertyError propError;

        vr::PathRead_t read = {};
        read.unRequiredBufferSize = 0;
        read.pszPath = nullptr;

        vrPaths->StringToHandle(&read.ulPath, "/format");
        read.pvBuffer = &frameFormat;
        read.unBufferSize = sizeof(int32_t);
        read.unTag = vr::k_unInt32PropertyTag;

        propError = vrPaths->ReadPathBatch(rawFrameQueue, &read, 1);
        if (propError != vr::TrackedProp_Success)
        {
            g_logger->error("Error reading camera block queue format {}", static_cast<int32_t>(propError));
            return;
        }

        vrPaths->StringToHandle(&read.ulPath, "/width");
        read.pvBuffer = &rawFrameWidth;
        read.unBufferSize = sizeof(rawFrameWidth);
        read.unTag = vr::k_unInt32PropertyTag;

        propError = vrPaths->ReadPathBatch(rawFrameQueue, &read, 1);
        if (propError != vr::TrackedProp_Success)
        {
            g_logger->error("Error reading camera block queue frame width {}", static_cast<int32_t>(propError));
            return;
        }

        vrPaths->StringToHandle(&read.ulPath, "/height");
        read.pvBuffer = &rawFrameHeight;
        read.unBufferSize = sizeof(rawFrameHeight);
        read.unTag = vr::k_unInt32PropertyTag;

        propError = vrPaths->ReadPathBatch(rawFrameQueue, &read, 1);
        if (propError != vr::TrackedProp_Success)
        {
            g_logger->error("Error reading camera block queue frame height {}", static_cast<int32_t>(propError));
            return;
        }
    }

    vr::PathHandle_t frameSizeHandle;
    vrPaths->StringToHandle(&frameSizeHandle, "/frame_size");

    vr::PathHandle_t frameSequenceHandle;
    vrPaths->StringToHandle(&frameSequenceHandle, "/frame_sequence");

    vr::PathHandle_t frameTimeMonotonicHandle;
    vrPaths->StringToHandle(&frameTimeMonotonicHandle, "/frame_time_monotonic");

    vr::PathHandle_t serverTimeTicksHandle;
    vrPaths->StringToHandle(&serverTimeTicksHandle, "/server_time_ticks");

    vr::PathHandle_t deliveryRateHandle;
    vrPaths->StringToHandle(&deliveryRateHandle, "/delivery_rate");

    vr::PathHandle_t elapsedTimeHandle;
    vrPaths->StringToHandle(&elapsedTimeHandle, "/elapsed_time");

    bool bWaitingForCamera = true;
    uint64_t lastFrameSequence = 0;
    vr::PropertyContainerHandle_t readHandle = 0;
    uint8_t* readBuffer = nullptr;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(FRAME_POLL_INTERVAL);

        if (!m_bRunThread) { return; }

        if (m_bIsPaused || !m_bUseBlockQueue) { continue; }

        bool bUseBlockQueueColor = cameraConf.OpenVR_UseBlockQueueForDepth &&
            cameraConf.OpenVR_UseBlockQueueForColor &&
            m_projectionMode != Projection_RoomView2D;

        uint64_t frameSequence = 0;
        uint64_t frameExposureTimestamp = 0;
        int32_t rawFrameDataBytes = 0;

        bool bGotFrame = false;

        while (true)
        {
            if (!m_bRunThread) { return; }

            if (m_bIsPaused || !m_bUseBlockQueue)
            {
                std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);
                break;
            }

            queueError = vrBlockQueue->WaitAndAcquireReadOnlyBlock(rawFrameQueue, &readHandle, (void**)&readBuffer, vr::EBlockQueueReadType_BlockQueueRead_Next, 10);
            if (queueError == vr::EBlockQueueError_BlockQueueError_BlockNotAvailable)
            {
                bWaitingForCamera = true;
            }
            else if (queueError != vr::EBlockQueueError_BlockQueueError_None)
            {
                g_logger->error("WaitAndAcquireReadOnlyBlock error {}", static_cast<int32_t>(queueError));
            }
            else
            {
                // Start measuring here to not include wait.
                m_cpuFrameTimer.StartPerfTimer();


                vr::ETrackedPropertyError propError;

                vr::PathRead_t read = {};
                read.unRequiredBufferSize = 0;
                read.pszPath = nullptr;

                read.ulPath = frameSequenceHandle;
                read.pvBuffer = &frameSequence;
                read.unBufferSize = sizeof(uint64_t);
                read.unTag = vr::k_unUint64PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    g_logger->error("Error reading /frame_sequence {}", static_cast<int32_t>(propError));
                }

                read.ulPath = serverTimeTicksHandle;
                read.pvBuffer = &frameExposureTimestamp;
                read.unBufferSize = sizeof(uint64_t);
                read.unTag = vr::k_unUint64PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    g_logger->error("Error reading /server_time_ticks {}", static_cast<int32_t>(propError));
                }

                read.ulPath = frameSizeHandle;
                read.pvBuffer = &rawFrameDataBytes;
                read.unBufferSize = sizeof(int32_t);
                read.unTag = vr::k_unInt32PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    rawFrameDataBytes = 0;
                    g_logger->error("Error reading /frame_size {}", static_cast<int32_t>(propError));
                }


                float frameLatencyMS = m_cpuFrameTimer.GetStartTimeDiffMS(frameExposureTimestamp);

                if (frameLatencyMS > FRAME_TIMEOUT_MS) // We were served a too old frame to be useful.
                {
                    bWaitingForCamera = true;
                    m_bWaitingForCamera = true;
                }
                else if (rawFrameDataBytes <= 0) // Infalid frame
                {
                    g_logger->warn("0 byte block queue frame received!");
                }
                else if (bWaitingForCamera) // Always accept the first frame offered if we were timed out.
                {
                    bGotFrame = true;
                    break;
                }
                else if (frameSequence != lastFrameSequence) // Normal wait for the next frame.
                {
                    bGotFrame = true;
                    break;
                }

                queueError = vrBlockQueue->ReleaseReadOnlyBlock(rawFrameQueue, readHandle);
                if (queueError != vr::EBlockQueueError_BlockQueueError_None)
                {
                    g_logger->error("ReleaseReadOnlyBlock error {}", static_cast<int32_t>(queueError));
                }

            }

            if (!m_bRunThread) {  return; }
        }

        if (!bGotFrame)
        {
            continue;
        }


        // check that we are still running before doing a costly memcpy.
        if (!m_bRunThread) 
        { 
            queueError = vrBlockQueue->ReleaseReadOnlyBlock(rawFrameQueue, readHandle);
            if (queueError != vr::EBlockQueueError_BlockQueueError_None)
            {
                g_logger->error("ReleaseReadOnlyBlock error {}", static_cast<int32_t>(queueError));
            }
            return; 
        }

        FramePtr<CameraCPUFrame> cpuFrame = m_cpuFrameQueue.AcquireWrite();
        if (!cpuFrame.HasFrame())
        {
            queueError = vrBlockQueue->ReleaseReadOnlyBlock(rawFrameQueue, readHandle);
            if (queueError != vr::EBlockQueueError_BlockQueueError_None)
            {
                g_logger->error("ReleaseReadOnlyBlock error {}", static_cast<int32_t>(queueError));
            }

            continue;
        }

        if (cpuFrame->FrameBuffer.get() == nullptr || cpuFrame->FrameBuffer->size() < rawFrameDataBytes)
        {
            cpuFrame->FrameBuffer = std::make_shared<std::vector<uint8_t>>(rawFrameDataBytes);
        }

        memcpy(cpuFrame->FrameBuffer->data(), readBuffer, rawFrameDataBytes);

        queueError = vrBlockQueue->ReleaseReadOnlyBlock(rawFrameQueue, readHandle);
        if (queueError != vr::EBlockQueueError_BlockQueueError_None)
        {
            g_logger->error("ReleaseReadOnlyBlock error {}", static_cast<int32_t>(queueError));
        }

        if (m_configManager->CheckFrameTextureDumpPending())
        {
            DumpCameraFrameTexture(cpuFrame->FrameBuffer, rawFrameWidth, rawFrameHeight, "OpenVR-BlockQueue");
        }

        bWaitingForCamera = false;
        m_bWaitingForCamera = false;
        lastFrameSequence = frameSequence;

        XrMatrix4x4f headToTrackingPose;
        if (!GetHMDPoseForTime(headToTrackingPose, frameExposureTimestamp))
        {
            m_bPoseAvailable = false;
            continue;
        }
        m_bPoseAvailable = true;

        cpuFrame->bIsValid = true;
        cpuFrame->bIsRaw = true;
        cpuFrame->FrameLayout = m_frameLayout;
        cpuFrame->RawFrameFormat = frameFormat;
        cpuFrame->FrameSize[0] = m_cameraTextureWidth;
        cpuFrame->FrameSize[1] = m_cameraTextureHeight;
        cpuFrame->RawFrameSize[0] = rawFrameWidth;
        cpuFrame->RawFrameSize[1] = rawFrameHeight;
        cpuFrame->RawFrameDataBytes = rawFrameDataBytes;
        cpuFrame->FrameExposureTimestamp = frameExposureTimestamp;
        cpuFrame->FrameSequence = frameSequence;

        XrMatrix4x4f_Multiply(&cpuFrame->CameraViewToWorldLeft, &headToTrackingPose, &m_cameraToHMDLeft);
        XrMatrix4x4f_Multiply(&cpuFrame->CameraViewToWorldRight, &headToTrackingPose, &m_cameraToHMDRight);

        

        m_cpuFrameTimer.EndPerfTimer();

        if (bUseBlockQueueColor)
        {
            cpuFrame.CommitWriteAndAcuireRead();
            CopyCPUFrameToGPU(cpuFrame.GetSharedPointer());
        }  
        else
        {
            cpuFrame.CommitWrite();
        }
    }
}


void CameraManagerOpenVR::CopyCPUFrameToGPU(std::shared_ptr<CameraCPUFrame> inFrame)
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
        return;
    }

    if (asyncRenderer->CopyAndDecodeCameraFrame(inFrame, &gpuFrame->FrameTextureResource))
    {
        
        gpuFrame->bIsValid = true;
        gpuFrame->bHasReversedDepth = false;
        gpuFrame->bIsFirstRender = true;
        gpuFrame->bIsRenderingMirrored = false;
        gpuFrame->FrameLayout = m_frameLayout;
        gpuFrame->FrameSequence = (uint32_t)inFrame->FrameSequence;
        gpuFrame->FrameExposureTimestamp = inFrame->FrameExposureTimestamp;
        gpuFrame->CameraViewToWorldLeft = inFrame->CameraViewToWorldLeft;
        gpuFrame->CameraViewToWorldRight = inFrame->CameraViewToWorldRight;
        gpuFrame->bColorsPreadjusted = m_configManager->CheckEnableAsyncColorAdjustment();
        gpuFrame->bisRectifiedFrame = false;

        gpuFrame.CommitWrite();
    }
    else
    {
        gpuFrame->bIsValid = false;
    }

    m_gpuFrameTimer.EndPerfTimer();
}

bool CameraManagerOpenVR::GetHMDPoseForTime(XrMatrix4x4f& headToTrackingPose, const uint64_t time)
{
    vr::TrackedDevicePose_t hmdPose;

    LARGE_INTEGER currentTime, freq;
    QueryPerformanceCounter(&currentTime);
    QueryPerformanceFrequency(&freq);

    float exposureRelativeTime = -(float)(currentTime.QuadPart - time);
    exposureRelativeTime /= ((float)freq.QuadPart);

    m_openVRManager->GetVRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, exposureRelativeTime, &hmdPose, 1);

    headToTrackingPose = ToXRMatrix4x4(hmdPose.mDeviceToAbsoluteTracking);
    return hmdPose.bPoseIsValid;
}



void CameraManagerOpenVR::UpdateFrameProjectionMatrix(std::shared_ptr<CameraGPUFrame>& frame)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::FrameLayout_Mono;

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    if (mainConf.ProjectionDistanceFar * 1.5f != m_projectionDistanceFar)
    {
        // Push back far plane by 1.5x to account for the flat projection plane of the passthrough frame.
        m_projectionDistanceFar = mainConf.ProjectionDistanceFar * 1.5f;

        // The camera indices are reversed for vertical layouts.
        uint32_t leftIndex = (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical) ? 1 : 0;

        vr::HmdMatrix44_t vrProjection;
        vr::EVRTrackedCameraError error = trackedCamera->GetCameraProjection(m_hmdDeviceId, leftIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

        if (error != vr::VRTrackedCameraError_None)
        {
            g_logger->error("CameraProjection error {} on device {}", static_cast<int32_t>(error), m_hmdDeviceId);
            return;
        }

        XrMatrix4x4f scaleMatrix, offsetMatrix, transMatrix;

        if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoHorizontal)
        {
            XrMatrix4x4f_CreateScale(&scaleMatrix, 0.5f, 1.0f, 1.0f);
            XrMatrix4x4f_CreateTranslation(&offsetMatrix, -0.5f, 0.0f, 0.0f);
            XrMatrix4x4f_Multiply(&transMatrix, &offsetMatrix, &scaleMatrix);
        }
        else if (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical)
        {
            XrMatrix4x4f_CreateScale(&scaleMatrix, 1.0f, 0.5f, 1.0f);
            XrMatrix4x4f_CreateTranslation(&offsetMatrix, 0.0f, 0.5f, 0.0f);
            XrMatrix4x4f_Multiply(&transMatrix, &offsetMatrix, &scaleMatrix);
        }
        else
        {
            XrMatrix4x4f_CreateIdentity(&transMatrix);
        }

        XrMatrix4x4f projectionMatrix = ToXRMatrix4x4Inverted(vrProjection);
        XrMatrix4x4f_Multiply(&m_cameraProjectionInvLeft, &projectionMatrix, &transMatrix);

        if (bIsStereo)
        {
            // The camera indices are reversed for vertical layouts.
            uint32_t rightIndex = (m_frameLayout == EStereoFrameLayout::FrameLayout_StereoVertical) ? 0 : 1;

            error = trackedCamera->GetCameraProjection(m_hmdDeviceId, rightIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

            if (error != vr::VRTrackedCameraError_None)
            {
                g_logger->error("CameraProjection error {} on device {}", static_cast<int32_t>(error), m_hmdDeviceId);
                return;
            }


            XrMatrix4x4f projectionMatrix = ToXRMatrix4x4Inverted(vrProjection);
            XrMatrix4x4f_Multiply(&m_cameraProjectionInvRight, &projectionMatrix, &transMatrix);

        }
    }
}
