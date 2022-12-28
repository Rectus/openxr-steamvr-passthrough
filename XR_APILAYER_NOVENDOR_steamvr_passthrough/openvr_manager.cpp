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
