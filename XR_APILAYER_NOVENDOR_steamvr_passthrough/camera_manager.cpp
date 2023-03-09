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
    , m_frameLayout(EStereoFrameLayout::Mono)
    , m_projectionDistanceFar(5.0f)
    , m_useAlternateProjectionCalc(false)
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

void CameraManager::GetFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManager::GetIntrinsics(const uint32_t cameraIndex, XrVector2f& focalLength, XrVector2f& center) const
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraIntrinsics(m_hmdDeviceId, cameraIndex, vr::VRTrackedCameraFrameType_MaximumUndistorted, (vr::HmdVector2_t*)&focalLength, (vr::HmdVector2_t*)&center);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        ErrorLog("GetCameraIntrinsics error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }
}

void CameraManager::GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const
{
    vr::TrackedPropertyError error;
    uint32_t numBytes = m_openVRManager->GetVRSystem()->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraDistortionCoefficients_Float_Array, vr::k_unFloatPropertyTag, &coeffs, 16 * sizeof(double), &error);
    if (error != vr::TrackedProp_Success || numBytes == 0)
    {
        ErrorLog("Failed to get tracked camera distortion coefficients, error [%i]\n", error);
    }
}

EStereoFrameLayout CameraManager::GetFrameLayout() const
{
    return m_frameLayout;
}

XrMatrix4x4f CameraManager::GetLeftToRightCameraTransform() const
{
    return m_cameraLeftToRightPose;
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

    vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, vr::VRTrackedCameraFrameType_Distorted, &m_cameraTextureWidth, &m_cameraTextureHeight, &m_cameraFrameBufferSize);
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
            m_cameraFrameWidth = m_cameraTextureWidth;
            m_cameraFrameHeight = m_cameraTextureHeight / 2;
        }
        else
        {
            m_frameLayout = EStereoFrameLayout::StereoHorizontalLayout;
            m_cameraFrameWidth = m_cameraTextureWidth / 2;
            m_cameraFrameHeight = m_cameraTextureHeight;
        }
    }
    else
    {
        m_frameLayout = EStereoFrameLayout::Mono;
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight;
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

    GetTrackedCameraEyePoses(m_cameraToHMDLeft, m_cameraToHMDRight);

    XrMatrix4x4f_Invert(&m_HMDToCameraLeft, &m_cameraToHMDLeft);
    XrMatrix4x4f_Invert(&m_HMDToCameraRight, &m_cameraToHMDRight);

    XrMatrix4x4f_Multiply(&m_cameraLeftToRightPose, &m_HMDToCameraRight, &m_cameraToHMDLeft);
    XrMatrix4x4f_Multiply(&m_cameraRightToLeftPose, &m_HMDToCameraLeft, &m_cameraToHMDRight);
}

bool CameraManager::GetCameraFrame(std::shared_ptr<CameraFrame>& frame)
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

void CameraManager::ServeFrames()
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

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        // Wait for the old frame struct to be available in case someone is still reading from it.
        std::unique_lock writeLock(m_underConstructionFrame->readWriteMutex);

        while (true)
        {
            vr::EVRTrackedCameraFrameType frameType = m_configManager->GetConfig_Main().ProjectionMode == ProjectionRoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

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

        vr::EVRTrackedCameraFrameType frameType = mainConf.ProjectionMode == ProjectionRoomView2D ? vr::VRTrackedCameraFrameType_MaximumUndistorted : vr::VRTrackedCameraFrameType_Distorted;

        if (m_renderAPI == DirectX11)
        {
            std::shared_ptr<IPassthroughRenderer> renderer = m_renderer.lock();

            if (!renderer.get())
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

        // TODO: Getting the framebuffer crashes under Vulkan
        if(m_renderAPI == DirectX12 || 
            (mainConf.ProjectionMode == ProjectionStereoReconstruction && m_renderAPI != Vulkan))
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

        XrMatrix4x4f_Multiply(&pose, &refSpacePose, &trackingToStage);
        XrMatrix4x4f_Multiply(&output,  &viewToTracking, &pose);
    }
    else
    {
        XrMatrix4x4f_Multiply(&output, &viewToTracking, &refSpacePose);
    }

    return output;
}

void CameraManager::UpdateProjectionMatrix(std::shared_ptr<CameraFrame>& frame)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    if (mainConf.ProjectionDistanceFar * 1.5f != m_projectionDistanceFar)
    {
        // Push back far plane by 1.5x to account for the flat projection plane of the passthrough frame.
        m_projectionDistanceFar = mainConf.ProjectionDistanceFar * 1.5f;

        vr::HmdMatrix44_t vrProjection;
        vr::EVRTrackedCameraError error = trackedCamera->GetCameraProjection(m_hmdDeviceId, 0, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

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
            XrMatrix4x4f_CreateTranslation(&offsetMatrix, 0.0f, -0.5f, 0.0f);
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
            error = trackedCamera->GetCameraProjection(m_hmdDeviceId, 1, vr::VRTrackedCameraFrameType_MaximumUndistorted, NEAR_PROJECTION_DISTANCE, m_projectionDistanceFar, &vrProjection);

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


void CameraManager::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrTime& displayTime, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    UpdateProjectionMatrix(frame);

    CalculateFrameProjectionForEye(LEFT_EYE, frame, layer, refSpaceInfo, distortionParams);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, layer, refSpaceInfo, distortionParams);
}

