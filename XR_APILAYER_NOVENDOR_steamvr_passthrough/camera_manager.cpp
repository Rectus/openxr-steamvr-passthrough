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



CameraManager::CameraManager(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
    : m_renderer(renderer)
    , m_renderAPI(renderAPI)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameType(vr::VRTrackedCameraFrameType_MaximumUndistorted)
    , m_frameLayout(EStereoFrameLayout::Mono)
    , m_projectionDistanceFar(5.0f)
{
    m_renderFrame = std::make_shared<CameraFrame>();
    m_servedFrame = std::make_shared<CameraFrame>();
    m_underConstructionFrame = std::make_shared<CameraFrame>();
}

CameraManager::~CameraManager()
{
    DeinitCamera();

    if (m_serveThread.joinable())
    {
        m_bRunThread = false;
        m_serveThread.join();
    }
}

bool CameraManager::InitCamera()
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
        m_serveThread = std::thread(&CameraManager::ServeFrames, this);
    }

    return true;
}

void CameraManager::DeinitCamera()
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

void CameraManager::GetFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize)
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManager::GetTrackedCameraEyePoses(XrMatrix4x4f& LeftPose, XrMatrix4x4f& RightPose)
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

    vr::HmdMatrix34_t Buffer[2];
    vr::TrackedPropertyError error;
    bool bGotLeftCamera = true;
    bool bGotRightCamera = true;

    uint32_t numBytes = vrSystem->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &Buffer, sizeof(Buffer), &error);
    if (error != vr::TrackedProp_Success || numBytes == 0)
    {
        ErrorLog("Failed to get tracked camera pose array, error [%i]\n",error);
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

void CameraManager::UpdateStaticCameraParameters()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, m_frameType, &m_cameraTextureWidth, &m_cameraTextureHeight, &m_cameraFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        ErrorLog("CameraFrameSize error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
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
        }
        else
        {
            m_frameLayout = EStereoFrameLayout::StereoHorizontalLayout;
        }
    }
    else
    {
        m_frameLayout = EStereoFrameLayout::Mono;
    }

    vr::HmdMatrix44_t vrHMDProjectionLeft = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Left, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionLeft = ToXRMatrix4x4(vrHMDProjectionLeft);

    vr::HmdMatrix34_t vrHMDViewLeft = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Left);
    m_rawHMDViewLeft = ToXRMatrix4x4Inverted(vrHMDViewLeft);

    vr::HmdMatrix44_t vrHMDProjectionRight = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Right, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionRight = ToXRMatrix4x4(vrHMDProjectionRight);

    vr::HmdMatrix34_t vrHMDViewRight = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Right);
    m_rawHMDViewRight = ToXRMatrix4x4Inverted(vrHMDViewRight);

    XrMatrix4x4f LeftCameraPose, RightCameraPose;
    GetTrackedCameraEyePoses(LeftCameraPose, RightCameraPose);

    m_cameraLeftToHMDPose = LeftCameraPose;

    XrMatrix4x4f LeftCameraPoseInv;
    XrMatrix4x4f_Invert(&LeftCameraPoseInv, &LeftCameraPose);
    XrMatrix4x4f_Multiply(&m_cameraLeftToRightPose, &LeftCameraPoseInv, &RightCameraPose);
}

bool CameraManager::GetCameraFrame(std::shared_ptr<CameraFrame>& frame)
{
    if (!m_bCameraInitialized) { return false; }

    std::unique_lock<std::mutex> lock(m_serveMutex, std::try_to_lock);
    if (lock.owns_lock() && m_servedFrame->bIsValid)
    {
        m_renderFrame->bIsValid = false;
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

void CameraManager::ServeFrames()
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera)
    {
        return;
    }

    bool bHasFrame = false;
    uint32_t lastFrameSequence = 0;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        while (true)
        {
            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, nullptr, 0, &m_underConstructionFrame->header, sizeof(vr::CameraVideoStreamFrameHeader_t));

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


        if (m_renderAPI == DirectX11)
        {
            std::shared_ptr<IPassthroughRenderer> renderer = m_renderer.lock();

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, m_frameType, renderer->GetRenderDevice(), &m_underConstructionFrame->frameTextureResource, nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamTextureD3D11 error %i\n", error);
                continue;
            }
        }
        else
        {
            if (m_underConstructionFrame->frameBuffer.get() == nullptr)
            {
                m_underConstructionFrame->frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, m_underConstructionFrame->frameBuffer->data(), (uint32_t)m_underConstructionFrame->frameBuffer->size(), nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamFrameBuffer error %i\n", error);
                continue;
            }
        }

        bHasFrame = true;
        lastFrameSequence = m_underConstructionFrame->header.nFrameSequence;

        m_underConstructionFrame->bIsValid = true;
        m_underConstructionFrame->frameLayout = m_frameLayout;

        {
            std::lock_guard<std::mutex> lock(m_serveMutex);

            m_servedFrame.swap(m_underConstructionFrame);
        }
    }
}


