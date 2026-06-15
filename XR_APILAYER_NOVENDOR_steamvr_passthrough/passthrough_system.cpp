
#include "pch.h"
#include "passthrough_system.h"

#include "resource.h"
#include "perfutil.h"
#include "pathutil.h"
#include "mathutil.h"
#include "lodepng.h"




PassthroughSystem::PassthroughSystem(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, bool bIsInitialConfig)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_bIsInitialConfig(bIsInitialConfig)
{

	m_openVRManager = std::make_shared<OpenVRManager>();
	m_menuIPCClient = std::make_shared<MenuIPCClient>();
	m_menuHandler = std::make_unique<MenuHandler>(m_dllModule, m_configManager, m_menuIPCClient);
	m_menuIPCClient->RegisterReader(m_menuHandler);

	m_renderModels = std::make_shared<std::vector<RenderModel>>();
}

PassthroughSystem::~PassthroughSystem()
{
	// Destroy renderer first so it can block on any outstanding rendering commands.
	m_inlineRenderer.reset();

	while (m_heldCameraFrames.size() > 0)
	{
		std::shared_ptr <ICameraManager>& cameraFrameManager =
			m_cameraProvider == CameraProvider_Augmented ?
			m_augmentedCameraManager : m_cameraManager;

		cameraFrameManager->ReleaseCameraGPUFrame(m_heldCameraFrames.front());
		m_heldCameraFrames.pop_front();
	}

	while (m_heldDepthFrames.size() > 0)
	{
		m_depthReconstruction->ReleaseDepthFrame(m_heldDepthFrames.front());
		m_heldDepthFrames.pop_front();
	}

	m_asyncRenderer.reset();
	m_depthReconstruction.reset();
	m_augmentedDepthReconstruction.reset();
	m_cameraManager.reset();
	m_augmentedCameraManager.reset();
	
	g_logger->flush();
	m_openVRManager.reset();
	m_menuHandler.reset();
	m_menuIPCClient.reset();
}


void PassthroughSystem::SetExtensions(ExtensionData& data)
{
	m_extensionData = data;
}

bool PassthroughSystem::SetupRenderer(const XrInstance instance, const XrSessionCreateInfo* createInfo, const XrSession* session)
{
	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	ClientData& clientData = m_menuHandler->GetClientData();

	VkResult res = volkInitialize();
	if (res != VK_SUCCESS)
	{
		g_logger->error("Volk initialization failed: {}", (int)res);
	}

	const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);

	while (entry != nullptr)
	{
		switch (entry->type)
		{
		case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
		{
			g_logger->info("Initializing rendering for Direct3D 11...");

			const XrGraphicsBindingD3D11KHR* bindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
			m_inlineRenderer = std::make_shared<PassthroughRendererDX11>(bindings->device, m_configManager);
			m_renderAPI = RenderAPI_Direct3D11;
			m_appRenderAPI = RenderAPI_Direct3D11;

			if (!SetupProcessingPipeline())
			{
				return false;
			}

			clientData.Values.bSessionActive = true;
			clientData.Values.RenderAPI = RenderAPI_Direct3D11;
			clientData.Values.AppRenderAPI = RenderAPI_Direct3D11;
			m_menuHandler->DispatchClientDataValues();
			m_bDepthSupportedByRenderer = true;
			g_logger->info("Direct3D 11 rendering initialized");

			return true;
		}

		case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR:
		{
			g_logger->info("Initializing rendering for Direct3D 12...");

			const XrGraphicsBindingD3D12KHR* bindings = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry);

			m_inlineRenderer = std::make_shared<PassthroughRendererDX11Interop>(bindings->device, bindings->queue, m_configManager);
			m_renderAPI = RenderAPI_Direct3D11;
			m_appRenderAPI = RenderAPI_Direct3D12;

			if (!SetupProcessingPipeline())
			{
				return false;
			}

			clientData.Values.bSessionActive = true;
			clientData.Values.RenderAPI = RenderAPI_Direct3D11;
			clientData.Values.AppRenderAPI = RenderAPI_Direct3D12;
			m_menuHandler->DispatchClientDataValues();
			m_bDepthSupportedByRenderer = true;
			g_logger->info("Direct3D 12 rendering initialized");

			return true;
		}

		case XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR: // same as XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR
		{
			g_logger->info("Initializing rendering for Vulkan...");

			ERenderAPI usedAPI = RenderAPI_Vulkan;

			const XrGraphicsBindingVulkanKHR* bindings = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(entry);
			if (mainConfig.UseLegacyVulkanRenderer)
			{

				m_inlineRenderer = std::make_shared<PassthroughRendererVulkan>(*bindings, m_configManager);
				g_logger->info("Using legacy Vulkan renderer");
			}
			else
			{
				if (!m_extensionData.bVulkan2ExtensionEnabled && mainConfig.AllowVulkanWithoutConfirmedFeatures)
				{

					g_logger->warn("Application is using the XR_KHR_vulkan_enable extension. Required Vulkan features can not be confirmed");
				}
				else if (!m_extensionData.bVulkan2ExtensionEnabled)
				{
					g_logger->error("The XR_KHR_vulkan_enable extension is only supported with the legacy renderer, passthrough rendering not enabled");
					return false;
				}

				usedAPI = RenderAPI_Direct3D11;
				m_inlineRenderer = std::make_shared<PassthroughRendererDX11Interop>(*bindings, m_configManager);
			}

			m_renderAPI = usedAPI;
			m_appRenderAPI = RenderAPI_Vulkan;

			if (!SetupProcessingPipeline())
			{
				return false;
			}

			clientData.Values.bSessionActive = true;
			clientData.Values.RenderAPI = usedAPI;
			clientData.Values.AppRenderAPI = RenderAPI_Vulkan;
			m_menuHandler->DispatchClientDataValues();
			m_bDepthSupportedByRenderer = false;
			g_logger->info("Vulkan rendering initialized");

			return true;
		}

		case XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR:
		{
			g_logger->info("Initializing rendering for OpenGL...");

			const XrGraphicsBindingOpenGLWin32KHR* bindings = reinterpret_cast<const XrGraphicsBindingOpenGLWin32KHR*>(entry);

			m_inlineRenderer = std::make_shared<PassthroughRendererDX11Interop>(*bindings, m_configManager);
			m_appRenderAPI = RenderAPI_OpenGL;
			m_renderAPI = RenderAPI_Direct3D11;

			if (!SetupProcessingPipeline())
			{
				return false;
			}

			clientData.Values.bSessionActive = true;
			clientData.Values.RenderAPI = RenderAPI_Direct3D11;
			clientData.Values.AppRenderAPI = RenderAPI_OpenGL;
			m_menuHandler->DispatchClientDataValues();
			m_bDepthSupportedByRenderer = false;
			g_logger->info("OpenGL rendering initialized");

			return true;
		}

		default:

			entry = reinterpret_cast<const XrBaseInStructure*>(entry->next);
		}
	}
	g_logger->info("Passthrough API layer: No supported graphics APIs detected!");
	return false;
}

