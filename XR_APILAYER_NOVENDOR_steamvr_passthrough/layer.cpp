// MIT License
//
// Copyright(c) 2023 Rectus
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "pch.h"
#include "layer.h"
#include "passthrough_renderer.h"
#include "camera_manager.h"
#include "config_manager.h"
#include "dashboard_menu.h"
#include "openvr_manager.h"
#include "depth_reconstruction.h"
#include <log.h>
#include <util.h>
#include <map>
#include <queue>
#include <shlobj_core.h>
#include <pathcch.h>
#include "lodepng.h"
#include "resource.h"

HMODULE g_dllModule = NULL;

// Directory under AppData to write config.
#define CONFIG_FILE_DIR L"\\OpenXR SteamVR Passthrough\\"
#define CONFIG_FILE_NAME L"config.ini"



namespace
{
    using namespace steamvr_passthrough;
    using namespace steamvr_passthrough::log;

    class OpenXrLayer : public steamvr_passthrough::OpenXrApi
    {
		public:

		OpenXrLayer() = default;

		~OpenXrLayer()
		{
			// Destroy renderer first so it can block on any outstanding rendering commands.

			m_Renderer.reset();
			m_depthReconstruction.reset();
			m_augmentedDepthReconstruction.reset();
			m_cameraManager.reset();
			m_augmentedCameraManager.reset();
			m_dashboardMenu.reset();
			m_openVRManager.reset();
		}

		XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo) override
		{
			if (createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO)
			{
			return XR_ERROR_VALIDATION_FAILURE;
			}

#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider,
					  "xrCreateInstance",
					  TLArg(xr::ToString(createInfo->applicationInfo.apiVersion).c_str(), "ApiVersion"),
					  TLArg(createInfo->applicationInfo.applicationName, "ApplicationName"),
					  TLArg(createInfo->applicationInfo.applicationVersion, "ApplicationVersion"),
					  TLArg(createInfo->applicationInfo.engineName, "EngineName"),
					  TLArg(createInfo->applicationInfo.engineVersion, "EngineVersion"),
					  TLArg(createInfo->createFlags, "CreateFlags"));

			for (uint32_t i = 0; i < createInfo->enabledApiLayerCount; i++)
			{
			TraceLoggingWrite(
				g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledApiLayerNames[i], "ApiLayerName"));
			}
			for (uint32_t i = 0; i < createInfo->enabledExtensionCount; i++)
			{
			TraceLoggingWrite(
				g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledExtensionNames[i], "ExtensionName"));
			}
