#include "pch.h"
#include "camera_manager.h"
#include <log.h>
#include "layer.h"


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






CameraManagerOpenVR::CameraManagerOpenVR(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
    : m_renderer(renderer)
    , m_renderAPI(renderAPI)
    , m_appRenderAPI(appRenderAPI)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameLayout(EStereoFrameLayout::Mono)
    , m_projectionDistanceFar(5.0f)
    , m_useAlternateProjectionCalc(false)
{
    m_renderFrame = std::make_shared<CameraFrame>();
    m_servedFrame = std::make_shared<CameraFrame>();
    m_underConstructionFrame = std::make_shared<CameraFrame>();
    m_renderModels = std::make_shared<std::vector<RenderModel>>();
}

CameraManagerOpenVR::~CameraManagerOpenVR()
{
    DeinitCamera();

    if (m_serveThread.joinable())
    {
        m_bRunThread = false;
        m_serveThread.join();
    }
}

bool CameraManagerOpenVR::InitCamera()
{
    if (m_bCameraInitialized) { return true; }

    m_hmdDeviceId = m_openVRManager->GetHMDDeviceId();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera) 
    {
        ErrorLog("SteamVR Tracked Camera interface error!\n");
        return false; 
    }

    bool bHasCamera = false;
    vr::EVRTrackedCameraError error = trackedCamera->HasCamera(m_hmdDeviceId, &bHasCamera);
    if (error != vr::VRTrackedCameraError_None)
    {
        ErrorLog("Error %i checking camera on device %i\n", error, m_hmdDeviceId);
        return false;
    }
    else if(!bHasCamera)
    {
        ErrorLog("No passthrough camera found!\n");
        return false;
    }

    UpdateStaticCameraParameters();

    vr::EVRTrackedCameraError cameraError = trackedCamera->AcquireVideoStreamingService(m_hmdDeviceId, &m_cameraHandle);

    if (cameraError != vr::VRTrackedCameraError_None)
    {
        Log("AcquireVideoStreamingService error %i on device %i\n", (int)cameraError, m_hmdDeviceId);
        return false;
    }

    m_bCameraInitialized = true;
    m_bRunThread = true;

    if (!m_serveThread.joinable())
    {
        m_serveThread = std::thread(&CameraManagerOpenVR::ServeFrames, this);
    }

    return true;
}

void CameraManagerOpenVR::DeinitCamera()
{
    if (!m_bCameraInitialized) { return; }
    m_bCameraInitialized = false;
    m_bRunThread = false;

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (trackedCamera)
    {
        vr::EVRTrackedCameraError error = trackedCamera->ReleaseVideoStreamingService(m_cameraHandle);

        if (error != vr::VRTrackedCameraError_None)
        {
            Log("ReleaseVideoStreamingService error %i\n", (int)error);
        }
    }

    if (m_serveThread.joinable())
    {
        m_serveThread.join();
    }
}

void CameraManagerOpenVR::GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, std::string& API) const
{
    if (m_bCameraInitialized)
    {
        width = m_cameraTextureWidth;
        height = m_cameraTextureHeight;
        fps = 0;
        API = "OpenVR - Active";
    }
    else
    {
        width = 0;
        height = 0;
        fps = 0;
        API = "OpenVR - Inactive";
    }
}

void CameraManagerOpenVR::GetDistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManagerOpenVR::GetUndistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraUndistortedTextureWidth;
    height = m_cameraUndistortedTextureHeight;
    bufferSize = m_cameraUndistortedFrameBufferSize;
}