bool PassthroughSystem::SetupProcessingPipeline()
{

	while (m_heldCameraFrames.size() > 0)
	{
		std::shared_ptr <ICameraManager>& cameraFrameManager =
			m_cameraProvider == CameraProvider_Augmented ?
			m_augmentedCameraManager : m_cameraManager;

		cameraFrameManager->ReleaseCameraGPUFrame(m_heldCameraFrames.front());
		m_heldCameraFrames.pop_front();
	}

	while (m_heldDepthFrames.size() > 0)
	{		
		m_depthReconstruction->ReleaseDepthFrame(m_heldDepthFrames.front());
		m_heldDepthFrames.pop_front();
	}

	m_asyncRenderer.reset();
	m_depthReconstruction.reset();
	m_augmentedDepthReconstruction.reset();
	m_cameraManager.reset();
	m_augmentedCameraManager.reset();
	
	if (!m_inlineRenderer.get())
	{
		g_logger->error("Trying to initialize processing pipeline without renderer!");
		return false;
	}

	m_inlineRenderer->DestroySharedTextures();	

	Config_Main& mainConfig = m_configManager->GetConfig_Main();

	m_cameraProvider = mainConfig.CameraProvider;
	m_projectionMode = mainConfig.ProjectionMode;

	m_bIsPaused = false;
	m_lastRenderTime.StartPerfTimer();

	if (m_projectionMode != Projection_RoomView2D)
	{
		m_asyncRenderer = std::make_shared<AsyncRenderer>(m_configManager, m_inlineRenderer);
	}

	if (m_cameraProvider == CameraProvider_OpenVR)
	{
		m_cameraManager = std::make_shared<CameraManagerOpenVR>(m_inlineRenderer, m_asyncRenderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, m_projectionMode);

		if (!m_cameraManager->InitCamera())
		{
			g_logger->error("Failed to initialize camera!");
			return false;
		}

		if (m_bIsInitialConfig)
		{
			m_bIsInitialConfig = false;

			// Default to stereo mode if we have a compatible headset.
			// TODO: Just checks for the fisheye model at the moment, whitelist of known models would be better.
			/*if (m_cameraManager->GetFrameLayout() != FrameLayout_Mono && m_cameraManager->IsUsingFisheyeModel())
			{
				m_projectionMode = Projection_StereoReconstruction;
			}*/
		}

		ClientData& data = m_menuHandler->GetClientData();
		m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
		m_menuHandler->DispatchClientDataValues();
	}
	else if (m_cameraProvider == CameraProvider_Augmented)
	{
		m_cameraManager = std::make_shared<CameraManagerOpenVR>(m_inlineRenderer, m_asyncRenderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, m_projectionMode);
		m_augmentedCameraManager = std::make_shared<CameraManagerOpenCV>(m_inlineRenderer, m_asyncRenderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, m_projectionMode, true);

		if (!m_cameraManager->InitCamera() || !m_augmentedCameraManager->InitCamera())
		{
			g_logger->error("Failed to initialize camera!");
			return false;
		}
		ClientData& data = m_menuHandler->GetClientData();
		m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
		m_menuHandler->DispatchClientDataValues();
	}
	else if (m_cameraProvider == CameraProvider_OpenCV)
	{
		m_cameraManager = std::make_shared<CameraManagerOpenCV>(m_inlineRenderer, m_asyncRenderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, m_projectionMode);

		if (!m_cameraManager->InitCamera())
		{
			g_logger->error("Failed to initialize camera!");
			return false;
		}
		ClientData& data = m_menuHandler->GetClientData();
		m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
		m_menuHandler->DispatchClientDataValues();
	}
	else
	{
		g_logger->error("No camera provider set!");
		return false;
	}

	m_bCamerasInitialized = true;

	if (!m_inlineRenderer->InitRenderer())
	{
		g_logger->error("Failed to initialize inline renderer!");
		return false;
	}

	if (m_projectionMode != Projection_RoomView2D)
	{
		if (!m_asyncRenderer->InitRenderer())
		{
			g_logger->error("Failed to initialize async renderer!");
			return false;
		}

		m_depthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_cameraManager, m_asyncRenderer);

		if (m_cameraProvider == CameraProvider_Augmented)
		{
			m_augmentedDepthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_augmentedCameraManager, m_asyncRenderer);
		}
	}

	return true;
}