#endif
			bool bEnableVulkan2Extension = false;
			bool bInverseAlphaExtensionEnabled = false;
			bool bEnableVarjoDepthExtension = false;
			bool bEnableVarjoCompositionExtension = false;

			std::vector<std::string> extensions = GetRequestedExtensions();
			for (uint32_t i = 0; i < extensions.size(); i++)
			{
				if (extensions[i].compare(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
				{
					bEnableVulkan2Extension = true;
				}
				else if (extensions[i].compare("XR_EXT_composition_layer_inverted_alpha") == 0)
				{
					bInverseAlphaExtensionEnabled = true;
				}
				else if (extensions[i].compare(XR_VARJO_ENVIRONMENT_DEPTH_ESTIMATION_EXTENSION_NAME) == 0)
				{
					bEnableVarjoDepthExtension = true;
				}
				else if (extensions[i].compare(XR_VARJO_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME) == 0)
				{
					bEnableVarjoCompositionExtension = true;
				}
			}
			
			XrResult result = OpenXrApi::xrCreateInstance(createInfo);

			if (result != XR_SUCCESS)
			{
				ErrorLog("xrCreateInstance returned error %i.\n", result);
				return result;
			}

			Log("Application %s creating OpenXR instance...\n", GetApplicationName().c_str());

#if USE_TRACELOGGING
			// Dump the application name and OpenXR runtime information to help debugging issues.
			XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
			OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties);
			const auto runtimeName = fmt::format("{} {}.{}.{}",
							 instanceProperties.runtimeName,
							 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
							 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
							 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
			TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(runtimeName.c_str(), "RuntimeName"));
			Log("Application: %s\n", GetApplicationName().c_str());
			Log("Using OpenXR runtime: %s\n", runtimeName.c_str());
#endif

#ifndef OPENVR_BUILD_STATIC
			// Try to load the OpenVR DLL from the same directory the current DLL is in.
			std::wstring dllPath(MAX_PATH, L'\0');
			if (FAILED(GetModuleFileNameW(g_dllModule, (LPWSTR)dllPath.c_str(), (DWORD)dllPath.size())))
			{
				ErrorLog("Error retreiving DLL path.\n");
			}

			std::wstring openVRPath = dllPath.substr(0, dllPath.find_last_of(L"/\\")) + L"\\openvr_api.dll";

			// If loading fails without error, hopefully it means the library is already loaded.
			if (LoadLibraryExW((LPWSTR)openVRPath.c_str(), NULL, 0) == nullptr && GetLastError() != 0)
			{
				ErrorLog("Error loading OpenVR DLL: %lu\n", GetLastError());
				return result;
			}
#endif

			PWSTR path;
			std::wstring filePath(PATHCCH_MAX_CCH, L'\0');

			SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
			lstrcpyW((PWSTR)filePath.c_str(), path);
			PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_DIR);
			CreateDirectoryW((PWSTR)filePath.data(), NULL);
			PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_NAME);

			m_configManager = std::make_shared<ConfigManager>(filePath);
			m_bIsInitialConfig = m_configManager->ReadConfigFile();


			// Check that the SteamVR OpenXR runtime is being used.
			if (m_configManager->GetConfig_Main().RequireSteamVRRuntime)
			{
				XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
				OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties);

				if (strncmp(instanceProperties.runtimeName, "SteamVR/OpenXR", 14))
				{
					ErrorLog("The active OpenXR runtime is %s, not SteamVR, passthrough layer not enabled\n", instanceProperties.runtimeName);
					return result;
				}
			}

			m_openVRManager = std::make_shared<OpenVRManager>();
			m_dashboardMenu = std::make_unique<DashboardMenu>(g_dllModule, m_configManager, m_openVRManager);


			if (bEnableVarjoDepthExtension && m_configManager->GetConfig_Extensions().ExtVarjoDepthEstimation)
			{
				m_bVarjoDepthExtensionEnabled = true;
				m_dashboardMenu->GetDisplayValues().bVarjoDepthEstimationExtensionActive = true;
				Log("Extension XR_VARJO_environment_depth_estimation enabled\n");
			}

			if (bEnableVarjoCompositionExtension && m_configManager->GetConfig_Extensions().ExtVarjoDepthComposition)
			{
				m_bVarjoCompositionExtensionEnabled = true;
				m_dashboardMenu->GetDisplayValues().bVarjoDepthCompositionExtensionActive = true;
				Log("Extension XR_VARJO_composition_layer_depth_test enabled\n");
			}

			m_bEnableVulkan2Extension = bEnableVulkan2Extension;
			m_bInverseAlphaExtensionEnabled = bInverseAlphaExtensionEnabled;

			m_bSuccessfullyLoaded = true;
			Log("OpenXR instance successfully created\n");

			return result;
		}

		XrResult xrGetVulkanDeviceExtensionsKHR(XrInstance instance, XrSystemId systemId, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer)
		{
			if (m_configManager->GetConfig_Main().UseLegacyVulkanRenderer)
			{
				return OpenXrApi::xrGetVulkanDeviceExtensionsKHR(instance, systemId, bufferCapacityInput, bufferCountOutput, buffer);
			}
			else
			{
				std::string exts = " VK_KHR_external_semaphore VK_KHR_external_semaphore_win32 VK_KHR_timeline_semaphore";

				if (bufferCapacityInput == 0)
				{
					XrResult res = OpenXrApi::xrGetVulkanDeviceExtensionsKHR(instance, systemId, bufferCapacityInput, bufferCountOutput, buffer);

					(*bufferCountOutput) += (uint32_t)exts.size();

					return res;
				}
				else
				{
					XrResult res = OpenXrApi::xrGetVulkanDeviceExtensionsKHR(instance, systemId, bufferCapacityInput, bufferCountOutput, buffer);

					if (bufferCapacityInput > exts.size() + 2)
					{
						strncpy(&buffer[bufferCapacityInput - exts.size() - 2], exts.c_str(), exts.size());
					}

					(*bufferCountOutput) += (uint32_t)exts.size();

					return res;
				}
			}
		}


		XrResult xrCreateVulkanDeviceKHR(XrInstance instance, const XrVulkanDeviceCreateInfoKHR* createInfo, VkDevice* vulkanDevice, VkResult* vulkanResult)
		{
			if (m_configManager->GetConfig_Main().UseLegacyVulkanRenderer)
			{
				return OpenXrApi::xrCreateVulkanDeviceKHR(instance, createInfo, vulkanDevice, vulkanResult);
			}
			else
			{
				std::vector<const char*> deviceExtensions;
				deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
				deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
				deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

				if (createInfo->vulkanCreateInfo->ppEnabledExtensionNames)
				{
					for (unsigned int i = 0; i < createInfo->vulkanCreateInfo->enabledExtensionCount; i++)
					{
						deviceExtensions.push_back(createInfo->vulkanCreateInfo->ppEnabledExtensionNames[i]);
					}
				}
				XrVulkanDeviceCreateInfoKHR newCreateInfo = *createInfo;

				VkBaseInStructure* base = (VkBaseInStructure*)createInfo->vulkanCreateInfo->pNext;
				bool bFoundFeatStruct = false;
				while (base != nullptr)
				{
					if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
					{
						bFoundFeatStruct = true;
						((VkPhysicalDeviceVulkan12Features*)base)->timelineSemaphore = true;
						break;
					}
					base = (VkBaseInStructure*)base->pNext;
				}

				VkDeviceCreateInfo newDeviceInfo = *createInfo->vulkanCreateInfo;
				newCreateInfo.vulkanCreateInfo = &newDeviceInfo;

				VkPhysicalDeviceVulkan12Features features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

				if (!bFoundFeatStruct)
				{
					features.timelineSemaphore = true;
					features.pNext = (void*)newDeviceInfo.pNext;
					newDeviceInfo.pNext = &features;
				}

				newDeviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
				newDeviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

				return OpenXrApi::xrCreateVulkanDeviceKHR(instance, &newCreateInfo, vulkanDevice, vulkanResult);
			}
		}


		XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override
		{
			if (m_configManager->GetConfig_Main().RequireSteamVRRuntime && !m_bSuccessfullyLoaded)
			{
				return OpenXrApi::xrGetSystem(instance, getInfo, systemId);
			}

			if (getInfo->type != XR_TYPE_SYSTEM_GET_INFO)
			{
				return XR_ERROR_VALIDATION_FAILURE;
			}

#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider,
					  "xrGetSystem",
					  TLPArg(instance, "Instance"),
					  TLArg(xr::ToCString(getInfo->formFactor), "FormFactor"));
#endif

			const XrResult result = OpenXrApi::xrGetSystem(instance, getInfo, systemId);
			if (XR_SUCCEEDED(result) && getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
			{
			if (*systemId != m_systemId)
			{
				XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
				OpenXrApi::xrGetSystemProperties(instance, *systemId, &systemProperties);

#if USE_TRACELOGGING
				TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg(systemProperties.systemName, "SystemName"));
#endif

				Log("Using OpenXR system: %s\n", systemProperties.systemName);
			}

			// Remember the XrSystemId to use.
			m_systemId = *systemId;
			}

