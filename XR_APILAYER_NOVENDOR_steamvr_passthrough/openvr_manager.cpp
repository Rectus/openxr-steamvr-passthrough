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
        g_logger->error("Failed to find IVRClientCore interface, error {}", static_cast<int32_t>(coreError));
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

    m_vrPaths = reinterpret_cast<vr::IVRPaths*>(m_vrClientCore->GetGenericInterface(vr::IVRPaths_Version, &error));

    if (m_vrPaths == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRPaths interface, error {}", static_cast<int32_t>(error));
        return false;
    }

    m_vrBlockQueue = reinterpret_cast<vr::IVRBlockQueue*>(m_vrClientCore->GetGenericInterface(vr::IVRBlockQueue_Version, &error));

    if (m_vrBlockQueue == nullptr || error != vr::EVRInitError::VRInitError_None)
    {
        g_logger->error("Failed to find IVRBlockQueue interface, error {}", static_cast<int32_t>(error));
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