void PassthroughSystem::ShutdownRenderer()
{
	m_inlineRenderer.reset();

	while (m_heldCameraFrames.size() > 0)
	{
		std::shared_ptr <ICameraManager>& cameraFrameManager =
			m_cameraProvider == CameraProvider_Augmented ?
			m_augmentedCameraManager : m_cameraManager;

		cameraFrameManager->ReleaseCameraGPUFrame(m_heldCameraFrames.front());
		m_heldCameraFrames.pop_front();
	}

	while (m_heldDepthFrames.size() > 0)
	{
		m_depthReconstruction->ReleaseDepthFrame(m_heldDepthFrames.front());
		m_heldDepthFrames.pop_front();
	}

	m_asyncRenderer.reset();
	m_depthReconstruction.reset();
	m_augmentedDepthReconstruction.reset();
	m_cameraManager.reset();
	m_augmentedCameraManager.reset();

}

EPassthroughCameraState PassthroughSystem::GetCameraState() const
{
	if (m_cameraProvider == CameraProvider_Augmented)
	{
		EPassthroughCameraState mainState = m_cameraManager->GetCameraState();
		EPassthroughCameraState augState = m_augmentedCameraManager->GetCameraState();

		if (mainState == augState)
		{
			return mainState;
		}
		else if (mainState == CameraState_Error || augState == CameraState_Error)
		{
			return CameraState_Error;
		}

		return CameraState_Waiting;
	}
	return m_cameraManager->GetCameraState();
}

void PassthroughSystem::GetTestPattern(DebugTexture& texture)
{
	HRSRC resInfo = FindResource(m_dllModule, MAKEINTRESOURCE(IDB_PNG_TESTPATTERN), L"PNG");
	if (resInfo == nullptr)
	{
		g_logger->error("Error finding test pattern resource!");
		return;
	}
	HGLOBAL memory = LoadResource(m_dllModule, resInfo);
	if (memory == nullptr)
	{
		g_logger->error("Error loading test pattern resource!");
		return;
	}
	size_t data_size = SizeofResource(m_dllModule, resInfo);
	void* data = LockResource(memory);

	if (data == nullptr)
	{
		g_logger->error("Error reading test pattern resource!");
		return;
	}

	std::lock_guard<std::mutex> writelock(texture.RWMutex);

	if (texture.CurrentTexture != DebugTexture_TestImage)
	{
		texture.Texture = std::vector<uint8_t>();
	}

	unsigned width, height;
	unsigned error = lodepng::decode(texture.Texture, width, height, (uint8_t*)data, data_size);

	if (error)
	{
		g_logger->error("Error decoding test pattern!");
		return;
	}

	texture.Texture.resize(width * height * 4);

	texture.TextureSize.width = width;
	texture.TextureSize.height = height;
	texture.PixelSize = 4;
	texture.Format = DebugTextureFormat_RGBA8;
	texture.CurrentTexture = DebugTexture_TestImage;
	texture.bDimensionsUpdated = true;

	return;
}

ClientData& PassthroughSystem::GetMenuClientData()
{
	return m_menuHandler->GetClientData();
}

void PassthroughSystem::DispatchMenuClientData()
{
	m_menuHandler->DispatchApplicationModuleName();
	m_menuHandler->DispatchApplicationName();
	m_menuHandler->DispatchEngineName();
	m_menuHandler->DispatchClientDataValues();
}

void PassthroughSystem::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain)
{
	m_inlineRenderer->InitRenderTarget(eye, rendertarget, imageIndex, swapchainInfo, swapchain);
}

void PassthroughSystem::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain)
{
	m_inlineRenderer->InitDepthBuffer(eye, depthBuffer, imageIndex, swapchainInfo, swapchain);
}