void CameraManager::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams)
{
    Config_Main& mainConf = m_configManager->GetConfig_Main();

    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t CameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    XrMatrix4x4f* worldToHMDMatrix = (eye == LEFT_EYE) ? &frame->worldToHMDProjectionLeft : &frame->worldToHMDProjectionRight;
    
    XrVector3f* hmdWorldPos = (eye == LEFT_EYE) ? &frame->hmdViewPosWorldLeft : &frame->hmdViewPosWorldRight;
    
    XrMatrix4x4f hmdWorldToView = GetHMDWorldToViewMatrix(eye, layer, refSpaceInfo);

    XrMatrix4x4f hmdViewToWorld;
    XrMatrix4x4f_Invert(&hmdViewToWorld, &hmdWorldToView);
    XrVector3f inPos{ 0,0,0 };
    XrMatrix4x4f_TransformVector3f(hmdWorldPos, &hmdViewToWorld, &inPos);

    auto depthInfo = (const XrCompositionLayerDepthInfoKHR*)layer.views[(eye == LEFT_EYE) ? 0 : 1].next;

    while (depthInfo != nullptr)
    {
        if (depthInfo->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
        {
            break;
        }
        depthInfo = (const XrCompositionLayerDepthInfoKHR*)depthInfo->next;
    }

    float nearZ = NEAR_PROJECTION_DISTANCE;
    float farZ = m_projectionDistanceFar;

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

    XrMatrix4x4f hmdProjection;
    XrMatrix4x4f_CreateProjectionFov(&hmdProjection, GRAPHICS_D3D, layer.views[(eye == LEFT_EYE) ? 0 : 1].fov, nearZ, farZ);

    // Handle infinite far Z - XrMatrix4x4f_CreateProjectionFov sets it up wrong.
    if (depthInfo && (farZ == (std::numeric_limits<float>::max)() || !std::isfinite(farZ) ))
    {
        hmdProjection.m[10] = 0;
        hmdProjection.m[14] = nearZ;
    }
    else if (frame->bHasReversedDepth)
    {
        // TODO check that apps with finite reversed z work
        //hmdProjection.m[10] *= -1;
        //hmdProjection.m[14] *= -1;
        //hmdProjection.m[10] = (depthInfo->farZ) / (depthInfo->farZ - depthInfo->nearZ);
        //hmdProjection.m[14] = (depthInfo->farZ * (depthInfo->nearZ)) / (depthInfo->farZ - depthInfo->nearZ);
    }

    if (CameraId == 0)
    {
        XrMatrix4x4f_Multiply(worldToHMDMatrix, &hmdProjection, &hmdWorldToView);
    }
    else
    {
        XrMatrix4x4f_Multiply(worldToHMDMatrix, &hmdProjection, &hmdWorldToView);
    }



    if (m_configManager->GetConfig_Main().ProjectionMode == ProjectionRoomView2D)
    {
        XrMatrix4x4f leftCameraToTrackingPose = ToXRMatrix4x4(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

        if (eye == LEFT_EYE)
        {

            XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldLeft, &leftCameraToTrackingPose, &m_cameraProjectionInvFarLeft);
        }
        else
        {

            if (bIsStereo)
            {
                XrMatrix4x4f tempMatrix;
                XrMatrix4x4f_Multiply(&tempMatrix, &leftCameraToTrackingPose, &m_cameraRightToLeftPose);
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &tempMatrix, &m_cameraProjectionInvFarRight);
            }
            else
            {
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &leftCameraToTrackingPose, &m_cameraProjectionInvFarLeft);
            }
        }
    }
    else
    {
        std::shared_lock readLock(distortionParams.readWriteMutex);

        // The math on these matrices still don't seem to line up perfectly, I don't know if that is due to the shaders, the matrices, the output from OpenCV, or the calibration data from OpenVR.

        XrMatrix4x4f frameProjection;
        XrMatrix4x4f_Transpose(&frameProjection, (eye == LEFT_EYE) ? &distortionParams.cameraProjectionLeft : &distortionParams.cameraProjectionRight);

        frameProjection.m[0] = 2.0f * frameProjection.m[0] / (float)m_cameraFrameWidth;
        frameProjection.m[5] = -2.0f * frameProjection.m[5] / (float)m_cameraFrameHeight;
        frameProjection.m[8] = (1.0f - 2.0f * frameProjection.m[8] / (float)m_cameraFrameWidth);
        frameProjection.m[9] = (1.0f - 2.0f * frameProjection.m[9] / (float)m_cameraFrameHeight);

        frameProjection.m[10] = -m_projectionDistanceFar / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
        frameProjection.m[11] = -1.0f;
        frameProjection.m[12] = 2.0f * frameProjection.m[12] / (float)m_cameraFrameWidth;
        frameProjection.m[13] = 0.0f;
        frameProjection.m[14] = -(m_projectionDistanceFar * NEAR_PROJECTION_DISTANCE) / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
        frameProjection.m[15] = 0.0f;


        // TODO: The custom 2D mode inverts the y view angle, but still projects better than everything else?!
        XrMatrix4x4f frameProjection2;
        XrMatrix4x4f_Transpose(&frameProjection2, (eye == LEFT_EYE) ? &distortionParams.cameraProjectionLeft : &distortionParams.cameraProjectionRight);

        frameProjection2.m[0] = -2.0f * frameProjection2.m[0] / (float)m_cameraFrameWidth;
        frameProjection2.m[5] = 2.0f * frameProjection2.m[5] / (float)m_cameraFrameHeight;
        frameProjection2.m[8] = -(1.0f - 2.0f * frameProjection2.m[8] / (float)m_cameraFrameWidth);
        frameProjection2.m[9] = -(1.0f - 2.0f * frameProjection2.m[9] / (float)m_cameraFrameHeight);

        frameProjection2.m[10] = m_projectionDistanceFar / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
        frameProjection2.m[11] = 1.0f;
        frameProjection2.m[12] = -2.0f * frameProjection.m[12] / (float)m_cameraFrameWidth;
        frameProjection2.m[13] = 0.0f;
        frameProjection2.m[14] = -(m_projectionDistanceFar * NEAR_PROJECTION_DISTANCE) / (m_projectionDistanceFar - NEAR_PROJECTION_DISTANCE);
        frameProjection.m[15] = 0.0f;

        XrMatrix4x4f frameProjectionInverse;
        XrMatrix4x4f_Invert(&frameProjectionInverse, &frameProjection2);

        XrMatrix4x4f leftCameraToTrackingPose = ToXRMatrix4x4(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);
        XrMatrix4x4f leftCameraFromTrackingPose;
        XrMatrix4x4f_Invert(&leftCameraFromTrackingPose, &leftCameraToTrackingPose);      

        if (eye == LEFT_EYE)
        {
            XrMatrix4x4f rectifiedRotationInverse;
            XrMatrix4x4f_Transpose(&rectifiedRotationInverse, &distortionParams.rectifiedRotationLeft);

            XrMatrix4x4f a;
            XrMatrix4x4f_Multiply(&a, &rectifiedRotationInverse, &leftCameraFromTrackingPose);
            XrMatrix4x4f_Multiply(&frame->worldToCameraProjectionLeft, &frameProjection, &a);

            XrMatrix4x4f_Multiply(&a, &distortionParams.rectifiedRotationLeft, &frameProjectionInverse);
            XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldLeft, &leftCameraToTrackingPose, &a);
        }
        else
        {
            XrMatrix4x4f rectifiedRotation = distortionParams.rectifiedRotationRight;

            // The right eye rotation matrices work for some reasong with x and z axis rotations reversed.
            rectifiedRotation.m[1] *= -1;
            rectifiedRotation.m[4] *= -1;
            rectifiedRotation.m[6] *= -1;
            rectifiedRotation.m[9] *= -1;
            
            XrMatrix4x4f a;
            XrMatrix4x4f_Multiply(&a, &rectifiedRotation, &leftCameraFromTrackingPose);
            XrMatrix4x4f_Multiply(&frame->worldToCameraProjectionRight, &frameProjection, &a);

            if (bIsStereo)
            {
                XrMatrix4x4f rectifiedRotationInverse;
                XrMatrix4x4f_Transpose(&rectifiedRotationInverse, &distortionParams.rectifiedRotationRight);

                rectifiedRotationInverse.m[1] *= -1;
                rectifiedRotationInverse.m[4] *= -1;
                rectifiedRotationInverse.m[6] *= -1;
                rectifiedRotationInverse.m[9] *= -1;

                XrMatrix4x4f_Multiply(&a, &rectifiedRotationInverse, &frameProjectionInverse);
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &leftCameraToTrackingPose, &a);
            }
            else
            {
                XrMatrix4x4f rectifiedRotationInverse;
                XrMatrix4x4f_Transpose(&rectifiedRotationInverse, &distortionParams.rectifiedRotationLeft);

                XrMatrix4x4f_Multiply(&a, &rectifiedRotationInverse, &frameProjectionInverse);
                XrMatrix4x4f_Multiply(&frame->cameraProjectionToWorldRight, &leftCameraToTrackingPose, &a);
            }
        }
    }
}
