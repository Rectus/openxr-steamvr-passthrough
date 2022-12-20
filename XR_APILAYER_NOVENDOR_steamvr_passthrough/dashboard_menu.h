#pragma once

#include <thread>
#include <condition_variable>

#include "layer.h"
#include "config_manager.h"


using Microsoft::WRL::ComPtr;


#define DASHBOARD_OVERLAY_KEY "XR_steamvr_passthrough_dashboard"

#define OVERLAY_RES_WIDTH 800
#define OVERLAY_RES_HEIGHT 400

enum EDashboardAPI
{
	None,
	DirectX11,
	DirectX12
};

struct MenuDisplayValues
{
	bool bSessionActive = false;
	EDashboardAPI renderAPI = None;
	std::string currentApplication;
	int frameBufferWidth = 0;
	int frameBufferHeight = 0;
	XrCompositionLayerFlags frameBufferFlags = 0;
	int64_t frameBufferFormat = 0;
	float frameToRenderLatencyMS = 0.0f;
	float frameToPhotonsLatencyMS = 0.0f;
	float renderTimeMS = 0.0f;


	bool bCorePassthroughActive = false;
	int CoreCurrentMode = 0;
};


class DashboardMenu
{
public:

	DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);

	~DashboardMenu();
	
	MenuDisplayValues& GetDisplayValues() { return m_displayValues; }

private:

	void CreateOverlay();
	void DestroyOverlay();
	void CreateThumbnail();

	void RunThread();
	void HandleEvents();
	void TickMenu();

	void SetupDX11();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	vr::VROverlayHandle_t m_overlayHandle;
	vr::VROverlayHandle_t m_thumbnailHandle;

	std::thread m_menuThread;
	bool m_bRunThread;

	ComPtr<ID3D11Device> m_d3d11Device;
	ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
	ComPtr<ID3D11Texture2D> m_d3d11Texture;
	ComPtr<ID3D11RenderTargetView> m_d3d11RTV;

	bool m_bMenuIsVisible;
	MenuDisplayValues m_displayValues;
};

