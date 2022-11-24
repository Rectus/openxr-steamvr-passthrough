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



CameraManager::CameraManager(std::shared_ptr<IPassthroughRenderer> renderer, std::shared_ptr<ConfigManager> configManager)
    : m_renderer(renderer)
    , m_configManager(configManager)
    , m_trackedCamera(nullptr)
    , m_frameType(vr::VRTrackedCameraFrameType_MaximumUndistorted)
    , m_frameLayout(EStereoFrameLayout::Mono)
{
    m_projectionDistanceFar = m_configManager->GetConfig_Main().ProjectionDistanceFar;
    m_projectionDistanceNear = m_configManager->GetConfig_Main().ProjectionDistanceNear;
}

CameraManager::~CameraManager()
{
    DeinitCamera();
}

bool CameraManager::InitRuntime()
{
    if (!vr::VR_IsRuntimeInstalled())
    {
        ErrorLog("SteamVR installation not detected!\n");
        return false;
    }

    vr::EVRInitError error;
    vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Background);

    if (error != vr::EVRInitError::VRInitError_None)
    {
        ErrorLog("Failed to init SteamVR runtime as background app, error %i\n", error);
        return false;
    }

    if (!vr::VRSystem() || !vr::VRCompositor())
    {
        ErrorLog("Invalid SteamVR interface handle!\n");
        vr::VR_Shutdown();
        return false;
    }

    for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
    {
        if (vr::VRSystem()->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_HMD)
        {
            m_hmdDeviceId = i;
            break;
        }
    }

    if (m_hmdDeviceId < 0)
    {
        ErrorLog("HMD device ID not found!\n");
        vr::VR_Shutdown();
        return false;
    }

    m_bRuntimeInitialized = true;
    return true;
}

void CameraManager::DeinitRuntime()
{
    if (!m_bRuntimeInitialized) { return; }

    if (m_bCameraInitialized)
    {
        DeinitCamera();
    }

    vr::VR_Shutdown();
}

bool CameraManager::InitCamera()
{
    if (m_bCameraInitialized) { return true; }

    m_trackedCamera = vr::VRTrackedCamera();

    if (!m_trackedCamera)
    {
        ErrorLog("SteamVR Tracked Camera interface error!\n");
        return false;
    }

    bool bHasCamera = false;
    vr::EVRTrackedCameraError error = m_trackedCamera->HasCamera(m_hmdDeviceId, &bHasCamera);
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

    vr::EVRTrackedCameraError cameraError = m_trackedCamera->AcquireVideoStreamingService(m_hmdDeviceId, &m_cameraHandle);

    if (cameraError != vr::VRTrackedCameraError_None)
    {
        Log("AcquireVideoStreamingService error %i on device %i\n", (int)cameraError, m_hmdDeviceId);
        return false;
    }

    m_bCameraInitialized = true;

    m_serveThread = std::thread(&CameraManager::ServeFrames, this);

    return true;
}

void CameraManager::DeinitCamera()
{
    if (!m_bCameraInitialized) { return; }
    m_bCameraInitialized = false;
    
    if (m_serveThread.joinable())
    {
        m_bRunThread = false;
        m_serveThread.join();
    }

    vr::IVRTrackedCamera* vrCamera = vr::VRTrackedCamera();

    if (vrCamera)
    {
        vr::EVRTrackedCameraError error = vrCamera->ReleaseVideoStreamingService(m_cameraHandle);

        if (error != vr::VRTrackedCameraError_None)
        {
            Log("ReleaseVideoStreamingService error %i\n", (int)error);
        }
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
    if (!vr::VRSystem())
    {
        return;
    }

    vr::HmdMatrix34_t Buffer[2];
    vr::TrackedPropertyError error;
    bool bGotLeftCamera = true;
    bool bGotRightCamera = true;

    uint32_t numBytes = vr::VRSystem()->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &Buffer, sizeof(Buffer), &error);
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
        //XrMatrix4x4f_CreateIdentity(&RightPose);
    }
}