void CameraManagerOpenVR::GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const
{
    Config_Camera& cameraConf = m_configManager->GetConfig_Camera();

    if (!cameraConf.OpenVRCustomCalibration)
    {
        uint32_t cameraIndex = 0;

        if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
        {
            cameraIndex = (cameraEye == LEFT_EYE) ? 0 : 1;
        }
        else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            // The camera indices are reversed for vertical layouts.
            cameraIndex = (cameraEye == LEFT_EYE) ? 1 : 0;
        }

        vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

        // OpenVR only provides camera inrinsics for the undistorted image dimesions for some reason.
        vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraIntrinsics(m_hmdDeviceId, cameraIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, (vr::HmdVector2_t*)&focalLength, (vr::HmdVector2_t*)&center);
        if (cameraError != vr::VRTrackedCameraError_None)
        {
            ErrorLog("GetCameraIntrinsics error %i on device Id %i\n", cameraError, m_hmdDeviceId);
        }

        // Multiply the values by the distorted/undistorted ratio, since we need the values for the distorted image.
        float xRatio = ((float)m_cameraFrameWidth) / m_cameraUndistortedFrameWidth;
        float yRatio = ((float)m_cameraFrameHeight) / m_cameraUndistortedFrameHeight;

        focalLength.x *= xRatio;
        focalLength.y *= yRatio;

        center.x *= xRatio;
        center.y *= yRatio;
    }
    else
    {
        if (m_frameLayout == EStereoFrameLayout::Mono || 
            (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout && cameraEye == LEFT_EYE) || 
            (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout && cameraEye == RIGHT_EYE))
        {
            focalLength.x = cameraConf.OpenVR_Camera0_IntrinsicsFocal[0] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
            focalLength.y = cameraConf.OpenVR_Camera0_IntrinsicsFocal[1] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
            center.x = cameraConf.OpenVR_Camera0_IntrinsicsCenter[0] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
            center.y = cameraConf.OpenVR_Camera0_IntrinsicsCenter[1] / (float)cameraConf.OpenVR_Camera0_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
        }
        else
        {
            focalLength.x = cameraConf.OpenVR_Camera1_IntrinsicsFocal[0] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
            focalLength.y = cameraConf.OpenVR_Camera1_IntrinsicsFocal[1] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
            center.x = cameraConf.OpenVR_Camera1_IntrinsicsCenter[0] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[0] * m_cameraTextureWidth;
            center.y = cameraConf.OpenVR_Camera1_IntrinsicsCenter[1] / (float)cameraConf.OpenVR_Camera1_IntrinsicsSensorPixels[1] * m_cameraTextureHeight;
        }

        if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            focalLength.y /= 2.0f;
            center.y /= 2.0f;
        }
        else if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
        {
            focalLength.x /= 2.0f;
            center.x /= 2.0f;
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
            ErrorLog("Failed to get tracked camera distortion coefficients, error [%i]\n", error);
        }
    }
    else
    {
        if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
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
    if (cameraConf.OpenVRCustomCalibration)
    {
        return cameraConf.OpenVR_CameraHasFisheyeLens;
    }
    return true;
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
            ErrorLog("Failed to get tracked camera pose array, error [%i]\n", error);
            bGotLeftCamera = false;
            bGotRightCamera = false;
        }

        if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
        {
            LeftPose = ToXRMatrix4x4(Buffer[0]);
            RightPose = ToXRMatrix4x4(Buffer[1]);
        }
        else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
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

        XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.OpenVR_Camera0_Translation[0], cameraConf.OpenVR_Camera0_Translation[1], cameraConf.OpenVR_Camera0_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.OpenVR_Camera0_Rotation[0], -cameraConf.OpenVR_Camera0_Rotation[1], -cameraConf.OpenVR_Camera0_Rotation[2]);

        XrMatrix4x4f_Multiply(&camera0Pose, &rotMatrix, &transMatrix);

        XrMatrix4x4f_CreateTranslation(&transMatrix, cameraConf.OpenVR_Camera1_Translation[0], cameraConf.OpenVR_Camera1_Translation[1], cameraConf.OpenVR_Camera1_Translation[2]);
        XrMatrix4x4f_CreateRotation(&rotMatrix, -cameraConf.OpenVR_Camera1_Rotation[0], -cameraConf.OpenVR_Camera1_Rotation[1], -cameraConf.OpenVR_Camera1_Rotation[2]);

        XrMatrix4x4f_Multiply(&camera1Pose, &rotMatrix, &transMatrix); 

        if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            XrMatrix4x4f_Invert(&LeftPose, &camera1Pose);
            XrMatrix4x4f_Invert(&RightPose, &camera0Pose);
        }
        else
        {
            XrMatrix4x4f_Invert(&LeftPose, &camera0Pose);
            XrMatrix4x4f_Invert(&RightPose, &camera1Pose);
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
        ErrorLog("CameraFrameSize error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }

    cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &m_cameraUndistortedTextureWidth, &m_cameraUndistortedTextureHeight, &m_cameraUndistortedFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        ErrorLog("CameraFrameSize error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }

    if (m_cameraUndistortedTextureWidth == 0 || m_cameraUndistortedTextureHeight == 0 || m_cameraUndistortedFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraUndistortedTextureWidth, m_cameraUndistortedTextureHeight, m_cameraUndistortedFrameBufferSize);
    }

    vr::TrackedPropertyError propError;

    int32_t layout = (vr::EVRTrackedCameraFrameLayout)vrSystem->GetInt32TrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraFrameLayout_Int32, &propError);

    if (propError != vr::TrackedProp_Success)
    {
        ErrorLog("GetTrackedCameraEyePoses error %i\n", propError);
    }

    if ((layout & vr::EVRTrackedCameraFrameLayout_Stereo) != 0)
    {
        if ((layout & vr::EVRTrackedCameraFrameLayout_VerticalLayout) != 0)
        {
            m_frameLayout = EStereoFrameLayout::StereoVerticalLayout;
            m_cameraFrameWidth = m_cameraTextureWidth;
            m_cameraFrameHeight = m_cameraTextureHeight / 2;

            m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth;
            m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight / 2;
        }
        else
        {
            m_frameLayout = EStereoFrameLayout::StereoHorizontalLayout;
            m_cameraFrameWidth = m_cameraTextureWidth / 2;
            m_cameraFrameHeight = m_cameraTextureHeight;

            m_cameraUndistortedFrameWidth = m_cameraUndistortedTextureWidth / 2;
            m_cameraUndistortedFrameHeight = m_cameraUndistortedTextureHeight;
        }
    }
    else
    {
        m_frameLayout = EStereoFrameLayout::Mono;
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

    GetTrackedCameraEyePoses(m_cameraToHMDLeft, m_cameraToHMDRight, false);
    XrMatrix4x4f_Invert(&m_HMDToCameraLeft, &m_cameraToHMDLeft);
    XrMatrix4x4f_Invert(&m_HMDToCameraRight, &m_cameraToHMDRight);

    XrMatrix4x4f camToHMDLeftOrig, camToHMDRightOrig;
    GetTrackedCameraEyePoses(camToHMDLeftOrig, camToHMDRightOrig, true);
    XrMatrix4x4f_Invert(&m_HMDToCameraLeftOriginal, &camToHMDLeftOrig);
    XrMatrix4x4f_Invert(&m_HMDToCameraRightOriginal, &camToHMDRightOrig);

    XrMatrix4x4f_Multiply(&m_cameraLeftToRightPose, &m_cameraToHMDLeft, &m_HMDToCameraRight);
    XrMatrix4x4f_Multiply(&m_cameraRightToLeftPose, &m_cameraToHMDRight, &m_HMDToCameraLeft);
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

void CameraManagerOpenVR::ServeFrames()
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera)
    {
        return;
    }

    ComPtr<ID3D11Device> d3dInteropDevice;

    if (m_renderAPI == Vulkan)
    {
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &d3dInteropDevice, NULL, NULL);
    }

    bool bHasFrame = false;
    uint32_t lastFrameSequence = 0;
    LARGE_INTEGER startFrameRetrievalTime;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        // Wait for the old frame struct to be available in case someone is still reading from it.
        std::unique_lock writeLock(m_underConstructionFrame->readWriteMutex);

        while (true)
        {
            startFrameRetrievalTime = StartPerfTimer();

            vr::EVRTrackedCameraFrameType frameType = m_configManager->GetConfig_Main().ProjectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, nullptr, 0, &m_underConstructionFrame->header, sizeof(vr::CameraVideoStreamFrameHeader_t));

            if (error == vr::VRTrackedCameraError_None)
            {
                if (!bHasFrame)
                {
                    break;
                }
                else if (m_underConstructionFrame->header.nFrameSequence != lastFrameSequence)
                {
                    break;
                }
            }
            else if (error != vr::VRTrackedCameraError_NoFrameAvailable)
            {
                ErrorLog("GetVideoStreamFrameBuffer-header error %i\n", error);
            }

            if (!m_bRunThread) { return; }

            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);

            if (!m_bRunThread) { return; }
        }

        if (!m_bRunThread) { return; }

        Config_Main& mainConf = m_configManager->GetConfig_Main();

        vr::EVRTrackedCameraFrameType frameType = mainConf.ProjectionMode == Projection_RoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

        if (m_renderAPI == DirectX11)
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
                ErrorLog("GetVideoStreamTextureD3D11 error %i\n", error);
                continue;
            }
        }
        else if (m_renderAPI == Vulkan)
        {
            ID3D11ShaderResourceView* srv;

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, frameType, d3dInteropDevice.Get(), (void**)&srv, nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamTextureD3D11 error %i\n", error);
                continue;
            }

            ComPtr<IDXGIResource> dxgiRes;
            ID3D11Resource* res;
            srv->GetResource(&res);
            res->QueryInterface(IID_PPV_ARGS(&dxgiRes));
            dxgiRes->GetSharedHandle(&m_underConstructionFrame->frameTextureResource);
        }

        m_underConstructionFrame->bHasFrameBuffer = false;

        if(m_renderAPI == DirectX12 || 
            (mainConf.ProjectionMode == Projection_StereoReconstruction && m_appRenderAPI != Vulkan))
        {
            if (m_underConstructionFrame->frameBuffer.get() == nullptr)
            {
                m_underConstructionFrame->frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, frameType, m_underConstructionFrame->frameBuffer->data(), (uint32_t)m_underConstructionFrame->frameBuffer->size(), nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamFrameBuffer error %i\n", error);
                continue;
            }

            m_underConstructionFrame->bHasFrameBuffer = true;
        }
        else if (mainConf.ProjectionMode == Projection_StereoReconstruction && m_renderAPI == DirectX11 && m_appRenderAPI == Vulkan && m_underConstructionFrame->frameTextureResource != nullptr)
        {
            if (m_underConstructionFrame->frameBuffer.get() == nullptr)
            {
                m_underConstructionFrame->frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            m_underConstructionFrame->bHasFrameBuffer = false;

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

            // Since SteamVR still crashes when using GetVideoStreamFrameBuffer under Vulkan, we manually download the image. This is very slow.
            if (renderer->DownloadTextureToCPU(m_underConstructionFrame->frameTextureResource, m_underConstructionFrame->header.nWidth, m_underConstructionFrame->header.nHeight, (uint32_t)m_underConstructionFrame->frameBuffer->size(), m_underConstructionFrame->frameBuffer->data()))
            {
                m_underConstructionFrame->bHasFrameBuffer = true;
            }
        }

        bHasFrame = true;
        lastFrameSequence = m_underConstructionFrame->header.nFrameSequence;

        m_underConstructionFrame->bIsValid = true;
        m_underConstructionFrame->frameLayout = m_frameLayout;

        if (mainConf.ProjectToRenderModels)
        {
            UpdateRenderModels();
        }
        m_underConstructionFrame->renderModels = m_renderModels;

        // Apply offset calibration to camera positions.
        XrMatrix4x4f origCamera0ToTrackingPose = ToXRMatrix4x4(m_underConstructionFrame->header.trackedDevicePose.mDeviceToAbsoluteTracking);
        XrMatrix4x4f headToTrackingPose, correctedCamera0ToHMDPose;

        // For vertical layouts the device pose is for the right camera.
        if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            if (m_configManager->GetConfig_Camera().OpenVRCustomCalibration)
            {
                // Strip original OpenVR calibration from pose if custom calibration used.
                XrMatrix4x4f_Multiply(&headToTrackingPose, &origCamera0ToTrackingPose, &m_HMDToCameraRightOriginal);
            }
            else
            {
                XrMatrix4x4f_Multiply(&headToTrackingPose, &origCamera0ToTrackingPose, &m_HMDToCameraRight);
            }
            correctedCamera0ToHMDPose = m_cameraToHMDRight;
            correctedCamera0ToHMDPose.m[12] *= mainConf.DepthOffsetCalibration;
            correctedCamera0ToHMDPose.m[13] *= mainConf.DepthOffsetCalibration;
            correctedCamera0ToHMDPose.m[14] *= mainConf.DepthOffsetCalibration;
            XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldRight, &headToTrackingPose, &correctedCamera0ToHMDPose);

            XrMatrix4x4f leftToRightPose = m_cameraLeftToRightPose;
            leftToRightPose.m[12] *= mainConf.DepthOffsetCalibration;
            leftToRightPose.m[13] *= mainConf.DepthOffsetCalibration;
            leftToRightPose.m[14] *= mainConf.DepthOffsetCalibration;

            XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldLeft, &m_underConstructionFrame->cameraViewToWorldRight, &leftToRightPose);
        }
        else
        {
            if (m_configManager->GetConfig_Camera().OpenVRCustomCalibration)
            {
                // Strip original OpenVR calibration from pose if custom calibration used.
                XrMatrix4x4f_Multiply(&headToTrackingPose, &origCamera0ToTrackingPose, &m_HMDToCameraLeftOriginal);
            }
            else
            {
                XrMatrix4x4f_Multiply(&headToTrackingPose, &origCamera0ToTrackingPose, &m_HMDToCameraLeft);
            }
            correctedCamera0ToHMDPose = m_cameraToHMDLeft;
            correctedCamera0ToHMDPose.m[12] *= mainConf.DepthOffsetCalibration;
            correctedCamera0ToHMDPose.m[13] *= mainConf.DepthOffsetCalibration;
            correctedCamera0ToHMDPose.m[14] *= mainConf.DepthOffsetCalibration;
            XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldLeft, &headToTrackingPose, &correctedCamera0ToHMDPose);

            XrMatrix4x4f rightToLeftPose = m_cameraRightToLeftPose;
            rightToLeftPose.m[12] *= mainConf.DepthOffsetCalibration;
            rightToLeftPose.m[13] *= mainConf.DepthOffsetCalibration;
            rightToLeftPose.m[14] *= mainConf.DepthOffsetCalibration;

            XrMatrix4x4f_Multiply(&m_underConstructionFrame->cameraViewToWorldRight, &m_underConstructionFrame->cameraViewToWorldLeft, &rightToLeftPose);
        }

        {
            std::lock_guard<std::mutex> lock(m_serveMutex);

            m_servedFrame.swap(m_underConstructionFrame);
        }

        m_averageFrameRetrievalTime = UpdateAveragePerfTime(m_frameRetrievalTimes, EndPerfTimer(startFrameRetrievalTime), 20);
    }
}