#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg((int)*systemId, "SystemId"));
#endif

			return result;
		}


		bool SetupRenderer(const XrInstance instance, const XrSessionCreateInfo* createInfo, const XrSession* session)
		{
			const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);

			while (entry)
			{
				switch (entry->type)
				{
				case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
				{
					Log("Direct3D 11 renderer initializing...\n");

					const XrGraphicsBindingD3D11KHR* dx11bindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
					m_Renderer = std::make_shared<PassthroughRendererDX11>(dx11bindings->device, g_dllModule, m_configManager);
					m_renderAPI = DirectX11;
					m_appRenderAPI = DirectX11;

					if (!SetupProcessingPipeline())
					{
						return false;
					}

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = DirectX11;
					m_dashboardMenu->GetDisplayValues().appRenderAPI = DirectX11;
					m_bDepthSupportedByRenderer = true;
					Log("Direct3D 11 renderer initialized\n");

					return true;
				}

				case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR:
				{
					Log("Direct3D 12 renderer initializing...\n");

					const XrGraphicsBindingD3D12KHR* dx12bindings = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry);

					ERenderAPI usedAPI = DirectX11;

					if (m_configManager->GetConfig_Main().UseLegacyD3D12Renderer)
					{
						m_Renderer = std::make_unique<PassthroughRendererDX12>(dx12bindings->device, dx12bindings->queue, g_dllModule, m_configManager);
						usedAPI = DirectX12;
						Log("Using legacy Direct3D 12 renderer\n");
					}
					else
					{
						m_Renderer = std::make_unique<PassthroughRendererDX11Interop>(dx12bindings->device, dx12bindings->queue, g_dllModule, m_configManager);
					}

					m_renderAPI = usedAPI;
					m_appRenderAPI = DirectX12;

					if (!SetupProcessingPipeline())
					{
						return false;
					}

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = usedAPI;
					m_dashboardMenu->GetDisplayValues().appRenderAPI = DirectX12;
					m_bDepthSupportedByRenderer = true;
					Log("Direct3D 12 renderer initialized\n");

					return true;
				}

				case XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR: // same as XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR
				{
					Log("Vulkan renderer initializing...\n");

					ERenderAPI usedAPI = Vulkan;

					const XrGraphicsBindingVulkanKHR* vulkanbindings = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(entry);
					if (m_configManager->GetConfig_Main().UseLegacyVulkanRenderer)
					{

						m_Renderer = std::make_unique<PassthroughRendererVulkan>(*vulkanbindings, g_dllModule, m_configManager);
					}
					else
					{
						if (!m_bEnableVulkan2Extension)
						{
							ErrorLog("The XR_KHR_vulkan_enable extension is only supported with the legacy renderer, passthrough rendering not enabled\n");
							return false;
						}

						usedAPI = DirectX11;
						m_Renderer = std::make_unique<PassthroughRendererDX11Interop>(*vulkanbindings, g_dllModule, m_configManager);
					}

					m_renderAPI = usedAPI;
					m_appRenderAPI = Vulkan;

					if (!SetupProcessingPipeline())
					{
						return false;
					}

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = usedAPI;
					m_dashboardMenu->GetDisplayValues().appRenderAPI = Vulkan;
					m_bDepthSupportedByRenderer = false;
					Log("Vulkan renderer initialized\n");

					return true;
				}

				default:

					entry = reinterpret_cast<const XrBaseInStructure*>(entry->next);
				}
			}
			Log("Passthrough API layer: No supported graphics APIs detected!\n");
			return false;
		}

		bool SetupProcessingPipeline()
		{
			uint32_t cameraTextureWidth;
			uint32_t cameraTextureHeight;
			uint32_t cameraFrameBufferSize;
			uint32_t cameraUndistortedTextureWidth;
			uint32_t cameraUndistortedTextureHeight;
			uint32_t cameraUndistortedFrameBufferSize;
			
			m_depthReconstruction.reset();
			m_augmentedDepthReconstruction.reset();
			m_cameraManager.reset();
			m_augmentedCameraManager.reset();

			if (!m_Renderer.get())
			{
				ErrorLog("Trying to initialize processing pipeline without renderer!\n");
				return false;
			}

			Config_Main& mainConfig = m_configManager->GetConfig_Main();

			m_bIsPaused = false;
			m_lastRenderTime = StartPerfTimer();

			if (mainConfig.CameraProvider == CameraProvider_OpenVR)
			{
				m_cameraManager = std::make_shared<CameraManagerOpenVR>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager);

				if (!m_cameraManager->InitCamera())
				{
					ErrorLog("Failed to initialize camera!\n");
					return false;
				}

				if (m_bIsInitialConfig)
				{
					m_bIsInitialConfig = false;

					// Default to stereo mode if we have a compatible headset.
					// TODO: Just checks for the fisheye model at the moment, whitelist of known models would be better.
					if (m_cameraManager->GetFrameLayout() != Mono && m_cameraManager->IsUsingFisheyeModel())
					{
						m_configManager->GetConfig_Main().ProjectionMode = Projection_StereoReconstruction;
					}
				}

				MenuDisplayValues& vals = m_dashboardMenu->GetDisplayValues();
				m_cameraManager->GetCameraDisplayStats(vals.CameraFrameWidth, vals.CameraFrameHeight, vals.CameraFrameRate, vals.CameraAPI);

				m_cameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_cameraManager->GetUndistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}
			else if (mainConfig.CameraProvider == CameraProvider_Augmented)
			{
				m_cameraManager = std::make_shared<CameraManagerOpenVR>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager);
				m_augmentedCameraManager = std::make_shared<CameraManagerOpenCV>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, true);

				if (!m_cameraManager->InitCamera() || !m_augmentedCameraManager->InitCamera())
				{
					ErrorLog("Failed to initialize camera!\n");
					return false;
				}
				MenuDisplayValues& vals = m_dashboardMenu->GetDisplayValues();
				m_augmentedCameraManager->GetCameraDisplayStats(vals.CameraFrameWidth, vals.CameraFrameHeight, vals.CameraFrameRate, vals.CameraAPI);

				m_augmentedCameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_augmentedCameraManager->GetDistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}
			else
			{
				m_cameraManager = std::make_shared<CameraManagerOpenCV>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager);

				if (!m_cameraManager->InitCamera())
				{
					ErrorLog("Failed to initialize camera!\n");
					return false;
				}
				MenuDisplayValues& vals = m_dashboardMenu->GetDisplayValues();
				m_cameraManager->GetCameraDisplayStats(vals.CameraFrameWidth, vals.CameraFrameHeight, vals.CameraFrameRate, vals.CameraAPI);

				m_cameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_cameraManager->GetDistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}

			m_bCamerasInitialized = true;

			m_Renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize, cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			if (!m_Renderer->InitRenderer())
			{
				ErrorLog("Failed to initialize renderer!\n");
				return false;
			}


			m_depthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_cameraManager);

			if (m_configManager->GetConfig_Main().CameraProvider == CameraProvider_Augmented)
			{
				m_augmentedDepthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_augmentedCameraManager);
			}

			return true;
		}

		void ResetRenderer()
		{
			m_swapChainLeft = XR_NULL_HANDLE;
			m_swapChainRight = XR_NULL_HANDLE;
			m_depthSwapChainLeft = XR_NULL_HANDLE;
			m_depthSwapChainRight = XR_NULL_HANDLE;

			SetupProcessingPipeline();
		}


		XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) override
		{
			if (m_configManager->GetConfig_Main().RequireSteamVRRuntime && !m_bSuccessfullyLoaded)
			{
				return OpenXrApi::xrCreateSession(instance, createInfo, session);
			}

			if (createInfo->type != XR_TYPE_SESSION_CREATE_INFO)
			{
				return XR_ERROR_VALIDATION_FAILURE;
			}

#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider,
					  "xrCreateSession",
					  TLPArg(instance, "Instance"),
					  TLArg((int)createInfo->systemId, "SystemId"),
					  TLArg(createInfo->createFlags, "CreateFlags"));
