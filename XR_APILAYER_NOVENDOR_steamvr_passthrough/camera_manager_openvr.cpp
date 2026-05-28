
#include "pch.h"
#include "camera_manager.h"
#include "layer_structs.h"
#include "mathutil.h"
#include "perfutil.h"



CameraManagerOpenVR::CameraManagerOpenVR(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
    : m_renderer(renderer)
    , m_renderAPI(renderAPI)
    , m_appRenderAPI(appRenderAPI)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameLayout(EStereoFrameLayout::FrameLayout_Mono)
    , m_projectionDistanceFar(5.0f)
    , m_useAlternateProjectionCalc(false)
{
    m_renderFrame = std::make_shared<CameraFrame>();
    m_servedFrame = std::make_shared<CameraFrame>();
    m_underConstructionFrame = std::make_shared<CameraFrame>();

    m_renderFrameCPU = std::make_shared<CameraCPUFrame>();
    m_servedFrameCPU = std::make_shared<CameraCPUFrame>();
    m_underConstructionFrameCPU = std::make_shared<CameraCPUFrame>();

    
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

    m_bUseBlockQueue = m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForDepth;

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

bool CameraManagerOpenVR::GetCameraFrame(std::shared_ptr<CameraFrame>& frame)
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

bool CameraManagerOpenVR::GetCameraFrameForWrite(std::shared_ptr<CameraFrame>& frame)
{
    if (!m_bCameraInitialized || !m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForColor) { return false; }

    if (m_extrenalFrameWriteLock.mutex() && m_extrenalFrameWriteLock.owns_lock())
    {
        m_extrenalFrameWriteLock.unlock();
    }

    m_extrenalFrameWriteLock = std::unique_lock<std::shared_mutex>(m_underConstructionFrame->readWriteMutex, std::try_to_lock);
    if (m_extrenalFrameWriteLock.owns_lock())
    {
        frame = m_underConstructionFrame;
        return true;
    }

    return false;
}

void CameraManagerOpenVR::ReleaseCameraFrameForWrite(std::shared_ptr<CameraFrame>& frame)
{
    if (m_extrenalFrameWriteLock.owns_lock())
    {
        std::lock_guard<std::mutex> lock(m_serveMutex);
        m_servedFrame.swap(m_underConstructionFrame);

        m_extrenalFrameWriteLock.unlock();
    }
}

bool CameraManagerOpenVR::GetCameraCPUFrame(std::shared_ptr<CameraCPUFrame>& frame)
{
    if (!m_bCameraInitialized) { return false; }

    std::unique_lock<std::mutex> lock(m_serveMutexCPU, std::try_to_lock);
    if (lock.owns_lock() && m_servedFrameCPU->bIsValid)
    {
        m_renderFrameCPU->bIsValid = false;
        m_renderFrameCPU.swap(m_servedFrameCPU);

        frame = m_renderFrameCPU;
        return true;
    }
    else if (m_renderFrameCPU->bIsValid)
    {
        frame = m_renderFrameCPU;
        return true;
    }

    return false;
}

void CameraManagerOpenVR::ServeFrames()
{
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
    LARGE_INTEGER startFrameRetrievalTime;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        if (m_bIsPaused) { continue; }

        bool bUseBlockQueue = m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForDepth || m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForColor;
        if (!m_bUseBlockQueue && bUseBlockQueue && !m_serveThreadBlockQueue.joinable())
        {
            m_serveThreadBlockQueue = std::thread(&CameraManagerOpenVR::ServeBlockQueueFrames, this);
        }
        m_bUseBlockQueue = bUseBlockQueue;

        if (m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForDepth && m_configManager->GetConfig_Camera().OpenVR_UseBlockQueueForColor)
        {
            continue;
        }
        
        // Wait for the old frame struct to be available in case someone is still reading from it.
        std::unique_lock writeLock(m_underConstructionFrame->readWriteMutex);

        std::unique_lock<std::shared_mutex> cpuFrameWriteLock;

        while (true)
        {
            startFrameRetrievalTime = StartPerfTimer();

            vr::EVRTrackedCameraFrameType frameType = m_configManager->GetConfig_Main().ProjectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, nullptr, 0, &m_underConstructionFrame->header, sizeof(vr::CameraVideoStreamFrameHeader_t));

            if (error == vr::VRTrackedCameraError_None)
            {
                float frameLatencyMS = GetPerfTimerDiff(m_underConstructionFrame->header.ulFrameExposureTime, startFrameRetrievalTime.QuadPart);

                if (frameLatencyMS > FRAME_TIMEOUT_MS) // We were served a too old frame to be useful.
                {
                    m_bWaitingForCamera = true;
                }
                else if (m_bWaitingForCamera) // Always accept the first frame offered if we were timed out.
                {
                    break;
                }
                else if (m_underConstructionFrame->header.nFrameSequence != lastFrameSequence) // Normal wait for the next frame.
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

        Config_Main& mainConf = m_configManager->GetConfig_Main();

        vr::EVRTrackedCameraFrameType frameType = mainConf.ProjectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

        if (m_renderAPI == RenderAPI_Direct3D11)
        {
            std::shared_ptr<IPassthroughRenderer> renderer = m_renderer.lock();

            if (!renderer.get())
            {
                continue;
            }

            std::shared_lock accessLock(renderer->GetAccessMutex(), std::try_to_lock);
            if (!accessLock.owns_lock())
            {
                continue;
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, frameType, renderer->GetRenderDevice(), &m_underConstructionFrame->frameTextureResource, nullptr, 0);
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
            dxgiRes->GetSharedHandle(&m_underConstructionFrame->frameTextureResource);
        }

        bool bDoFrameDump = m_configManager->CheckFrameTextureDumpPending();
        bool bWantsCPUFrameBuffer = bDoFrameDump ||
            (!m_bUseBlockQueue && mainConf.ProjectionMode == Projection_StereoReconstruction);
        bool bGotCPUFrame = false;

        if (bWantsCPUFrameBuffer)
        {
            // Construct here to allow unlocking outside the scope.
            cpuFrameWriteLock = std::unique_lock<std::shared_mutex>(m_underConstructionFrameCPU->ReadWriteMutex);

            // Get the CPU frame for depth reconstruction.
            if(m_appRenderAPI == RenderAPI_Direct3D11 || m_appRenderAPI == RenderAPI_Direct3D12)
            {
                if (m_underConstructionFrameCPU->FrameBuffer.get() == nullptr || m_underConstructionFrameCPU->FrameBuffer->size() < m_cameraFrameBufferSize)
                {
                    m_underConstructionFrameCPU->FrameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
                }

                vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, m_underConstructionFrameCPU->FrameBuffer->data(), (uint32_t)m_underConstructionFrameCPU->FrameBuffer->size(), nullptr, 0);
                if (error != vr::VRTrackedCameraError_None)
                {
                    g_logger->error("GetVideoStreamFrameBuffer error {}", static_cast<int32_t>(error));
                    continue;
                }

                bGotCPUFrame = true;
            }
            // GetVideoStreamFrameBuffer crashes when used with Vulkan and OpenGL apps,
            // for those APIs we manually copy it from the GPU texture instead.
            else if (m_renderAPI == RenderAPI_Direct3D11 && m_underConstructionFrame->frameTextureResource != nullptr)
            {
                if (m_underConstructionFrameCPU->FrameBuffer.get() == nullptr || m_underConstructionFrameCPU->FrameBuffer->size() < m_cameraFrameBufferSize)
                {
                    m_underConstructionFrameCPU->FrameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
                }

                std::shared_ptr<IPassthroughRenderer> renderer = m_renderer.lock();

                if (!renderer.get())
                {
                    continue;
                }

                std::shared_lock accessLock(renderer->GetAccessMutex(), std::try_to_lock);
                if (!accessLock.owns_lock())
                {
                    continue;
                }

                if (renderer->DownloadTextureToCPU(m_underConstructionFrame->frameTextureResource, m_underConstructionFrame->header.nWidth, m_underConstructionFrame->header.nHeight, (uint32_t)m_underConstructionFrameCPU->FrameBuffer->size(), m_underConstructionFrameCPU->FrameBuffer->data()))
                {
                    bGotCPUFrame = true;
                }
            }

            if (!bGotCPUFrame)
            {
                cpuFrameWriteLock.unlock();
            }
        }

        m_bWaitingForCamera = false;
        lastFrameSequence = m_underConstructionFrame->header.nFrameSequence;

        m_underConstructionFrame->bIsValid = true;
        m_underConstructionFrame->frameLayout = m_frameLayout;

        vr::TrackedDevicePose_t hmdPose;

        LARGE_INTEGER time, freq;
        QueryPerformanceCounter(&time);
        QueryPerformanceFrequency(&freq);

        float exposureRelativeTime = -(float)(time.QuadPart - m_underConstructionFrame->header.ulFrameExposureTime);
        exposureRelativeTime /= ((float)freq.QuadPart);

        m_openVRManager->GetVRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, exposureRelativeTime, &hmdPose, 1);

        if (!hmdPose.bPoseIsValid)
        {
            m_bPoseAvailable = false;
            continue;
        }
        m_bPoseAvailable = true;

        XrMatrix4x4f headToTrackingPose = ToXRMatrix4x4(hmdPose.mDeviceToAbsoluteTracking);

        XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldLeft, &headToTrackingPose, &m_cameraToHMDLeft);
        XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldRight, &headToTrackingPose, &m_cameraToHMDRight);


        // Camera projection matrix for Room View mode
        if (mainConf.ProjectionMode == Projection_RoomView2D)
        {
            XrMatrix4x4f worldToCameraProjectionLeft;
            XrMatrix4x4f_Multiply(&worldToCameraProjectionLeft, &m_underConstructionFrame->cameraViewToWorldLeft, &m_cameraProjectionInvLeft);
            XrMatrix4x4f_Invert(&m_underConstructionFrame->worldToCameraProjectionLeft, &worldToCameraProjectionLeft);

            XrMatrix4x4f worldToCameraProjectionRight;
            if (m_frameLayout != EStereoFrameLayout::FrameLayout_Mono)
            {
                XrMatrix4x4f_Multiply(&worldToCameraProjectionRight, &m_underConstructionFrame->cameraViewToWorldRight, &m_cameraProjectionInvRight);
            }
            else
            {
                XrMatrix4x4f_Multiply(&worldToCameraProjectionRight, &m_underConstructionFrame->cameraViewToWorldLeft, &m_cameraProjectionInvLeft);
            }
            XrMatrix4x4f_Invert(&m_underConstructionFrame->worldToCameraProjectionRight, &worldToCameraProjectionRight);
        }

        if (bGotCPUFrame)
        {
            m_underConstructionFrameCPU->bIsValid = true;
            m_underConstructionFrameCPU->bIsRaw = false;
            m_underConstructionFrameCPU->FrameExposureTimestamp = m_underConstructionFrame->header.ulFrameExposureTime;
            m_underConstructionFrameCPU->FrameLayout = m_frameLayout;
            m_underConstructionFrameCPU->FrameSequence = m_underConstructionFrame->header.nFrameSequence;
            m_underConstructionFrameCPU->FrameSize[0] = m_cameraTextureWidth;
            m_underConstructionFrameCPU->FrameSize[1] = m_cameraTextureHeight;
            m_underConstructionFrameCPU->CameraViewToWorldLeft = m_underConstructionFrame->cameraViewToWorldLeft;
            m_underConstructionFrameCPU->CameraViewToWorldRight = m_underConstructionFrame->cameraViewToWorldRight;

            if (bDoFrameDump)
            {
                DumpCameraFrameTexture(m_underConstructionFrameCPU->FrameBuffer, m_cameraTextureWidth, m_cameraTextureHeight, "OpenVR");
            }

            if(bWantsCPUFrameBuffer)
            {
                std::lock_guard<std::mutex> lock(m_serveMutexCPU);
                m_servedFrameCPU.swap(m_underConstructionFrameCPU);
            }
            cpuFrameWriteLock.unlock();
        }


        {
            std::lock_guard<std::mutex> lock(m_serveMutex);
            m_servedFrame.swap(m_underConstructionFrame);
        }

        m_averageFrameRetrievalTime = UpdateAveragePerfTime(m_frameRetrievalTimes, EndPerfTimer(startFrameRetrievalTime), 20);
    }
}