void CameraManagerOpenVR::UpdateRenderModels()
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
XrMatrix4x4f CameraManagerOpenVR::GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
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
        XrMatrix4x4f_Multiply(&output,  &viewToTracking, &pose);
    }
    else
    {
        XrMatrix4x4f_Multiply(&output, &viewToTracking, &refSpacePose);
    }

    return output;
}

void CameraManagerOpenVR::UpdateProjectionMatrix(std::shared_ptr<CameraFrame>& frame)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    if (mainConf.ProjectionDistanceFar * 1.5f != m_projectionDistanceFar)
    {
        // Push back far plane by 1.5x to account for the flat projection plane of the passthrough frame.
        m_projectionDistanceFar = mainConf.ProjectionDistanceFar * 1.5f;

        // The camera indices are reversed for vertical layouts.
        uint32_t leftIndex = (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout) ? 1 : 0;

        vr::HmdMatrix44_t vrProjection;
        vr::EVRTrackedCameraError error = trackedCamera->GetCameraProjection(m_hmdDeviceId, leftIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

        if (error != vr::VRTrackedCameraError_None)
        {
            ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
            return;
        }

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

        XrMatrix4x4f projectionMatrix = ToXRMatrix4x4Inverted(vrProjection);
        XrMatrix4x4f_Multiply(&m_cameraProjectionInvFarLeft, &projectionMatrix, &transMatrix);

        if (bIsStereo)
        {
            // The camera indices are reversed for vertical layouts.
            uint32_t rightIndex = (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout) ? 0 : 1;

            error = trackedCamera->GetCameraProjection(m_hmdDeviceId, rightIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
                return;
            }


            XrMatrix4x4f projectionMatrix = ToXRMatrix4x4Inverted(vrProjection);
            XrMatrix4x4f_Multiply(&m_cameraProjectionInvFarRight, &projectionMatrix, &transMatrix);

        }
    }
}