#endif

			XrResult result = OpenXrApi::xrCreateSession(instance, createInfo, session);
			if (XR_SUCCEEDED(result))
			{
				if (!m_bSuccessfullyLoaded)
				{
					ErrorLog("Loading failed, not attaching to session!\n");
				}
				else if (isSystemHandled(createInfo->systemId) && !isCurrentSession(*session))
				{
					m_currentInstance = instance;
					m_currentSession = *session;
					if (SetupRenderer(instance, createInfo, session))
					{
						Log("Passthrough API layer enabled for session\n");
						m_bUsePassthrough = m_configManager->GetConfig_Main().EnablePassthrough;
						m_dashboardMenu->GetDisplayValues().currentApplication = GetApplicationName();
					}
					else
					{
						m_bUsePassthrough = false;
						m_currentSession = XR_NULL_HANDLE;
						ErrorLog("Failed to initialize rendering system!\n");
					}
				}

#if USE_TRACELOGGING
				TraceLoggingWrite(g_traceProvider, "xrCreateSession", TLPArg(*session, "Session"));
#endif
			}

			return result;
		}


		XrResult xrDestroySession(XrSession session)
		{
			if (isCurrentSession(session))
			{
				Log("Passthrough session ending...\n");
				
				m_depthReconstruction.reset();
				m_augmentedDepthReconstruction.reset();
				m_cameraManager.reset();
				m_augmentedCameraManager.reset();

				m_Renderer.reset();

				m_currentSession = XR_NULL_HANDLE;
				m_currentInstance = XR_NULL_HANDLE;

				m_dashboardMenu->GetDisplayValues().bSessionActive = false;
				m_dashboardMenu->GetDisplayValues().renderAPI = None;
				m_dashboardMenu->GetDisplayValues().frameBufferFlags = 0;
				m_dashboardMenu->GetDisplayValues().frameBufferFormat = 0;
				m_dashboardMenu->GetDisplayValues().depthBufferFormat = 0;
				m_dashboardMenu->GetDisplayValues().frameBufferWidth = 0;
				m_dashboardMenu->GetDisplayValues().frameBufferHeight = 0;
				m_dashboardMenu->GetDisplayValues().frameToPhotonsLatencyMS = 0;
				m_dashboardMenu->GetDisplayValues().frameToRenderLatencyMS = 0;
				m_dashboardMenu->GetDisplayValues().renderTimeMS = 0;

				m_dashboardMenu->GetDisplayValues().bCorePassthroughActive = false;
				m_dashboardMenu->GetDisplayValues().CoreCurrentMode = 0;
			}

			XrResult result = OpenXrApi::xrDestroySession(session);

			return result;
		}


		XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId,	XrViewConfigurationType viewConfigurationType, uint32_t environmentBlendModeCapacityInput, uint32_t* environmentBlendModeCountOutput, XrEnvironmentBlendMode* environmentBlendModes)
		{
			const XrResult result = OpenXrApi::xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);

			// Ignore adding modes if not a regular stereo view.
			if (!isSystemHandled(systemId) || !m_bSuccessfullyLoaded || !XR_SUCCEEDED(result) || viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
			{
				return result;
			}

			bool additiveEnabled = false;
			bool alphaEnabled = false;
			unsigned numBlendModes = 1;
			if (m_configManager->GetConfig_Core().CoreAdditive) 
			{ 
				additiveEnabled = true;
				numBlendModes++;
			}

			if (m_configManager->GetConfig_Core().CoreAlphaBlend)
			{
				alphaEnabled = true;
				numBlendModes++;
			}
		
			if (environmentBlendModeCapacityInput < 1) {
				if (*environmentBlendModeCountOutput < numBlendModes)
				{
					*environmentBlendModeCountOutput = numBlendModes;
				}
				return XR_SUCCESS;
			}

			if (environmentBlendModeCapacityInput < numBlendModes)
			{
				*environmentBlendModeCountOutput = numBlendModes;
				return XR_ERROR_SIZE_INSUFFICIENT;
			}

			int pref = m_configManager->GetConfig_Core().CorePreferredMode;

			if (pref == 3 && alphaEnabled)
			{
				environmentBlendModes[0] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;

				if (additiveEnabled)
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
					environmentBlendModes[2] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				}
				else
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				}
			}
			else if (pref == 2 && additiveEnabled)
			{
				environmentBlendModes[0] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;

				if (alphaEnabled)
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
					environmentBlendModes[2] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				}
				else
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				}
			}
			else
			{
				environmentBlendModes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

				if (additiveEnabled && alphaEnabled)
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
					environmentBlendModes[2] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
				}
				else if (additiveEnabled)
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
				}
				else if (alphaEnabled)
				{
					environmentBlendModes[1] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
				}
			}

			if (*environmentBlendModeCountOutput < numBlendModes)
			{
				*environmentBlendModeCountOutput = numBlendModes;
			}
			return XR_SUCCESS;
		}


		XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) 
		{
			if (!isCurrentSession(session))
			{
				return OpenXrApi::xrCreateReferenceSpace(session, createInfo, space);
			}

			XrResult result = OpenXrApi::xrCreateReferenceSpace(session, createInfo, space);
			if (XR_SUCCEEDED(result))
			{
				m_refSpaces[*space] = *createInfo;
			}

			return result;
		}


		XrResult xrDestroySpace(XrSpace space)
		{
			m_refSpaces.erase(space);

			return OpenXrApi::xrDestroySpace(space);
		}


		XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain)
		{
			if (!isCurrentSession(session))
			{
				return OpenXrApi::xrCreateSwapchain(session, createInfo, swapchain);
			}

			XrSwapchainCreateInfo newCreateInfo = *createInfo;
			
			if (m_appRenderAPI == Vulkan && m_renderAPI == DirectX11)
			{
				newCreateInfo.usageFlags |= (XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT);
			}

			XrResult result = OpenXrApi::xrCreateSwapchain(session, &newCreateInfo, swapchain);
			if (XR_SUCCEEDED(result))
			{
				m_swapchainProperties[*swapchain] = *createInfo;
			}
			return result;
		}


		XrResult xrDestroySwapchain(XrSwapchain swapchain)
		{
			m_acquiredSwapchains.erase(swapchain);
			m_waitedSwapchains.erase(swapchain);
			m_heldSwapchains.erase(swapchain);
			m_swapchainProperties.erase(swapchain);

			return OpenXrApi::xrDestroySwapchain(swapchain);
		}


		XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index)
		{
			if (!m_swapchainProperties.contains(swapchain))
			{
				return OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);
			}

			auto acq = m_acquiredSwapchains.find(swapchain);
			if (acq == m_acquiredSwapchains.end())
			{
				m_acquiredSwapchains.emplace(swapchain, std::queue<uint32_t>());
			}

			auto waited = m_waitedSwapchains.find(swapchain);
			if (waited == m_waitedSwapchains.end())
			{
				m_waitedSwapchains.emplace(swapchain, false);
			}

			auto held = m_heldSwapchains.find(swapchain);
			if (held == m_heldSwapchains.end())
			{
				m_heldSwapchains.emplace(swapchain, std::queue<uint32_t>());
			}

			XrResult result = OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);
			if (XR_SUCCEEDED(result))
			{
				if (m_acquiredSwapchains.find(swapchain)->second.empty())
				{
					m_waitedSwapchains[swapchain] = false;
				}

				m_acquiredSwapchains[swapchain].push(*index);
			}
			else
			{
				ErrorLog("Error in xrAcquireSwapchainImage: %i\n", result);
			}

			return result;
		}


		XrResult xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* waitInfo)
		{
			if (!m_swapchainProperties.contains(swapchain))
			{
				return OpenXrApi::xrWaitSwapchainImage(swapchain, waitInfo);
			}
			else
			{
				// Release any held swapchain before we wait on the next, in order to comply to spec.
				// This may prevent passthrough rendering if the app calls xrEndFrame before releasing another swapchain.
				auto held = m_heldSwapchains.find(swapchain);
				if (held != m_heldSwapchains.end() && !held->second.empty())
				{
					ErrorLog("App waiting on a second swapchain per-frame!\n");
					held->second.pop();

					OpenXrApi::xrReleaseSwapchainImage(swapchain, nullptr);
				}

				XrResult result = OpenXrApi::xrWaitSwapchainImage(swapchain, waitInfo);

				if (XR_SUCCEEDED(result))
				{
					if (m_waitedSwapchains.find(swapchain) != m_waitedSwapchains.end())
					{
						m_waitedSwapchains[swapchain] = true;
					}
				}
				return result;
			}
		}


		XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo)
		{
			if (!m_swapchainProperties.contains(swapchain))
			{
				return OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
			}

			// Delay releasing the swapchains until we can render the passthrough.
			auto acq = m_acquiredSwapchains.find(swapchain);
			if (acq != m_acquiredSwapchains.end() && !acq->second.empty())
			{
				// If passthrough is disabled, or the swapchain is being released without being waited on, we invalidate and let the runtime deal with it.
				if (!m_bUsePassthrough || m_waitedSwapchains.find(swapchain) != m_waitedSwapchains.end() && !m_waitedSwapchains[swapchain])
				{
					acq->second.pop();

					return OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
				}
				else
				{
					m_waitedSwapchains[swapchain] = false;
					m_heldSwapchains[swapchain].push(acq->second.front());
					acq->second.pop();
					
					return XR_SUCCESS;
				}
			}
			else
			{
				DebugLog("Swapchain not acquired: %i\n", swapchain);
				return OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
			}
		}


		XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
		{
			m_bUsePassthrough = isCurrentSession(session) && m_configManager->GetConfig_Main().EnablePassthrough;

			XrResult result = OpenXrApi::xrBeginFrame(session, frameBeginInfo);

			if (isCurrentSession(session) && result == XR_SUCCESS)
			{
				m_bBeginframeCalled = true;
			}

			return result;
		}


		void UpdateSwapchains(const ERenderEye eye, const XrCompositionLayerProjection* layer, FrameRenderParameters& renderParams)
		{
			XrSwapchain* storedSwapchain = (eye == LEFT_EYE) ? &m_swapChainLeft : &m_swapChainRight;
			XrSwapchain* storedDepthSwapchain = (eye == LEFT_EYE) ? &m_depthSwapChainLeft : &m_depthSwapChainRight;
			int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

			const XrSwapchain newSwapchain = layer->views[viewIndex].subImage.swapchain;

			auto props = m_swapchainProperties.find(newSwapchain);

			if (props == m_swapchainProperties.end())
			{
				return;
			}

			int64_t imageFormat = props->second.format;

			auto held = m_heldSwapchains.find(newSwapchain);
			if (held == m_heldSwapchains.end() || held->second.empty())
			{
				return;
			}

			int imageIndex = held->second.back();
			
			if (eye == LEFT_EYE)
			{
				m_dashboardMenu->GetDisplayValues().frameBufferFormat = props->second.format;
				renderParams.LeftFrameIndex = imageIndex;
			}
			else
			{
				renderParams.RightFrameIndex = imageIndex;
			}

			if (newSwapchain != *storedSwapchain)
			{
				Log("Updating swapchain %u to %u with eye %u, index %u, arraySize %u\n", *storedSwapchain, newSwapchain, eye, imageIndex, props->second.arraySize);

				XrSwapchainImageD3D12KHR swapchainImages[3];
				uint32_t numImages = 0;

				XrResult result = OpenXrApi::xrEnumerateSwapchainImages(newSwapchain, 3, &numImages, (XrSwapchainImageBaseHeader*)swapchainImages);
				if (XR_SUCCEEDED(result))
				{
					for (uint32_t i = 0; i < numImages; i++)
					{
						m_Renderer->InitRenderTarget(eye, swapchainImages[i].texture, i, props->second);
					}
					*storedSwapchain = newSwapchain;
				}
				else
				{
					ErrorLog("Error in xrEnumerateSwapchainImages: %i\n", result);

					if (eye == LEFT_EYE)
					{
						renderParams.LeftFrameIndex = -1;
					}
					else
					{
						renderParams.RightFrameIndex = -1;
					}
					return;
				}
			}

			if (!m_configManager->GetConfig_Depth().DepthReadFromApplication)
			{
				return;
			}

			// Find associated depth swapchain if one exists.
			auto depthInfo = (const XrCompositionLayerDepthInfoKHR*) layer->views[viewIndex].next;

			while (depthInfo != nullptr)
			{
				if (depthInfo->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
				{
					break;
				}
				depthInfo = (const XrCompositionLayerDepthInfoKHR*) depthInfo->next;
			}

			if (depthInfo != nullptr && depthInfo->subImage.swapchain != *storedDepthSwapchain)
			{
				auto depthProps = m_swapchainProperties.find(depthInfo->subImage.swapchain);

				if (depthProps != m_swapchainProperties.end())
				{
					if (eye == LEFT_EYE)
					{
						m_dashboardMenu->GetDisplayValues().depthBufferFormat = depthProps->second.format;
					}

					Log("Found depth swapchain %u for color swapchain %u, arraySize %u, depth range [%f:%f], Z-range[%g:%g]\n", depthInfo->subImage.swapchain, newSwapchain, depthProps->second.arraySize, depthInfo->minDepth, depthInfo->maxDepth, depthInfo->nearZ, depthInfo->farZ);

					XrSwapchainImageD3D12KHR depthImages[3];
					uint32_t numImages = 0;

					XrResult result = OpenXrApi::xrEnumerateSwapchainImages(depthInfo->subImage.swapchain, 3, &numImages, (XrSwapchainImageBaseHeader*)depthImages);
					if (XR_SUCCEEDED(result))
					{
						for (uint32_t i = 0; i < numImages; i++)
						{
							m_Renderer->InitDepthBuffer(eye, depthImages[i].texture, i, depthProps->second);
						}
						*storedDepthSwapchain = depthInfo->subImage.swapchain;
					}
					else
					{
						ErrorLog("Error in xrEnumerateSwapchainImages when enumerating depthbuffers: %i\n", result);
					}
				}
			}

			if (depthInfo != nullptr)
			{
				auto depth = m_heldSwapchains.find(*storedDepthSwapchain);
				if (depth != m_heldSwapchains.end() && !depth->second.empty())
				{
					if (eye == LEFT_EYE)
					{
						renderParams.LeftDepthIndex = depth->second.back();
					}
					else
					{
						renderParams.RightDepthIndex = depth->second.back();
					}
				}
				else
				{
					ErrorLog("Error: No valid depth swapchain found!\n");
				}
			}
		}


		bool IsBlendModeEnabled(XrEnvironmentBlendMode blendMode, const XrCompositionLayerProjection* layer)
		{
			Config_Core& conf = m_configManager->GetConfig_Core();
			if (conf.CorePassthroughEnable)
			{
				if (conf.CoreForcePassthrough) { return true; }

				if ((conf.CoreAlphaBlend && blendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND &&
					layer->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) ||
					(conf.CoreAdditive && blendMode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE))
				{
					return true;
				}
			}
			return false;
		}

		void GetTestPattern(DebugTexture& texture)
		{
			HRSRC resInfo = FindResource(g_dllModule, MAKEINTRESOURCE(IDB_PNG_TESTPATTERN), L"PNG");
			if (resInfo == nullptr)
			{
				ErrorLog("Error finding test pattern resource.\n");
				return;
			}
			HGLOBAL memory = LoadResource(g_dllModule, resInfo);
			if (memory == nullptr)
			{
				ErrorLog("Error loading test pattern resource.\n");
				return;
			}
			size_t data_size = SizeofResource(g_dllModule, resInfo);
			void* data = LockResource(memory);

			if (data == nullptr)
			{
				ErrorLog("Error reading test pattern resource.\n");
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
				ErrorLog("Error decoding test pattern.\n");
				return;
			}

			texture.Texture.resize(width * height * 4);

			texture.Height = height;
			texture.Width = width;
			texture.PixelSize = 4;
			texture.Format = DebugTextureFormat_RGBA8;
			texture.CurrentTexture = DebugTexture_TestImage;
			texture.bDimensionsUpdated = true;

			return;
		}

		void RenderPassthroughOnAppLayer(const XrFrameEndInfo* frameEndInfo, uint32_t layerNum)
		{
			XrCompositionLayerProjection* layer = (XrCompositionLayerProjection*)frameEndInfo->layers[layerNum];
			std::shared_ptr<CameraFrame> frame;

			m_dashboardMenu->GetDisplayValues().frameBufferHeight = layer->views[0].subImage.imageRect.extent.height;
			m_dashboardMenu->GetDisplayValues().frameBufferWidth = layer->views[0].subImage.imageRect.extent.width;
			m_dashboardMenu->GetDisplayValues().frameBufferFlags = layer->layerFlags;

			if (m_configManager->GetConfig_Main().CameraProvider == CameraProvider_Augmented)
			{
				if (!m_augmentedCameraManager->GetCameraFrame(frame))
				{
					return;
				}
			}
			else
			{
				if (!m_cameraManager->GetCameraFrame(frame))
				{
					return;
				}
			}

			std::shared_lock readLock(frame->readWriteMutex);


			LARGE_INTEGER preRenderTime = StartPerfTimer();

			float frameToRenderTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, preRenderTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().frameToRenderLatencyMS = UpdateAveragePerfTime(m_frameToRenderTimes, frameToRenderTime, 20);

			LARGE_INTEGER displayTime;

			OpenXrApi::xrConvertTimeToWin32PerformanceCounterKHR(m_currentInstance, frameEndInfo->displayTime, &displayTime);

			float frameToPhotonsTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, displayTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().frameToPhotonsLatencyMS = UpdateAveragePerfTime(m_frameToPhotonTimes, frameToPhotonsTime, 20);


			float timeToPhotons = GetPerfTimerDiff(preRenderTime.QuadPart, displayTime.QuadPart);
			
			Config_Depth& depthConf = m_configManager->GetConfig_Depth();

			std::shared_ptr<DepthFrame> depthFrame = m_depthReconstruction->GetDepthFrame();
			if (depthFrame.get())
			{
				std::shared_lock depthReadLock(depthFrame->readWriteMutex);
			}		

			if (m_configManager->GetConfig_Main().CameraProvider == CameraProvider_Augmented)
			{
				m_augmentedCameraManager->CalculateFrameProjection(frame, depthFrame, *layer, timeToPhotons, m_refSpaces[layer->space], m_augmentedDepthReconstruction->GetDistortionParameters());
			}
			else
			{
				m_cameraManager->CalculateFrameProjection(frame, depthFrame, *layer, timeToPhotons, m_refSpaces[layer->space], m_depthReconstruction->GetDistortionParameters());
			}

			FrameRenderParameters renderParams;

			UpdateSwapchains(LEFT_EYE, layer, renderParams);
			UpdateSwapchains(RIGHT_EYE, layer, renderParams);

			if (renderParams.LeftFrameIndex < 0 || renderParams.RightFrameIndex < 0)
			{
				ErrorLog("Error: No swapchains found!\n");
				return;
			}

			renderParams.BlendMode = (EPassthroughBlendMode)frameEndInfo->environmentBlendMode;

			if (m_configManager->GetConfig_Core().CoreForcePassthrough && m_configManager->GetConfig_Core().CoreForceMode >= 0)
			{
				renderParams.BlendMode = (EPassthroughBlendMode)m_configManager->GetConfig_Core().CoreForceMode;
			}

			if (renderParams.BlendMode == AlphaBlendPremultiplied && 
				(layer->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT))
			{
				renderParams.BlendMode = AlphaBlendUnpremultiplied;
			}

			renderParams.bInvertLayerAlpha =  m_bInverseAlphaExtensionEnabled &&
				(layer->layerFlags & XR_COMPOSITION_LAYER_INVERTED_ALPHA_BIT_EXT);

			if (m_configManager->GetConfig_Main().DebugTexture == DebugTexture_TestImage &&
				m_configManager->GetDebugTexture().CurrentTexture != DebugTexture_TestImage)
			{
				GetTestPattern(m_configManager->GetDebugTexture());
			}		

			renderParams.bEnableDepthRange = false;			

			renderParams.bEnableDepthBlending = depthConf.DepthReadFromApplication &&
					((m_bVarjoDepthEnabled && 
					(renderParams.BlendMode == AlphaBlendPremultiplied ||
					renderParams.BlendMode == AlphaBlendUnpremultiplied)) ||
				depthConf.DepthForceComposition);

			m_dashboardMenu->GetDisplayValues().bDepthBlendingActive = renderParams.bEnableDepthBlending;

			
			if (m_bVarjoCompositionExtensionEnabled)
			{
				auto header = (XrCompositionLayerDepthTestVARJO*)layer->next;
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
							layer->next = header->next;
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

			UVDistortionParameters& distParams =
				m_configManager->GetConfig_Main().CameraProvider == CameraProvider_Augmented ?
				m_augmentedDepthReconstruction->GetDistortionParameters() :
				m_depthReconstruction->GetDistortionParameters();


			m_Renderer->RenderPassthroughFrame(layer, frame.get(), renderParams, depthFrame, distParams);

			depthFrame->bIsFirstRender = false;


			float renderTime = EndPerfTimer(preRenderTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().renderTimeMS = UpdateAveragePerfTime(m_passthroughRenderTimes, renderTime, 20);

			m_dashboardMenu->GetDisplayValues().stereoReconstructionTimeMS = m_depthReconstruction->GetReconstructionPerfTime();
			m_dashboardMenu->GetDisplayValues().frameRetrievalTimeMS = m_cameraManager->GetFrameRetrievalPerfTime();
		}


		XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) 
		{
			if (!isCurrentSession(session))
			{
				// Strip out blend mode even when we aren't handling the session.
				XrFrameEndInfo modifiedFrameEndInfo = *frameEndInfo;
				modifiedFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

				return OpenXrApi::xrEndFrame(session, &modifiedFrameEndInfo);
			}

			Config_Main& mainConfig = m_configManager->GetConfig_Main();

			bool bResetPending = false;

			if (m_Renderer.get() &&m_configManager->CheckResetRendererResetPending())
			{
				bResetPending = true;
			}


			XrResult result;

			if (m_dashboardMenu.get())
			{
				m_dashboardMenu->GetDisplayValues().CoreCurrentMode = frameEndInfo->environmentBlendMode;
				m_dashboardMenu->GetDisplayValues().bCorePassthroughActive = false;
			}

			bool bInvalidEndFrame = false;

			if (frameEndInfo->displayTime == 0 || !m_bBeginframeCalled)
			{
				bInvalidEndFrame = true;
			}

			m_bBeginframeCalled = false;

			bool bDidRender = false;
			if (m_bUsePassthrough && !bResetPending && !bInvalidEndFrame)
			{
				for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
				{
					if (frameEndInfo->layers[i] != nullptr && frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && IsBlendModeEnabled(frameEndInfo->environmentBlendMode, (const XrCompositionLayerProjection*)frameEndInfo->layers[i]))
					{
						if (m_bIsPaused)
						{
							if (mainConfig.CameraProvider == CameraProvider_Augmented)
							{
								m_augmentedCameraManager->SetPaused(false);
							}
							m_cameraManager->SetPaused(false);

							if (!m_bCamerasInitialized)
							{
								if (mainConfig.CameraProvider == CameraProvider_Augmented)
								{
									if ((!m_cameraManager->InitCamera() || !m_augmentedCameraManager->InitCamera()))
									{
										ErrorLog("Failed to reinitialize camera!\n");
										break;
									}
								}
								else if(!m_cameraManager->InitCamera())
								{
									ErrorLog("Failed to reinitialize camera!\n");
									break;
								}
								m_bCamerasInitialized = true;
							}
							m_bIsPaused = false;
						}

						m_dashboardMenu->GetDisplayValues().bCorePassthroughActive = true;
						RenderPassthroughOnAppLayer(frameEndInfo, i);
						bDidRender = true;

						break;
					}
				}
			}

			for (auto& held : m_heldSwapchains)
			{
				while (!held.second.empty())
				{
					held.second.pop();
					m_waitedSwapchains[held.first] = false;
					result = OpenXrApi::xrReleaseSwapchainImage(held.first, nullptr);
					if (XR_FAILED(result))
					{
						ErrorLog("Error in xrReleaseSwapchainImage: %i\n", result);
					}
				}
			}

			m_heldSwapchains.clear();


			XrFrameEndInfo modifiedFrameEndInfo = *frameEndInfo;
			modifiedFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

			result = OpenXrApi::xrEndFrame(session, &modifiedFrameEndInfo);

			if (bResetPending)
			{
				ResetRenderer();
			}
			else if (bDidRender)
			{
				m_lastRenderTime = StartPerfTimer();
			}
			else
			{
				float time = EndPerfTimer(m_lastRenderTime);
				if (!m_bIsPaused && mainConfig.PauseImageHandlingOnIdle && time > mainConfig.IdleTimeSeconds * 1000.0f)
				{
					m_bIsPaused = true;

					if (mainConfig.CameraProvider == CameraProvider_Augmented)
					{
						m_augmentedCameraManager->SetPaused(true);
					}
					m_cameraManager->SetPaused(true);

					if (mainConfig.CloseCameraStreamOnPause)
					{
						if (mainConfig.CameraProvider == CameraProvider_Augmented)
						{
							m_augmentedCameraManager->DeinitCamera();
						}
						m_cameraManager->DeinitCamera();

						m_bCamerasInitialized = false;
					}
				}
			}

			return result;
		}


		XrResult xrSetEnvironmentDepthEstimationVARJO(XrSession session, XrBool32 enabled)
		{
			if (!m_bVarjoDepthExtensionEnabled)
			{
				ErrorLog("xrSetEnvironmentDepthEstimationVARJO called without enabling extension!\n");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (m_bDepthSupportedByRenderer && m_configManager->GetConfig_Extensions().ExtVarjoDepthEstimation && enabled)
			{
				m_bVarjoDepthEnabled = true;
				return XR_SUCCESS;
			}
			else if ((!m_bDepthSupportedByRenderer || !m_configManager->GetConfig_Extensions().ExtVarjoDepthEstimation) && enabled)
			{
				if (!m_bDepthSupportedByRenderer)
				{
					ErrorLog("Varjo depth estimation unsupported by current renderer.\n");
				}
				return XR_ERROR_FEATURE_UNSUPPORTED;
			}
			else
			{
				m_bVarjoDepthEnabled = false;
				return XR_SUCCESS;
			}
		}


		private:

		bool isSystemHandled(XrSystemId systemId) const { return systemId == m_systemId; }
		bool isCurrentSession(XrSession session) const { return session == m_currentSession; }

		XrInstance m_currentInstance{XR_NULL_HANDLE};
		XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
		XrSession m_currentSession{XR_NULL_HANDLE};
		std::shared_ptr<IPassthroughRenderer> m_Renderer;
		std::shared_ptr<ConfigManager> m_configManager;
		std::shared_ptr<ICameraManager> m_cameraManager;
		std::shared_ptr<ICameraManager> m_augmentedCameraManager;
		std::unique_ptr<DashboardMenu> m_dashboardMenu;
		std::shared_ptr<OpenVRManager> m_openVRManager;
		std::shared_ptr<DepthReconstruction> m_depthReconstruction;
		std::shared_ptr<DepthReconstruction> m_augmentedDepthReconstruction;

		bool m_bSuccessfullyLoaded = false;
		bool m_bUsePassthrough = false;
		bool m_bInverseAlphaExtensionEnabled = false;
		bool m_bVarjoDepthExtensionEnabled = false;
		bool m_bVarjoDepthEnabled = false;
		bool m_bVarjoCompositionExtensionEnabled = false;
		bool m_bEnableVulkan2Extension = false;
		bool m_bDepthSupportedByRenderer = false;
		bool m_bBeginframeCalled = false;
		bool m_bIsInitialConfig = false;

		XrSwapchain m_swapChainLeft{XR_NULL_HANDLE};
		XrSwapchain m_swapChainRight{XR_NULL_HANDLE};

		XrSwapchain m_depthSwapChainLeft{ XR_NULL_HANDLE };
		XrSwapchain m_depthSwapChainRight{ XR_NULL_HANDLE };

		std::map<XrSpace, XrReferenceSpaceCreateInfo> m_refSpaces{};
		std::map<XrSwapchain, XrSwapchainCreateInfo> m_swapchainProperties{};
		std::map<XrSwapchain, std::queue<uint32_t>> m_acquiredSwapchains{};
		std::map<XrSwapchain, bool> m_waitedSwapchains{};
		std::map<XrSwapchain, std::queue<uint32_t>> m_heldSwapchains{};

		std::deque<float> m_frameToRenderTimes;
		std::deque<float> m_frameToPhotonTimes;
		std::deque<float> m_passthroughRenderTimes;

		LARGE_INTEGER m_lastRenderTime;
		bool m_bIsPaused = false;
		bool m_bCamerasInitialized = false;

		ERenderAPI m_renderAPI = DirectX11;
		ERenderAPI m_appRenderAPI = DirectX11;

    };

    std::unique_ptr<OpenXrLayer> g_instance = nullptr;
	

} // namespace

namespace steamvr_passthrough
{
    OpenXrApi* GetInstance()
    {
	if (!g_instance)
	{
	    g_instance = std::make_unique<OpenXrLayer>();
	}
	return g_instance.get();
    }

    void ResetInstance() { g_instance.reset(); }

} // namespace steamvr_passthrough

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		g_dllModule = hModule;
#if USE_TRACELOGGING
		TraceLoggingRegister(steamvr_passthrough::log::g_traceProvider);
#endif
	break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
	break;
    }
    return TRUE;
}
