#pragma once

#include "config_manager.h"
#include "shared_structs.h"
#include <imgui.h>

#define OPENVR_APP_KEY "no_vendor.openxr_steamvr_passthrough_menu"
#define VR_MANIFEST_FILE_NAME "openxr_steamvr_passthrough_menu.vrmanifest"

#define DASHBOARD_OVERLAY_KEY "XR_APILAYER_NOVENDOR_steamvr_passthrough.menu.dashboard"

class DashboardOverlay
{
public:
	DashboardOverlay();
	~DashboardOverlay();

	bool IsRuntimeInitialized() 
	{
		std::lock_guard<std::mutex> lock(m_runtimeMutex);
		return m_bRuntimeInitialized;
	}
	bool HasOverlay() { return m_bHasOverlay; }
	bool IsOverlayVisible() { return m_bOverlayVisible; }
	bool HasFocus() { return m_bOverlayVisible && m_bHasFocus; }
	bool InitRuntime();
	bool CreateOverlay(uint32_t width, uint32_t height);
	void DestroyOverlay();
	void SetThumbnail(vr::VRVulkanTextureData_t* textureData);

	void OverlayFrameSync();
	bool HandleOverlayEvents(ImGuiIO& io);
	void UpdateOverlay(vr::VRVulkanTextureData_t* textureData, ImGuiIO& io);

	void GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties);
	void GetDeviceIdentProperties(std::vector<DeviceIdentProperties>& properties);

protected:

	std::mutex m_runtimeMutex;

	vr::VROverlayHandle_t m_overlayHandle;
	vr::VROverlayHandle_t m_thumbnailHandle;

	std::atomic<bool> m_bRuntimeInitialized = false;
	bool m_bHasOverlay = false;
	bool m_bOverlayVisible = false;
	bool m_bHasFocus = false;
	bool m_bIsKeyboardOpen = false;
	int m_overlayWidth = 0;
	int m_overlayHeight = 0;
};