void PassthroughSystem::OnPreRenderFrame(const XrFrameEndInfo* frameEndInfo)
{
	ClientData& data = m_menuHandler->GetClientData();

	if (m_cameraManager.get())
	{
		data.Values.CameraState = m_cameraManager->GetCameraState();
	}

	data.Values.CoreCurrentMode = frameEndInfo->environmentBlendMode;
	data.Values.NumCompositionLayers = frameEndInfo->layerCount;
	bool bDepthSubmitted = false;

	for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
	{
		auto layer = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);

		if (layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) { continue; }

		data.Values.FrameBufferHeight = layer->views[0].subImage.imageRect.extent.height;
		data.Values.FrameBufferWidth = layer->views[0].subImage.imageRect.extent.width;
		data.Values.FrameBufferFlags = layer->layerFlags;

		auto depthInfo = reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(layer->views[0].next);

		while (depthInfo != nullptr)
		{
			if (depthInfo->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
			{
				bDepthSubmitted = true;
				break;
			}
			depthInfo = reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(depthInfo->next);
		}

		break;
	}
	data.Values.bDepthLayerSubmitted = bDepthSubmitted;

	m_menuHandler->DispatchClientDataValues();
}

bool PassthroughSystem::RenderPassthroughOnAppLayer(const XrFrameEndInfo* frameEndInfo, const uint32_t layerNum, FrameRenderParameters& renderParams)
{
	// Make local copies for consistency
	Config_Main mainConf = m_configManager->GetConfig_Main();
	Config_Core coreConf = m_configManager->GetConfig_Core();
	Config_Extensions extConf = m_configManager->GetConfig_Extensions();
	Config_Depth depthConf = m_configManager->GetConfig_Depth();

	if (m_bIsPaused)
	{
		if (m_cameraProvider == CameraProvider_Augmented)
		{
			m_augmentedCameraManager->SetPaused(false);
		}
		m_cameraManager->SetPaused(false);

		if (!m_bCamerasInitialized)
		{
			if (m_cameraProvider == CameraProvider_Augmented)
			{
				if ((!m_cameraManager->InitCamera() || !m_augmentedCameraManager->InitCamera()))
				{
					g_logger->error("Failed to reinitialize camera!");
					return false;
				}
			}
			else if (!m_cameraManager->InitCamera())
			{
				g_logger->error("Failed to reinitialize camera!");
				return false;
			}
			m_bCamerasInitialized = true;
		}
		m_bIsPaused = false;
	}


	if (renderParams.LeftFrameIndex < 0 || renderParams.RightFrameIndex < 0)
	{
		g_logger->error("Error: No swapchains found!");
		return false;
	}


	renderParams.CameraProvider = m_cameraProvider;
	renderParams.ProjectionMode = m_projectionMode;
	renderParams.ProjectionDistance = mainConf.ProjectionDistanceFar;

	if (renderParams.bUseFBPassthrough)
	{
		if (extConf.ExtFBPassthroughAllowColorSettings && renderParams.FBLayer->ColorAdjustmentEnabled)
		{
			m_configManager->SetEnableAsyncColorAdjustment(false);
			renderParams.bForceColorSettings = true;
			renderParams.ForcedBrightness = renderParams.FBLayer->Brightness;
			renderParams.ForcedContrast = renderParams.FBLayer->Contrast;
			renderParams.ForcedSaturation = renderParams.FBLayer->Saturation;
		}
		else
		{
			m_configManager->SetEnableAsyncColorAdjustment(true);
		}
		renderParams.RenderOpacity = renderParams.FBLayer->Opacity;
	}
	else
	{
		m_configManager->SetEnableAsyncColorAdjustment(true);
	}

	ClientData& clientData = m_menuHandler->GetClientData();
	

	if (coreConf.CoreForcePassthrough && coreConf.CoreForceMode >= 0)
	{
		renderParams.BlendMode = (EPassthroughBlendMode)coreConf.CoreForceMode;
		clientData.Values.bCorePassthroughActive = true;
		clientData.Values.bFBPassthroughActive = false;
		clientData.Values.bFBPassthroughDepthActive = false;
	}
	else if (renderParams.bUseFBPassthrough)
	{
		renderParams.BlendMode = AlphaBlendPremultiplied;
		clientData.Values.bCorePassthroughActive = false;
		clientData.Values.bFBPassthroughActive = true;
		clientData.Values.bFBPassthroughDepthActive = renderParams.FBLayer->DepthEnabled;
	}
	else
	{
		renderParams.BlendMode = (EPassthroughBlendMode)frameEndInfo->environmentBlendMode;
		clientData.Values.bCorePassthroughActive = true;
		clientData.Values.bFBPassthroughActive = false;
		clientData.Values.bFBPassthroughDepthActive = false;
	}

	auto layer = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[layerNum]);

	if (renderParams.BlendMode == AlphaBlendPremultiplied)
	{
		if (coreConf.CoreForcePremultipliedAlpha == 0 ||
			(coreConf.CoreForcePremultipliedAlpha == -1 &&
				(layer->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) != 0))
		{
			renderParams.BlendMode = AlphaBlendUnpremultiplied;
		}
	}

	renderParams.bInvertLayerAlpha = m_extensionData.bInverseAlphaExtensionEnabled &&
		(layer->layerFlags & XR_COMPOSITION_LAYER_INVERTED_ALPHA_BIT_EXT);

	renderParams.bEnableDepthRange = false;

	renderParams.bReadApplicationDepth = depthConf.DepthReadFromApplication;
	renderParams.bEnableDepthBlending = depthConf.DepthReadFromApplication &&
		((renderParams.bVarjoDepthEnabled &&
			(renderParams.BlendMode == AlphaBlendPremultiplied ||
				renderParams.BlendMode == AlphaBlendUnpremultiplied)) ||
			(renderParams.bUseFBPassthrough && renderParams.FBLayer->DepthEnabled) ||
			depthConf.DepthForceComposition);

	clientData.Values.bDepthBlendingActive = renderParams.bEnableDepthBlending;


	if (m_extensionData.bVarjoCompositionExtensionEnabled)
	{
		// Dirty hack: Casting away const
		XrCompositionLayerProjection* editableLayer = (XrCompositionLayerProjection*)frameEndInfo->layers[layerNum];
		auto header = (XrCompositionLayerDepthTestVARJO*)editableLayer->next;
		XrCompositionLayerDepthTestVARJO* prevHeader = nullptr;

		while (header != nullptr)
		{
			if (header->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_VARJO)
			{
				renderParams.bEnableDepthRange = true;
				renderParams.DepthRangeMin = header->depthTestRangeNearZ;
				renderParams.DepthRangeMax = header->depthTestRangeFarZ;

				// Hide header from runtime
				if (prevHeader)
				{
					prevHeader->next = header->next;
				}
				else
				{
					editableLayer->next = header->next;
				}
				break;
			}
			prevHeader = header;
			header = (XrCompositionLayerDepthTestVARJO*)header->next;
		}
	}

	if (depthConf.DepthForceRangeTest)
	{
		renderParams.bEnableDepthRange = true;
		renderParams.DepthRangeMin = depthConf.DepthForceRangeTestMin;
		renderParams.DepthRangeMax = depthConf.DepthForceRangeTestMax;
	}


	if (m_projectionMode == Projection_StereoReconstruction && m_depthReconstruction.get())
	{
		return RenderPassthroughWithDepth(frameEndInfo, layerNum, renderParams, mainConf);
	}
	else
	{
		return RenderPassthrough2D(frameEndInfo, layerNum, renderParams, mainConf);
	}
}