void CameraManager::UpdateStaticCameraParameters()
{
    vr::EVRTrackedCameraError cameraError = m_trackedCamera->GetCameraFrameSize(m_hmdDeviceId, m_frameType, &m_cameraTextureWidth, &m_cameraTextureHeight, &m_cameraFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        ErrorLog("CameraFrameSize error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }

    vr::TrackedPropertyError propError;

    int32_t layout = (vr::EVRTrackedCameraFrameLayout)vr::VRSystem()->GetInt32TrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraFrameLayout_Int32, &propError);

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

    vr::HmdMatrix44_t vrHMDProjectionLeft = vr::VRSystem()->GetProjectionMatrix(vr::Hmd_Eye::Eye_Left, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionLeft = ToXRMatrix4x4(vrHMDProjectionLeft);

    vr::HmdMatrix34_t vrHMDViewLeft = vr::VRSystem()->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Left);
    m_rawHMDViewLeft = ToXRMatrix4x4Inverted(vrHMDViewLeft);

    vr::HmdMatrix44_t vrHMDProjectionRight = vr::VRSystem()->GetProjectionMatrix(vr::Hmd_Eye::Eye_Right, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionRight = ToXRMatrix4x4(vrHMDProjectionRight);

    vr::HmdMatrix34_t vrHMDViewRight = vr::VRSystem()->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Right);
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

    if (m_serveMutex.try_lock())
    {
        if (m_servedFrame.get() != nullptr)
        {
            m_renderFrame = m_servedFrame;
            m_serveMutex.unlock();

            frame = m_renderFrame;
            return true;
        }
        else
        {
            m_serveMutex.unlock();
            return false;
        }
    }
    else if (m_renderFrame.get() != nullptr)
    {
        frame = m_renderFrame;
        return true;
    }

    return false;
}

void CameraManager::ServeFrames()
{
    bool bHasFrame = false;
    uint32_t lastFrameSequence = 0;
    vr::CameraVideoStreamFrameHeader_t frameHeader;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);
        while (true)
        {
            vr::EVRTrackedCameraError error = m_trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, nullptr, 0, &frameHeader, sizeof(vr::CameraVideoStreamFrameHeader_t));

            if (error == vr::VRTrackedCameraError_None)
            {
                if (!bHasFrame)
                {
                    break;
                }
                else if (frameHeader.nFrameSequence != lastFrameSequence)
                {
                    break;
                }
            }
            else if (error != vr::VRTrackedCameraError_NoFrameAvailable)
            {
                ErrorLog("GetVideoStreamFrameBuffer error %i\n", error);
            }

            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);
        }

        bool bFoundframeRes = false;
        std::shared_ptr<std::vector<uint8_t>> frameBuffer;
        void* frameTexRes = nullptr;
        std::shared_ptr<IPassthroughRenderer> r = m_renderer.lock();

        if (r && r->GetCameraFrameResource(&frameTexRes, m_cameraHandle, m_frameType))
        {
            bFoundframeRes = true;
        }
        else
        {
            frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);

            vr::EVRTrackedCameraError error = m_trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, frameBuffer->data(), (uint32_t)frameBuffer->size(), nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamFrameBuffer error %i\n", error);
                continue;
            }
        }

        bHasFrame = true;
        lastFrameSequence = frameHeader.nFrameSequence;

        m_serveMutex.lock();

        m_servedFrame = std::make_shared<CameraFrame>();
        memcpy(&m_servedFrame->header, &frameHeader, sizeof(frameHeader));

        if (bFoundframeRes)
        {
            m_servedFrame->frameTextureResource = frameTexRes;
        }
        else
        {
            m_servedFrame->frameBuffer = frameBuffer;
        }
        XrMatrix4x4f_CreateIdentity(&m_servedFrame->frameUVProjectionLeft);
        XrMatrix4x4f_CreateIdentity(&m_servedFrame->frameUVProjectionRight);
        m_servedFrame->frameLayout = m_frameLayout;

        m_serveMutex.unlock();
    }
}


// Constructs a matrix from the roomscale origin to the HMD eye projection.
XrMatrix4x4f CameraManager::GetHMDMVPMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    XrMatrix4x4f output, pose, viewToTracking, projection, viewToStage, trackingToStage, refSpacePose;

    int viewNum = eye == LEFT_EYE ? 0 : 1;

    XrVector3f scale = { 1, 1, 1 };

    // The application provided HMD pose used to make sure reprojection works correctly.
    XrMatrix4x4f_CreateTranslationRotationScale(&pose, &layer.views[viewNum].pose.position, &layer.views[viewNum].pose.orientation, &scale);
    XrMatrix4x4f_Invert(&viewToTracking, &pose);

    // Apply any pose the application might have configured in its reference spaces.
    XrMatrix4x4f_CreateTranslationRotationScale(&pose, &refSpaceInfo.poseInReferenceSpace.position, &refSpaceInfo.poseInReferenceSpace.orientation, &scale);
    XrMatrix4x4f_Invert(&refSpacePose, &pose);

    XrMatrix4x4f_CreateProjectionFov(&projection, GRAPHICS_D3D, layer.views[viewNum].fov, m_projectionDistanceFar * 0.5f, m_projectionDistanceFar);

    if (refSpaceInfo.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
    {
        vr::HmdMatrix34_t mat = vr::VRSystem()->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();
        trackingToStage = ToXRMatrix4x4Inverted(mat);

        XrMatrix4x4f_Multiply(&pose, &viewToTracking, &trackingToStage);
        XrMatrix4x4f_Multiply(&viewToStage, &pose, &refSpacePose);
    }
    else
    {
        XrMatrix4x4f_Multiply(&viewToStage, &viewToTracking, &refSpacePose);
    }
    
    XrMatrix4x4f_Multiply(&output, &projection, &viewToStage);

    return output;
}