// Constructs a matrix from the roomscale origin to the HMD eye pose.
XrMatrix4x4f CameraManager::GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
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

        XrMatrix4x4f_Multiply(&pose, &viewToTracking, &trackingToStage);
        XrMatrix4x4f_Multiply(&output, &pose, &refSpacePose);
    }
    else
    {
        XrMatrix4x4f_Multiply(&output, &viewToTracking, &refSpacePose);
    }

    return output;
}

void CameraManager::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrTime& displayTime, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    // Push back far plane by 1.2x to account for the flat projection place of the passthrough frame.
    if (m_configManager->GetConfig_Main().ProjectionDistanceFar * 1.2f != m_projectionDistanceFar)
    {
        m_projectionDistanceFar = m_configManager->GetConfig_Main().ProjectionDistanceFar * 1.2f;

        vr::HmdMatrix44_t vrProjection;
        vr::EVRTrackedCameraError error= trackedCamera->GetCameraProjection(m_hmdDeviceId, 0, m_frameType, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

        if (error != vr::VRTrackedCameraError_None)
        {
            ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
            return;
        }

        // Hack to scale the projection to one half of the stereo frame.
        if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
        {
            vrProjection.m[0][0] *= 2.0f;
            vrProjection.m[0][2] -= 0.5f;
        }
        else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
        {
            vrProjection.m[1][1] *= 2.0f;
            vrProjection.m[1][2] -= 0.5f;
        }

        m_cameraProjectionInvFarLeft = ToXRMatrix4x4Inverted(vrProjection);

        if (m_frameLayout != EStereoFrameLayout::Mono)
        {
            error = trackedCamera->GetCameraProjection(m_hmdDeviceId, 1, m_frameType, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
                return;
            }

            // Hack to scale the projection to one half of the stereo frame.
            if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
            {
                vrProjection.m[0][0] *= 2.0f;
                vrProjection.m[0][2] -= 0.5f;
            }
            else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
            {
                vrProjection.m[1][1] *= 2.0f;
                vrProjection.m[1][2] -= 0.5f;
            }

            m_cameraProjectionInvFarRight = ToXRMatrix4x4Inverted(vrProjection);
        }
    }

    CalculateFrameProjectionForEye(LEFT_EYE, frame, layer, refSpaceInfo);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, layer, refSpaceInfo);
}

void CameraManager::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t CameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    XrMatrix4x4f* worldToHMDMatrix = (eye == LEFT_EYE) ? &frame->hmdWorldToProjectionLeft : &frame->hmdWorldToProjectionRight;
    XrMatrix4x4f* trackedCameraMatrix = (eye == LEFT_EYE) ? &frame->cameraToWorldLeft : &frame->cameraToWorldRight;
    XrVector3f* hmdWorldPos = (eye == LEFT_EYE) ? &frame->hmdViewPosWorldLeft : &frame->hmdViewPosWorldRight;
    
    XrMatrix4x4f hmdWorldToView = GetHMDWorldToViewMatrix(eye, layer, refSpaceInfo);

    XrMatrix4x4f hmdViewToWorld;
    XrMatrix4x4f_Invert(&hmdViewToWorld, &hmdWorldToView);
    XrVector3f inPos{ 0,0,0 };
    XrMatrix4x4f_TransformVector3f(hmdWorldPos, &hmdViewToWorld, &inPos);

    XrMatrix4x4f hmdProjection;
    XrMatrix4x4f_CreateProjectionFov(&hmdProjection, GRAPHICS_D3D, layer.views[(eye == LEFT_EYE) ? 0 : 1].fov, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar);

    XrMatrix4x4f leftCameraToTrackingPose = ToXRMatrix4x4(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

    if (CameraId == 0)
    {
        XrMatrix4x4f_Multiply(trackedCameraMatrix, &leftCameraToTrackingPose, &m_cameraProjectionInvFarLeft);

        XrMatrix4x4f_Multiply(worldToHMDMatrix, &hmdProjection, &hmdWorldToView);
    }
    else
    {
        XrMatrix4x4f tempMatrix;
        XrMatrix4x4f_Multiply(&tempMatrix, &leftCameraToTrackingPose, &m_cameraLeftToRightPose);
        XrMatrix4x4f_Multiply(trackedCameraMatrix, &tempMatrix, &m_cameraProjectionInvFarRight);

        XrMatrix4x4f_Multiply(worldToHMDMatrix, &hmdProjection, &hmdWorldToView);
    }
}