bool PassthroughSystem::RenderPassthrough2D(const XrFrameEndInfo* frameEndInfo, const uint32_t layerNum, FrameRenderParameters& renderParams, Config_Main& mainConf)
{
	FramePtr<CameraGPUFrame> gpuFrame = m_cameraManager->AcquireCameraGPUFrame();
	if (!gpuFrame.HasFrame())
	{
		return false;
	}

	ClientData& clientData = m_menuHandler->GetClientData();
	clientData.Values.LastCameraTimestamp = gpuFrame->FrameExposureTimestamp;

	uint64_t preRenderTime = m_passthroughRenderTime.StartPerfTimer();

	UVDistortionParameters& distParams = 
		(m_projectionMode == Projection_RoomView2D || !m_depthReconstruction.get()) ?
		m_dummyDistParams : m_depthReconstruction->GetDistortionParameters();

	if (mainConf.DebugTexture == DebugTexture_TestImage &&
		m_configManager->GetDebugTexture().CurrentTexture != DebugTexture_TestImage)
	{
		GetTestPattern(m_configManager->GetDebugTexture());
	}

	if (mainConf.ProjectToRenderModels)
	{
		UpdateRenderModels(gpuFrame->FrameExposureTimestamp);
		renderParams.RenderModels = m_renderModels;
	}


	// TODO: Holds on to the last two frames. Ideally we should detect when the render is complete instead.
	while (m_heldCameraFrames.size() >= 2)
	{
		m_cameraManager->ReleaseCameraGPUFrame(m_heldCameraFrames.front());
		m_heldCameraFrames.pop_front();
	}

	std::shared_ptr<CameraGPUFrame> sharedGPUFrame = gpuFrame.AcquireManualRead();
	m_heldCameraFrames.push_back(sharedGPUFrame);

	std::shared_ptr<DepthFrame> dummyDepthFrame;

	auto layer = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[layerNum]);


	CalculateFrameProjection(sharedGPUFrame, dummyDepthFrame, *layer, renderParams);

	m_inlineRenderer->RenderPassthroughFrame(layer, sharedGPUFrame, renderParams, dummyDepthFrame, distParams);



	m_passthroughRenderTime.EndPerfTimer();
	clientData.Values.RenderTimeMS = m_passthroughRenderTime.GetAverageTimeMS();

	m_frameToRenderTime.AveragesAddTimeInterval(sharedGPUFrame->FrameExposureTimestamp, preRenderTime);
	clientData.Values.FrameToRenderLatencyMS = m_frameToRenderTime.GetAverageTimeMS();

	m_frameToPhotonTime.AveragesAddTimeInterval(sharedGPUFrame->FrameExposureTimestamp, renderParams.DisplayTime);
	clientData.Values.FrameToPhotonsLatencyMS = m_frameToPhotonTime.GetAverageTimeMS();

	clientData.Values.DepthToRenderLatencyMS = 0.0f;
	clientData.Values.DepthToPhotonsLatencyMS = 0.0f;
	clientData.Values.StereoReconstructionTimeMS = 0.0f;
	clientData.Values.StereoRenderTimeMS = 0.0f;
	
	clientData.Values.GPUFrameRetrievalTimeMS = m_cameraManager->GetGPUFrameRetrievalPerfTime();
	clientData.Values.CPUFrameRetrievalTimeMS = m_cameraManager->GetCPUFrameRetrievalPerfTime();

	return true;
}