void CameraManagerOpenVR::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, std::shared_ptr<DepthFrame> depthFrame, const XrCompositionLayerProjection& layer, float timeToPhotons, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    UpdateProjectionMatrix(frame);

    CalculateFrameProjectionForEye(LEFT_EYE, frame, layer, refSpaceInfo, distortionParams);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, layer, refSpaceInfo, distortionParams);

    // Detect the FOV being upside-down in order to prevent triangles from being backface culled
    frame->bIsRenderingMirrored = (layer.views[0].fov.angleUp - layer.views[0].fov.angleDown) < 0.0f;

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

void CameraManagerOpenVR::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t cameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    XrMatrix4x4f hmdWorldToView = GetHMDWorldToViewMatrix(eye, layer, refSpaceInfo);
    
    XrVector3f* projectionOriginWorld = (eye == LEFT_EYE) ? &frame->projectionOriginWorldLeft : &frame->projectionOriginWorldRight;
    XrMatrix4x4f hmdViewToWorld;
    XrMatrix4x4f_Invert(&hmdViewToWorld, &hmdWorldToView);
    XrVector3f inPos{ 0,0,0 };
    XrMatrix4x4f_TransformVector3f(projectionOriginWorld, &hmdViewToWorld, &inPos);


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
    if (depthInfo && (farZ == (std::numeric_limits<float>::max)() || !std::isfinite(farZ) ))
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



    if (mainConf.ProjectionMode == Projection_RoomView2D)
    {
        if (eye == LEFT_EYE)
        {

            XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldLeft, &frame->cameraViewToWorldLeft, &m_cameraProjectionInvFarLeft);

            XrMatrix4x4f_Invert(&frame->worldToCameraProjectionLeft, &frame->cameraProjectionToWorldLeft);
        }
        else
        {
            if (bIsStereo)
            {
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &frame->cameraViewToWorldRight, &m_cameraProjectionInvFarRight);
            }
            else
            {
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &frame->cameraViewToWorldLeft, &m_cameraProjectionInvFarLeft);
            }

            XrMatrix4x4f_Invert(&frame->worldToCameraProjectionRight, &frame->cameraProjectionToWorldRight);
        }
    }
    else
    {
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
}
