#pragma once

#include <thread>
#include <condition_variable>

#include "layer.h"
#include "config_manager.h"
#include "openvr_manager.h"
#include "imgui.h"

using Microsoft::WRL::ComPtr;


#define DASHBOARD_OVERLAY_KEY "XR_APILAYER_NOVENDOR_steamvr_passthrough.{}.dashboard"

#define OVERLAY_RES_WIDTH 1200
#define OVERLAY_RES_HEIGHT 700

enum EMenuTab
{
	TabMain,
	TabApplication,
	TabStereo,
	TabOverrides,
	TabCamera,
	TabDebug
};

struct MenuDisplayValues
{
	bool bSessionActive = false;
	bool bDepthBlendingActive = false;
	ERenderAPI renderAPI = None;
	ERenderAPI appRenderAPI = None;
	std::string currentApplication;
	int frameBufferWidth = 0;
	int frameBufferHeight = 0;
	XrCompositionLayerFlags frameBufferFlags = 0;
	int64_t frameBufferFormat = 0;
	int64_t depthBufferFormat = 0;
	float nearZ = 0.0f;
	float farZ = 0.0f;

	float frameToRenderLatencyMS = 0.0f;
	float frameToPhotonsLatencyMS = 0.0f;
	float renderTimeMS = 0.0f;
	float stereoReconstructionTimeMS = 0.0f;
	float frameRetrievalTimeMS = 0.0f;

	bool bCorePassthroughActive = false;
	int CoreCurrentMode = 0;

	bool bExtInvertedAlphaActive = false;
	bool bVarjoDepthEstimationExtensionActive = false;
	bool bVarjoDepthCompositionExtensionActive = false;

	uint32_t CameraFrameWidth = 0;
	uint32_t CameraFrameHeight = 0;
	float CameraFrameRate = 0.0f;
	std::string CameraAPI;
};


class DashboardMenu
{
public:

	DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);

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

	void TextDescription(const char* fmt, ...);
	void TextDescriptionSpaced(const char* fmt, ...);

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	HMODULE m_dllModule;

	vr::VROverlayHandle_t m_overlayHandle;
	vr::VROverlayHandle_t m_thumbnailHandle;

	std::thread m_menuThread;
	bool m_bRunThread;

	ComPtr<ID3D11Device5> m_d3d11Device;
	ComPtr<ID3D11DeviceContext4> m_d3d11DeviceContext;
	ComPtr<ID3D11Texture2D> m_d3d11Texture[2];
	ComPtr<ID3D11RenderTargetView> m_d3d11RTV[2];
	ComPtr<ID3D11Fence> m_d3d11Fence;
	HANDLE m_d3d11FenceEvent;
	int m_frameIndex = 0;
	uint64_t m_syncCounter = 0;
	LARGE_INTEGER m_lastFrameStart;

	bool m_bMenuIsVisible;
	MenuDisplayValues m_displayValues;
	EMenuTab m_activeTab;

	ImFont* m_mainFont;
	ImFont* m_smallFont;
	ImFont* m_fixedFont;

	std::vector<DeviceDebugProperties> m_deviceDebugProps;
	int m_currentDebugDevice;

	std::vector<DeviceIdentProperties> m_deviceIdentProps;
	int m_currentIdentDevice;

	std::vector<std::string> m_cameraDevices;

	bool m_cameraTabBeenOpened = false;
	bool m_debugTabBeenOpened = false;

	bool m_bIsKeyboardOpen = false;
};