bool PassthroughSystem::RenderPassthroughWithDepth(const XrFrameEndInfo* frameEndInfo, const uint32_t layerNum, FrameRenderParameters& renderParams, Config_Main& mainConf)
{
	
	std::shared_ptr <ICameraManager>& cameraFrameManager =
		m_cameraProvider == CameraProvider_Augmented ?
		m_augmentedCameraManager : m_cameraManager;

	FramePtr<CameraGPUFrame> gpuFrame = cameraFrameManager->AcquireCameraGPUFrame();
	if (!gpuFrame.HasFrame())
	{
		return false;
	}

	FramePtr<DepthFrame> depthFrame = m_depthReconstruction->GetDepthFrame();
	if (!depthFrame.HasFrame())
	{
		return false;
	}

	ClientData& clientData = m_menuHandler->GetClientData();
	clientData.Values.LastCameraTimestamp = gpuFrame->FrameExposureTimestamp;

	uint64_t preRenderTime = m_passthroughRenderTime.StartPerfTimer();

	UVDistortionParameters& distParams = (m_projectionMode == Projection_RoomView2D || !m_depthReconstruction.get()) ?
		m_dummyDistParams :
		(m_cameraProvider == CameraProvider_Augmented ?
			m_augmentedDepthReconstruction->GetDistortionParameters() :
			m_depthReconstruction->GetDistortionParameters());

	if (mainConf.DebugTexture == DebugTexture_TestImage &&
		m_configManager->GetDebugTexture().CurrentTexture != DebugTexture_TestImage)
	{
		GetTestPattern(m_configManager->GetDebugTexture());
	}

	if (mainConf.ProjectToRenderModels)
	{
		UpdateRenderModels(gpuFrame->FrameExposureTimestamp);
		renderParams.RenderModels = m_renderModels;
	}

	// TODO: Holds on to the last two frames. Ideally we should detect when the render is complete instead.
	while (m_heldCameraFrames.size() >= 2)
	{
		cameraFrameManager->ReleaseCameraGPUFrame(m_heldCameraFrames.front());
		m_heldCameraFrames.pop_front();

		m_depthReconstruction->ReleaseDepthFrame(m_heldDepthFrames.front());
		m_heldDepthFrames.pop_front();
	}

	std::shared_ptr<CameraGPUFrame> sharedGPUFrame = gpuFrame.AcquireManualRead();
	m_heldCameraFrames.push_back(sharedGPUFrame);

	std::shared_ptr<DepthFrame> sharedDepthFrame = depthFrame.AcquireManualRead();
	m_heldDepthFrames.push_back(sharedDepthFrame);

	auto layer = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[layerNum]);


	CalculateFrameProjection(sharedGPUFrame, sharedDepthFrame, *layer, renderParams);

	m_inlineRenderer->RenderPassthroughFrame(layer, sharedGPUFrame, renderParams, sharedDepthFrame, distParams);



	m_passthroughRenderTime.EndPerfTimer();
	clientData.Values.RenderTimeMS = m_passthroughRenderTime.GetAverageTimeMS();

	m_frameToRenderTime.AveragesAddTimeInterval(sharedGPUFrame->FrameExposureTimestamp, preRenderTime);
	clientData.Values.FrameToRenderLatencyMS = m_frameToRenderTime.GetAverageTimeMS();

	m_frameToPhotonTime.AveragesAddTimeInterval(sharedGPUFrame->FrameExposureTimestamp, renderParams.DisplayTime);
	clientData.Values.FrameToPhotonsLatencyMS = m_frameToPhotonTime.GetAverageTimeMS();

	m_depthToRenderTime.AveragesAddTimeInterval(sharedDepthFrame->FrameExposureTimestamp, preRenderTime);
	clientData.Values.DepthToRenderLatencyMS = m_depthToRenderTime.GetAverageTimeMS();

	m_depthToPhotonTime.AveragesAddTimeInterval(sharedDepthFrame->FrameExposureTimestamp, renderParams.DisplayTime);
	clientData.Values.DepthToPhotonsLatencyMS = m_depthToPhotonTime.GetAverageTimeMS();

	clientData.Values.StereoReconstructionTimeMS = m_depthReconstruction->GetReconstructionPerfTime();
	clientData.Values.StereoRenderTimeMS = m_depthReconstruction->GetRenderPerfTime();

	clientData.Values.GPUFrameRetrievalTimeMS = m_cameraManager->GetGPUFrameRetrievalPerfTime();
	clientData.Values.CPUFrameRetrievalTimeMS = m_cameraManager->GetCPUFrameRetrievalPerfTime();

	return true;
}


