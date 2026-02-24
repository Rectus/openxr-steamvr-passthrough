#include "pch.h"
#include "openvr_manager.h"
#include "layer_structs.h"

#include "ivrclientcore.h"

OpenVRManager::OpenVRManager()
    : m_bRuntimeInitialized(false)
    , m_hmdDeviceId(-1)
{
    InitRuntime();
}

OpenVRManager::~OpenVRManager()
{
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    if (m_bRuntimeInitialized)
    {
        m_vrSystem = nullptr;
        m_vrCompositor = nullptr;
        m_vrTrackedCamera = nullptr;
        m_vrOverlay = nullptr;
        m_vrRenderModels = nullptr;
    }
}

typedef void* (*VRClientCoreFactoryFn)(const char* pInterfaceName, int* pReturnCode);

bool OpenVRManager::InitRuntime()
{
    std::lock_guard<std::mutex> lock(m_runtimeMutex);

    if (m_bRuntimeInitialized) { return true; }

    if (!vr::VR_IsRuntimeInstalled())
    {
        g_logger->error("SteamVR installation not detected!");
        return false;
    }

    // According to Valve, VR_Init can't be used in the same process as an OpenXR application.
    // Instead we manually load all the required interfaces directly from the client module.
    // SteamVR uses the same client library for OpenXR and OpenVR clients.
    // https://steamcommunity.com/app/250820/discussions/3/600786717156353525/

    // Get a handle for vrclient_x64.dll, which should already be loaded by the OpenXR app.
    HMODULE vrClientModule = GetModuleHandleA("vrclient_x64");
    if (!vrClientModule)
    {
        g_logger->error("vrclient_x64.dll not loaded by application!");
        return false;
    }
    
    VRClientCoreFactoryFn factoryFunc = (VRClientCoreFactoryFn)(GetProcAddress(vrClientModule, "VRClientCoreFactory"));
    if (!factoryFunc)
    {
        g_logger->error("Failed to get VRClientCoreFactory from vrclient_x64.dll!");
        return false;
    }

    int coreError = 0;
    m_vrClientCore = static_cast<vr::IVRClientCore*>(factoryFunc(vr::IVRClientCore_Version, &coreError));
    if (!m_vrClientCore)
    {
        g_logger->error("Failed to find IVRSystem interface, error {}", static_cast<int32_t>(coreError));
        return false;
    }

    vr::EVRInitError error;
    m_vrSystem = reinterpret_cast<vr::IVRSystem *>(m_vrClientCore->GetGenericInterface(vr::IVRSystem_Version, &error));

    if (m_vrSystem == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRSystem interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    m_vrCompositor = reinterpret_cast<vr::IVRCompositor*>(m_vrClientCore->GetGenericInterface(vr::IVRCompositor_Version, &error));

    if (m_vrCompositor == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRCompositor interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    m_vrTrackedCamera = reinterpret_cast<vr::IVRTrackedCamera*>(m_vrClientCore->GetGenericInterface(vr::IVRTrackedCamera_Version, &error));

    if (m_vrTrackedCamera == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRTrackedCamera interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    m_vrOverlay = reinterpret_cast<vr::IVROverlay*>(m_vrClientCore->GetGenericInterface(vr::IVROverlay_Version, &error));

    if (m_vrOverlay == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVROverlay interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    m_vrRenderModels = reinterpret_cast<vr::IVRRenderModels*>(m_vrClientCore->GetGenericInterface(vr::IVRRenderModels_Version, &error));

    if (m_vrRenderModels == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRRenderModels interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
    {
        if (m_vrSystem->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_HMD)
        {
            m_hmdDeviceId = i;
            break;
        }
    }

    if (m_hmdDeviceId < 0)
    {
        g_logger->error("HMD device ID not found!");
        return false;
    }

    m_bRuntimeInitialized = true;

    return true;
}

void OpenVRManager::GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties)
{
    properties.clear();

    vr::IVRSystem* vrSystem = GetVRSystem();
    vr::IVRTrackedCamera* trackedCamera = GetVRTrackedCamera();

    char stringPropBuffer[256];

    for (uint32_t deviceId = 0; deviceId < vr::k_unMaxTrackedDeviceCount; deviceId++)
    {
        if (vrSystem->GetTrackedDeviceClass(deviceId) == vr::TrackedDeviceClass_Invalid)
        {
            continue;
        }

        properties.push_back(DeviceDebugProperties());
        DeviceDebugProperties& deviceProps = properties.at(properties.size() - 1);

        deviceProps.DeviceClass = vrSystem->GetTrackedDeviceClass(deviceId);
        deviceProps.DeviceId = deviceId;


        deviceProps.bHasCamera = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_HasCamera_Bool);
        deviceProps.NumCameras = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_NumCameras_Int32);

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        uint32_t numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ManufacturerName_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.DeviceName.assign(stringPropBuffer);

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ModelNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.DeviceName.append(" ");
        deviceProps.DeviceName.append(stringPropBuffer);

        vr::HmdMatrix34_t cameraToHeadmatrices[4];
        uint32_t numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &cameraToHeadmatrices, sizeof(cameraToHeadmatrices));

        int32_t cameraDistortionFunctions[4];
        numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraDistortionFunction_Int32_Array, vr::k_unInt32PropertyTag, &cameraDistortionFunctions, sizeof(cameraDistortionFunctions));

        double cameraDistortionCoeffs[4][vr::k_unMaxDistortionFunctionParameters];
        numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraDistortionCoefficients_Float_Array, vr::k_unFloatPropertyTag, &cameraDistortionCoeffs, sizeof(cameraDistortionCoeffs));

        vr::HmdVector4_t whiteBalance[4];
        numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraWhiteBalance_Vector4_Array, vr::k_unHmdVector4PropertyTag, &whiteBalance, sizeof(whiteBalance));

        uint32_t fbSize;

        trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_Distorted, &deviceProps.DistortedFrameWidth, &deviceProps.DistortedFrameHeight, &fbSize);
        trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_Undistorted, &deviceProps.UndistortedFrameWidth, &deviceProps.UndistortedFrameHeight, &fbSize);
        trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &deviceProps.MaximumUndistortedFrameWidth, &deviceProps.MaximumUndistortedFrameHeight, &fbSize);

        for (uint32_t cameraId = 0; cameraId < deviceProps.NumCameras; cameraId++)
        {
            CameraDebugProperties& cameraProps = deviceProps.CameraProps[cameraId];

            trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_Distorted, &cameraProps.DistortedFocalLength, &cameraProps.DistortedOpticalCenter);
            trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_Undistorted, &cameraProps.UndistortedFocalLength, &cameraProps.UndistortedOpticalCenter);
            trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &cameraProps.MaximumUndistortedFocalLength, &cameraProps.MaximumUndistortedOpticalCenter);

            trackedCamera->GetCameraProjection(deviceId, cameraId, vr::VRTrackedCameraFrameType_Undistorted, 0.1f, 1.0f, &cameraProps.UndistortedProjecton);
            trackedCamera->GetCameraProjection(deviceId, cameraId, vr::VRTrackedCameraFrameType_MaximumUndistorted, 0.1f, 1.0f, &cameraProps.MaximumUndistortedProjecton);

            memcpy(&cameraProps.CameraToHeadTransform, &cameraToHeadmatrices[cameraId], sizeof(vr::HmdMatrix34_t));
            cameraProps.DistortionFunction = (vr::EVRDistortionFunctionType)cameraDistortionFunctions[cameraId];

            for (uint32_t coeff = 0; coeff < vr::k_unMaxDistortionFunctionParameters; coeff++)
            {
                cameraProps.DistortionCoefficients[coeff] = cameraDistortionCoeffs[cameraId][coeff];
            }
            cameraProps.WhiteBalance = whiteBalance[cameraId];
        }

        deviceProps.CameraFrameLayout = (vr::EVRTrackedCameraFrameLayout)vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraFrameLayout_Int32);
        deviceProps.CameraStreamFormat = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraStreamFormat_Int32);
        deviceProps.CameraToHeadTransform = vrSystem->GetMatrix34TrackedDeviceProperty(deviceId, vr::Prop_CameraToHeadTransform_Matrix34);
        deviceProps.CameraFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_CameraFirmwareVersion_Uint64);

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_CameraFirmwareDescription_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.CameraFirmwareDescription.assign(stringPropBuffer);

        deviceProps.CameraCompatibilityMode = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraCompatibilityMode_Int32);
        deviceProps.bCameraSupportsCompatibilityModes = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_CameraSupportsCompatibilityModes_Bool);
        deviceProps.CameraExposureTime = vrSystem->GetFloatTrackedDeviceProperty(deviceId, vr::Prop_CameraExposureTime_Float);
        deviceProps.CameraGlobalGain = vrSystem->GetFloatTrackedDeviceProperty(deviceId, vr::Prop_CameraGlobalGain_Float);

        deviceProps.HMDFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_FirmwareVersion_Uint64);
        deviceProps.FPGAFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_FPGAVersion_Uint64);
        deviceProps.bHMDSupportsRoomViewDirect = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_Hmd_SupportsRoomViewDirect_Bool);
        deviceProps.bSupportsRoomViewDepthProjection = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_SupportsRoomViewDepthProjection_Bool);
        deviceProps.bAllowCameraToggle = vrSystem->GetBoolTrackedDeviceProperty(deviceId, (vr::ETrackedDeviceProperty)1055); // vr::Prop_AllowCameraToggle_Bool
        deviceProps.bAllowLightSourceFrequency = vrSystem->GetBoolTrackedDeviceProperty(deviceId, (vr::ETrackedDeviceProperty)1056); // vr::Prop_AllowLightSourceFrequency_Bool
        
    } 
}

void OpenVRManager::GetDeviceIdentProperties(std::vector<DeviceIdentProperties>& properties)
{
    properties.clear();

    vr::IVRSystem* vrSystem = GetVRSystem();

    char stringPropBuffer[256];

    for (uint32_t deviceId = 0; deviceId < vr::k_unMaxTrackedDeviceCount; deviceId++)
    {
        if (vrSystem->GetTrackedDeviceClass(deviceId) == vr::TrackedDeviceClass_Invalid)
        {
            continue;
        }

        properties.push_back(DeviceIdentProperties());
        DeviceIdentProperties& deviceProps = properties.at(properties.size() - 1);

        deviceProps.DeviceId = deviceId;

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        uint32_t numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ManufacturerName_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.DeviceName.assign(stringPropBuffer);

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ModelNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.DeviceName.append(" ");
        deviceProps.DeviceName.append(stringPropBuffer);

        memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
        numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_SerialNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
        deviceProps.DeviceSerial.assign(stringPropBuffer);
    }
}