void CameraManager::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrTime& displayTime, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    m_projectionDistanceFar = m_configManager->GetConfig_Main().ProjectionDistanceFar;
    m_projectionDistanceNear = m_configManager->GetConfig_Main().ProjectionDistanceNear;

    CalculateFrameProjectionForEye(LEFT_EYE, frame, layer, refSpaceInfo);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, layer, refSpaceInfo);
}

void CameraManager::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t CameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    XrMatrix4x4f* frameUVMat = (eye == LEFT_EYE) ? &frame->frameUVProjectionLeft : &frame->frameUVProjectionRight;
    vr::HmdMatrix44_t vrProjection;

    vr::EVRTrackedCameraError error = m_trackedCamera->GetCameraProjection(m_hmdDeviceId, CameraId, m_frameType, m_projectionDistanceFar * 0.5f, m_projectionDistanceFar, &vrProjection);

    if (error != vr::VRTrackedCameraError_None)
    {
        ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
        XrMatrix4x4f_CreateIdentity(frameUVMat);
        return;
    }

    XrMatrix4x4f hmdMVPMatrix = GetHMDMVPMatrix(eye, layer, refSpaceInfo);
    XrMatrix4x4f cameraProjectionInv = ToXRMatrix4x4Inverted(vrProjection);
    XrMatrix4x4f leftCameraToTrackingPose = ToXRMatrix4x4(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

    XrMatrix4x4f tempMatrix, trackedCameraMatrix, transformToCamera;

    if (CameraId == 0)
    {
        XrMatrix4x4f_Multiply(&trackedCameraMatrix, &leftCameraToTrackingPose, &cameraProjectionInv);
        XrMatrix4x4f_Multiply(&transformToCamera, &hmdMVPMatrix, &trackedCameraMatrix);
    }
    else
    {
        XrMatrix4x4f_Multiply(&tempMatrix, &leftCameraToTrackingPose, &m_cameraLeftToRightPose);
        XrMatrix4x4f_Multiply(&trackedCameraMatrix, &tempMatrix, &cameraProjectionInv);
        XrMatrix4x4f_Multiply(&transformToCamera, &hmdMVPMatrix, &trackedCameraMatrix);
    }

    // Calculate matrix for transforming the clip space quad to the quad output by the camera transform
    // as per: https://mrl.cs.nyu.edu/~dzorin/ug-graphics/lectures/lecture7/

    XrVector4f P1 = { -1, -1, 1, 1 };
    XrVector4f P2 = { 1, -1, 1, 1 };
    XrVector4f P3 = { 1, 1, 1, 1 };
    XrVector4f P4 = { -1, 1, 1, 1 };

    XrVector4f Q1;
    XrVector4f Q2;
    XrVector4f Q3;
    XrVector4f Q4;
    XrMatrix4x4f_TransformVector4f(&Q1, &transformToCamera, &P1);
    XrMatrix4x4f_TransformVector4f(&Q2, &transformToCamera, &P2);
    XrMatrix4x4f_TransformVector4f(&Q3, &transformToCamera, &P3);
    XrMatrix4x4f_TransformVector4f(&Q4, &transformToCamera, &P4);

    XrVector3f R1 = { Q1.x, Q1.y, Q1.w };
    XrVector3f R2 = { Q2.x, Q2.y, Q2.w };
    XrVector3f R3 = { Q3.x, Q3.y, Q3.w };
    XrVector3f R4 = { Q4.x, Q4.y, Q4.w };

    XrVector3f tempv1, tempv2;

    XrVector3f H1;
    XrVector3f H2;
    XrVector3f H3;

    XrVector3f_Cross(&tempv1, &R2, &R1);
    XrVector3f_Cross(&tempv2, &R3, &R4);
    XrVector3f_Cross(&H1, &tempv1, &tempv2);

    XrVector3f_Cross(&tempv1, &R1, &R4);
    XrVector3f_Cross(&tempv2, &R2, &R3);
    XrVector3f_Cross(&H2, &tempv1, &tempv2);

    XrVector3f_Cross(&tempv1, &R1, &R3);
    XrVector3f_Cross(&tempv2, &R2, &R4);
    XrVector3f_Cross(&H3, &tempv1, &tempv2);

    XrMatrix4x4f T = {
        H1.x, H2.x, H3.x, 0,
        H1.y, H2.y, H3.y, 0,
        H1.z, H2.z, H3.z, 0,
        0, 0, 0, 1};

    XrMatrix4x4f_Invert(&tempMatrix, &T);
    XrMatrix4x4f_Transpose(frameUVMat, &tempMatrix);
}