void PassthroughSystem::OnPostRenderFrame(bool bDidRender, bool bInhibitIdle)
{
	if (bDidRender)
	{
		m_menuHandler->GetClientData().Values.LastFrameTimestamp = m_lastRenderTime.StartPerfTimer();
		m_menuHandler->DispatchClientDataValues();
	}
	else
	{
		if (m_menuHandler.get())
		{
			ClientData& data = m_menuHandler->GetClientData();
			data.Values.bCorePassthroughActive = false;
			data.Values.bFBPassthroughActive = false;
			data.Values.LastFrameTimestamp = GetCurrentTimeSytemTicks();

			m_menuHandler->DispatchClientDataValues();
		}

		float time = m_lastRenderTime.EndPerfTimerMS();

		Config_Main& mainConf = m_configManager->GetConfig_Main();
		
		if (!m_bIsPaused && !bInhibitIdle && mainConf.PauseImageHandlingOnIdle && time > mainConf.IdleTimeSeconds * 1000.0f)
		{
			m_bIsPaused = true;

			if (m_cameraProvider == CameraProvider_Augmented)
			{
				m_augmentedCameraManager->SetPaused(true);
			}
			m_cameraManager->SetPaused(true);

			if (mainConf.CloseCameraStreamOnPause)
			{
				if (m_cameraProvider == CameraProvider_Augmented)
				{
					m_augmentedCameraManager->DeinitCamera();
				}
				m_cameraManager->DeinitCamera();

				m_bCamerasInitialized = false;
			}
		}
	}
}

void PassthroughSystem::CalculateFrameProjection(std::shared_ptr<CameraGPUFrame> cameraFrame, std::shared_ptr<DepthFrame> depthFrame, const XrCompositionLayerProjection& layer, FrameRenderParameters& renderParams)
{
	CalculateHMDProjectionForEye(RenderEye_Left, layer, renderParams);
	CalculateHMDProjectionForEye(RenderEye_Right, layer, renderParams);

	// Detect the FOV being upside-down in order to prevent triangles from being backface culled
	renderParams.bMirrorRender = (layer.views[0].fov.angleUp - layer.views[0].fov.angleDown) < 0.0f;

	if (m_appRenderAPI == RenderAPI_OpenGL)
	{
		// Flip mirrored setting on OpenGL to get correct backface culling on rendering to upside down texture.
		renderParams.bMirrorRender = !renderParams.bMirrorRender;
	}

	renderParams.PrevHMDFrame_HMDEyeLeft = m_lastHMDFrame_HMDEyeLeft;
	renderParams.PrevHMDFrame_HMDEyeRight = m_lastHMDFrame_HMDEyeRight;

	m_lastHMDFrame_HMDEyeLeft = renderParams.HMDEyeLeft;
	m_lastHMDFrame_HMDEyeRight = renderParams.HMDEyeRight;


	if (cameraFrame->FrameSequence != m_lastFrameSequence)
	{
		m_lastFrameSequence = cameraFrame->FrameSequence;
		renderParams.bIsFirstRenderOfCameraFrame = true;

		renderParams.CameraLeft = cameraFrame->CameraLeft;
		renderParams.CameraRight = cameraFrame->CameraRight;
		
		if (m_cameraProvider == CameraProvider_Augmented)
		{
			m_augmentedDepthReconstruction->CalculateCameraProjection(cameraFrame, renderParams);
		}
		else if (m_projectionMode != Projection_RoomView2D)
		{
			m_depthReconstruction->CalculateCameraProjection(cameraFrame, renderParams);
		}

		m_currentCameraFrame_CameraLeft = renderParams.CameraLeft;
		m_currentCameraFrame_CameraRight= renderParams.CameraRight;	

		m_lastCameraFrame_HMDEyeLeft = m_currentCameraFrame_HMDEyeLeft;
		m_lastCameraFrame_HMDEyeRight = m_currentCameraFrame_HMDEyeRight;

		m_currentCameraFrame_HMDEyeLeft = renderParams.HMDEyeLeft;
		m_currentCameraFrame_HMDEyeRight = renderParams.HMDEyeRight;	
	}
	else // Previous HMD frame was rendered from the same camera frame
	{
		renderParams.bIsFirstRenderOfCameraFrame = false;

		renderParams.CameraLeft = m_currentCameraFrame_CameraLeft;
		renderParams.CameraRight = m_currentCameraFrame_CameraRight;
	}

	renderParams.ColorHistory_HMDEyeLeft = m_lastCameraFrame_HMDEyeLeft;
	renderParams.ColorHistory_HMDEyeRight = m_lastCameraFrame_HMDEyeRight;

}

