
#pragma once

#include "layer_structs.h"
#include "async_renderer.h"
#include "passthrough_renderer.h"
#include "depth_reconstruction.h"
#include "camera_manager.h"
#include "config_manager.h"
#include "openvr_manager.h"
#include "menu_handler.h"
#include "menu_ipc_client.h"


class PassthroughSystem
{
public:
	PassthroughSystem(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, bool bIsInitialConfig);
	~PassthroughSystem();

	void SetExtensions(ExtensionData& data);
	bool SetupRenderer(const XrInstance instance, const XrSessionCreateInfo* createInfo, const XrSession* session);
	bool SetupProcessingPipeline();
	void ShutdownRenderer();
	EPassthroughCameraState GetCameraState() const;
	bool IsDepthSupportedByRenderer() const { return m_bDepthSupportedByRenderer; }
	ERenderAPI GetLayerRenderAPI() const { return m_renderAPI; };
	ERenderAPI GetAppRenderAPI() const { return m_appRenderAPI; };
	void GetTestPattern(DebugTexture& texture);
	ClientData& GetMenuClientData();
	void DispatchMenuClientData();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain);
	void OnPreRenderFrame(const XrFrameEndInfo* frameEndInfo);
	bool RenderPassthroughOnAppLayer(const XrFrameEndInfo* frameEndInfo, const uint32_t layerNum, FrameRenderParameters& renderParams);
	void OnPostRenderFrame(bool bDidRender, bool bInhibitIdle);

private:
	void CalculateFrameProjection(std::shared_ptr<CameraGPUFrame> cameraFrame, std::shared_ptr<DepthFrame> depthFrame, const XrCompositionLayerProjection& layer, FrameRenderParameters& renderParams);
	void CalculateHMDProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraGPUFrame>& cameraFrame, const XrCompositionLayerProjection& layer, FrameRenderParameters& renderParams);
	XrMatrix4x4f GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo);
	void UpdateRenderModels(const uint64_t cameraFrameTimestamp);

	HMODULE m_dllModule;

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<IPassthroughRenderer> m_inlineRenderer;
	std::shared_ptr<AsyncRenderer> m_asyncRenderer;
	std::shared_ptr<ICameraManager> m_cameraManager;
	std::shared_ptr<ICameraManager> m_augmentedCameraManager;
	std::shared_ptr<MenuHandler> m_menuHandler;
	std::shared_ptr<MenuIPCClient> m_menuIPCClient;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	std::shared_ptr<DepthReconstruction> m_depthReconstruction;
	std::shared_ptr<DepthReconstruction> m_augmentedDepthReconstruction;

	PerfTimer m_frameToRenderTime{ 20 };
	PerfTimer m_frameToPhotonTime{ 20 };
	PerfTimer m_depthToRenderTime{ 20 };
	PerfTimer m_depthToPhotonTime{ 20 };
	PerfTimer m_passthroughRenderTime{ 20 };
	PerfTimer m_lastRenderTime{};

	bool m_bIsPaused = false;
	bool m_bIsInitialConfig = false;
	bool m_bCamerasInitialized = false;
	bool m_bDepthSupportedByRenderer = false;
	ExtensionData m_extensionData{};

	ECameraProvider m_cameraProvider = CameraProvider_None;
	EProjectionMode m_projectionMode = Projection_RoomView2D;
	ERenderAPI m_renderAPI = RenderAPI_Direct3D11;
	ERenderAPI m_appRenderAPI = RenderAPI_Direct3D11;

	std::shared_ptr<std::vector<RenderModel>> m_renderModels;
	std::shared_ptr<DepthFrame> m_dummyDepthFrame;
	UVDistortionParameters m_dummyDistParams{};

	XrMatrix4x4f m_lastWorldToCameraProjectionLeft{};
	XrMatrix4x4f m_lastWorldToCameraProjectionRight{};
	XrMatrix4x4f m_lastCameraFrame_WorldToHMDProjectionLeft{};
	XrMatrix4x4f m_lastCameraFrame_WorldToHMDProjectionRight{};
	XrMatrix4x4f m_lastHMDFrame_WorldToHMDProjectionLeft{};
	XrMatrix4x4f m_lastHMDFrame_WorldToHMDProjectionRight{};
	uint32_t m_lastFrameSequence = 0;

	XrMatrix4x4f m_lastDispWorldToCameraProjectionLeft{};
	XrMatrix4x4f m_lastDispWorldToCameraProjectionRight{};
	XrMatrix4x4f m_lastDisparityViewToWorldLeft{};
	XrMatrix4x4f m_lastDisparityViewToWorldRight{};
};