void CameraManagerOpenVR::ServeBlockQueueFrames()
{
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

    int32_t frameFormat = 0;
    int32_t rawFrameWidth = 0;
    int32_t rawFrameHeight = 0;

    {   
        vr::ETrackedPropertyError propError;

        vr::PathRead_t read = {};
        read.unRequiredBufferSize = 0;
        read.pszPath = nullptr;

        vrPaths->StringToHandle(&read.ulPath, "/format");
        read.pvBuffer = &frameFormat;
        read.unBufferSize = sizeof(frameFormat);
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
    LARGE_INTEGER startFrameRetrievalTime;
    vr::PropertyContainerHandle_t readHandle = 0;
    uint8_t* readBuffer = nullptr;
    bool bDoFrameDump = false;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(FRAME_POLL_INTERVAL);

        if (!m_bRunThread) { return; }

        if (m_bIsPaused || !m_bUseBlockQueue || m_configManager->GetConfig_Main().ProjectionMode != Projection_StereoReconstruction) { continue; }

        // Wait for the old frame struct to be available in case someone is still reading from it.
        std::unique_lock writeLock(m_underConstructionFrameCPU->ReadWriteMutex);
        std::shared_ptr<CameraCPUFrame>& frame = m_underConstructionFrameCPU;

        bool bGotFrame = false;

        while (true)
        {
            if (!m_bRunThread) { return; }

            if (m_bIsPaused || !m_bUseBlockQueue || m_configManager->GetConfig_Main().ProjectionMode != Projection_StereoReconstruction)
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
                startFrameRetrievalTime = StartPerfTimer();

                vr::ETrackedPropertyError propError;

                vr::PathRead_t read = {};
                read.unRequiredBufferSize = 0;
                read.pszPath = nullptr;

                read.ulPath = frameSequenceHandle;
                read.pvBuffer = &frame->FrameSequence;
                read.unBufferSize = sizeof(uint64_t);
                read.unTag = vr::k_unUint64PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    g_logger->error("Error reading /frame_sequence {}", static_cast<int32_t>(propError));
                }

                read.ulPath = serverTimeTicksHandle;
                read.pvBuffer = &frame->FrameExposureTimestamp;
                read.unBufferSize = sizeof(uint64_t);
                read.unTag = vr::k_unUint64PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    g_logger->error("Error reading /server_time_ticks {}", static_cast<int32_t>(propError));
                }

                read.ulPath = frameSizeHandle;
                read.pvBuffer = &frame->RawFrameDataBytes;
                read.unBufferSize = sizeof(int32_t);
                read.unTag = vr::k_unInt32PropertyTag;

                propError = vrPaths->ReadPathBatch(readHandle, &read, 1);
                if (propError != vr::TrackedProp_Success)
                {
                    frame->RawFrameDataBytes = 0;
                    g_logger->error("Error reading /frame_size {}", static_cast<int32_t>(propError));
                }


                float frameLatencyMS = GetPerfTimerDiff(frame->FrameExposureTimestamp, startFrameRetrievalTime.QuadPart);

                if (frameLatencyMS > FRAME_TIMEOUT_MS) // We were served a too old frame to be useful.
                {
                    bWaitingForCamera = true;
                }
                else if (bWaitingForCamera) // Always accept the first frame offered if we were timed out.
                {
                    bGotFrame = true;
                    break;
                }
                else if (frame->FrameSequence != lastFrameSequence) // Normal wait for the next frame.
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

            if (!m_bRunThread) { return; }
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


        if (frame->FrameBuffer.get() == nullptr || frame->FrameBuffer->size() < frame->RawFrameDataBytes)
        {
            frame->FrameBuffer = std::make_shared<std::vector<uint8_t>>(frame->RawFrameDataBytes);
        }

        memcpy(frame->FrameBuffer->data(), readBuffer, frame->RawFrameDataBytes);

        queueError = vrBlockQueue->ReleaseReadOnlyBlock(rawFrameQueue, readHandle);
        if (queueError != vr::EBlockQueueError_BlockQueueError_None)
        {
            g_logger->error("ReleaseReadOnlyBlock error {}", static_cast<int32_t>(queueError));
        }


        bWaitingForCamera = false;
        lastFrameSequence = frame->FrameSequence;

        frame->bIsValid = true;
        frame->bIsRaw = true;
        frame->FrameLayout = m_frameLayout;
        frame->RawFrameFormat = frameFormat;
        frame->FrameSize[0] = m_cameraTextureWidth;
        frame->FrameSize[1] = m_cameraTextureHeight;
        frame->RawFrameSize[0] = rawFrameWidth;
        frame->RawFrameSize[1] = rawFrameHeight;

        vr::TrackedDevicePose_t hmdPose;

        LARGE_INTEGER time, freq;
        QueryPerformanceCounter(&time);
        QueryPerformanceFrequency(&freq);

        float exposureRelativeTime = -(float)(time.QuadPart - frame->FrameExposureTimestamp);
        exposureRelativeTime /= ((float)freq.QuadPart);

        m_openVRManager->GetVRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, exposureRelativeTime, &hmdPose, 1);

        if (!hmdPose.bPoseIsValid)
        {
            continue;
        }

        XrMatrix4x4f headToTrackingPose = ToXRMatrix4x4(hmdPose.mDeviceToAbsoluteTracking);

        XrMatrix4x4f_Multiply(&frame->CameraViewToWorldLeft, &headToTrackingPose, &m_cameraToHMDLeft);
        XrMatrix4x4f_Multiply(&frame->CameraViewToWorldRight, &headToTrackingPose, &m_cameraToHMDRight);

        {
            std::lock_guard<std::mutex> lock(m_serveMutexCPU);

            m_servedFrameCPU.swap(m_underConstructionFrameCPU);
        }

        m_averageBlockQueueFrameRetrievalTime = UpdateAveragePerfTime(m_blockQueueFrameRetrievalTimes, EndPerfTimer(startFrameRetrievalTime), 20);
    }
}





void CameraManagerOpenVR::UpdateFrameProjectionMatrix(std::shared_ptr<CameraFrame>& frame)
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