void PassthroughSystem::CalculateHMDProjectionForEye(const ERenderEye eye, const XrCompositionLayerProjection& layer, FrameRenderParameters& renderParams)
{
	float nearZ = NEAR_PROJECTION_DISTANCE;
	float farZ = renderParams.ProjectionDistance * 1.5f;


	const XrCompositionLayerDepthInfoKHR* depthInfo = nullptr;
	renderParams.bHasReversedDepth = false;

	if (renderParams.bReadApplicationDepth)
	{
		depthInfo = (const XrCompositionLayerDepthInfoKHR*)layer.views[(eye == RenderEye_Left) ? 0 : 1].next;

		while (depthInfo != nullptr)
		{
			if (depthInfo->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
			{
				break;
			}
			depthInfo = (const XrCompositionLayerDepthInfoKHR*)depthInfo->next;
		}

		// Match the near and far plane with application
		if (depthInfo)
		{
			// Handle reversed depth
			if (depthInfo->farZ < depthInfo->nearZ)
			{
				nearZ = depthInfo->farZ;
				farZ = depthInfo->nearZ;
				renderParams.bHasReversedDepth = true;
			}
			else
			{
				nearZ = depthInfo->nearZ;
				farZ = depthInfo->farZ;
			}
		}
	}


	XrMatrix4x4f hmdProjection;
	XrMatrix4x4f_CreateProjectionFov(&hmdProjection, GRAPHICS_D3D, layer.views[(eye == RenderEye_Left) ? 0 : 1].fov, nearZ, farZ);

	// Handle infinite and reversed Z - XrMatrix4x4f_CreateProjectionFov sets it up wrong.
	if (depthInfo && (farZ == (std::numeric_limits<float>::max)() || !std::isfinite(farZ)))
	{
		hmdProjection.m[10] = 0;
		hmdProjection.m[14] = nearZ;
	}
	else if (depthInfo)
	{
		hmdProjection.m[10] = -(depthInfo->farZ * depthInfo->maxDepth - depthInfo->nearZ * depthInfo->minDepth) / (depthInfo->farZ - depthInfo->nearZ);
		hmdProjection.m[14] = -(depthInfo->farZ * depthInfo->nearZ * (depthInfo->maxDepth - depthInfo->minDepth)) / (depthInfo->farZ - depthInfo->nearZ);
	}

	if (m_appRenderAPI == RenderAPI_OpenGL)
	{
		// Flip vertical axis to render to upside down texture.
		hmdProjection.m[1] *= -1;
		hmdProjection.m[5] *= -1;
		hmdProjection.m[9] *= -1;
		hmdProjection.m[13] *= -1;
	}

	ProjectedView& hmdView = (eye == RenderEye_Left) ? renderParams.HMDEyeLeft : renderParams.HMDEyeRight;

	XrMatrix4x4f hmdWorldToView = GetHMDWorldToViewMatrix(eye, layer, renderParams.ReferenceSpace);

	XrMatrix4x4f_Invert(&hmdView.ViewToWorld, &hmdWorldToView);
	XrMatrix4x4f_Multiply(&hmdView.WorldToProjection, &hmdProjection, &hmdWorldToView);
}

// Constructs a matrix from the roomscale origin to the HMD eye pose.
XrMatrix4x4f PassthroughSystem::GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo)
{
	vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

	XrMatrix4x4f output, pose, viewToTracking, trackingToStage, refSpacePose;

	int viewNum = eye == RenderEye_Left ? 0 : 1;

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
		XrMatrix4x4f_Multiply(&output, &viewToTracking, &pose);
	}
	// TODO: Add cases for handling view and local floor
	else
	{
		XrMatrix4x4f_Multiply(&output, &viewToTracking, &refSpacePose);
	}

	return output;
}

void PassthroughSystem::UpdateRenderModels(const uint64_t cameraFrameTimestamp)
{
	vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
	vr::IVRRenderModels* vrRenderModels = m_openVRManager->GetVRRenderModels();

	static char modelName[vr::k_unMaxPropertyStringSize];

	int numDevices = 1;

	for (int i = 1; i < vr::k_unMaxTrackedDeviceCount; i++)
	{
		if (!vrSystem->IsTrackedDeviceConnected(i))
		{
			break;
		}

		numDevices = i;

		vr::TrackedPropertyError error;
		uint32_t numBytes = m_openVRManager->GetVRSystem()->GetStringTrackedDeviceProperty(i, vr::Prop_RenderModelName_String, modelName, vr::k_unMaxPropertyStringSize, &error);

		bool bFoundModel = false;

		for (RenderModel model : *m_renderModels)
		{
			if (model.deviceId == i)
			{
				if (strncmp(model.modelName.data(), modelName, numBytes) != 0)
				{
					vr::RenderModel_t* newModel;

					if (vrRenderModels->LoadRenderModel_Async(modelName, &newModel) == vr::VRRenderModelError_None)
					{
						model.modelName = modelName;
						MeshCreateRenderModel(model.mesh, newModel);
					}
				}

				bFoundModel = true;
				break;
			}
		}

		if (!bFoundModel)
		{
			vr::RenderModel_t* newModel;

			if (vrRenderModels->LoadRenderModel_Async(modelName, &newModel) == vr::VRRenderModelError_None)
			{
				RenderModel rm;
				rm.deviceId = i;
				rm.modelName = modelName;
				MeshCreateRenderModel(rm.mesh, newModel);
				m_renderModels->push_back(rm);
			}
		}
	}

	
	double exposureRelativeTime = -GetPerfTimeDiffSeconds(cameraFrameTimestamp, GetCurrentTimeSytemTicks());

	std::vector<vr::TrackedDevicePose_t> poses(numDevices);

	m_openVRManager->GetVRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, (float)exposureRelativeTime, poses.data(), numDevices);

	for (RenderModel& model : *m_renderModels.get())
	{
		model.meshToWorldTransform = ToXRMatrix4x4(poses[model.deviceId].mDeviceToAbsoluteTracking);
	}
}
