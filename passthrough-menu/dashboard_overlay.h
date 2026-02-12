#pragma once

#include "config_manager.h"
#include "openvr_manager.h"
#include <imgui.h>

#define OPENVR_APP_KEY "no_vendor.openxr_steamvr_passthrough_menu"
#define VR_MANIFEST_FILE_NAME "openxr_steamvr_passthrough_menu.vrmanifest"

#define DASHBOARD_OVERLAY_KEY "XR_APILAYER_NOVENDOR_steamvr_passthrough.menu.dashboard"

#define OVERLAY_RES_WIDTH 1200
#define OVERLAY_RES_HEIGHT 700

class DashboardOverlay
{
public:
	DashboardOverlay(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);
	~DashboardOverlay();

	void CreateOverlay();
	void DestroyOverlay();
	void CreateThumbnail();

	void HandleOverlayEvents(ImGuiIO& io);

	void UpdateOverlay();

protected:

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	vr::VROverlayHandle_t m_overlayHandle;
	vr::VROverlayHandle_t m_thumbnailHandle;

	bool m_bOverlayVisible = false;
	bool m_bIsKeyboardOpen = false;
};

