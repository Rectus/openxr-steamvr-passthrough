#include "pch.h"
#include "openvr_manager.h"
#include <log.h>
#include "layer.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


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
        vr::VR_Shutdown();
    }
}

bool OpenVRManager::InitRuntime()
{
    if (m_bRuntimeInitialized) { return true; }

    if (!vr::VR_IsRuntimeInstalled())
    {
        ErrorLog("SteamVR installation not detected!\n");
        return false;
    }

    vr::EVRInitError error;
    vr::IVRSystem* system = vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Overlay);

    if (error != vr::EVRInitError::VRInitError_None)
    {
        ErrorLog("Failed to initialize SteamVR runtime, error %i\n", error);
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
    m_vrSystem = system;
    m_vrCompositor = vr::VRCompositor();
    m_vrTrackedCamera = vr::VRTrackedCamera();
    m_vrOverlay = vr::VROverlay();

    return true;
}

void OpenVRManager::GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties)
{
    properties.clear();

    vr::IVRSystem* vrSystem = GetVRSystem();
    vr::IVRTrackedCamera* trackedCamera = GetVRTrackedCamera();

    char stringPropBuffer[vr::k_unMaxPropertyStringSize];

    for (uint32_t deviceId = 0; deviceId < vr::k_unMaxTrackedDeviceCount; deviceId++)
    {
        if (vrSystem->GetTrackedDeviceClass(deviceId) == vr::TrackedDeviceClass_Invalid)
        {
            continue;
        }

        DeviceDebugProperties deviceProps;

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

        double cameraDistortionCoeffs[vr::k_unMaxDistortionFunctionParameters][4];
        numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraDistortionCoefficients_Float_Array, vr::k_unFloatPropertyTag, &cameraDistortionCoeffs, sizeof(cameraDistortionCoeffs));

        vr::HmdVector4_t whiteBalance[4];
        numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraWhiteBalance_Vector4_Array, vr::k_unHmdVector4PropertyTag, &whiteBalance, sizeof(whiteBalance));

        for (uint32_t cameraId = 0; cameraId < deviceProps.NumCameras; cameraId++)
        {
            deviceProps.CameraProps[cameraId].CameraToHeadTransform = cameraToHeadmatrices[cameraId];
            deviceProps.CameraProps[cameraId].DistortionFunction = (vr::EVRDistortionFunctionType)cameraDistortionFunctions[cameraId];

            for (uint32_t coeff = 0; coeff < vr::k_unMaxDistortionFunctionParameters; coeff++)
            {
                deviceProps.CameraProps[cameraId].DistortionCoefficients[cameraId] = cameraDistortionCoeffs[coeff][cameraId];
            }
            deviceProps.CameraProps[cameraId].WhiteBalance = whiteBalance[cameraId];
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

        properties.push_back(deviceProps);
    }
}