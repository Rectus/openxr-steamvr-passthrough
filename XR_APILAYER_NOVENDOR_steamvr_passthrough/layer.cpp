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
#include "layer_structs.h"
#include "perfutil.h"
#include "pathutil.h"
#include "passthrough_renderer.h"
#include "camera_manager.h"
#include "config_manager.h"
#include "menu_handler.h"
#include "menu_ipc_client.h"
#include "openvr_manager.h"
#include "depth_reconstruction.h"
#include <util.h>
#include <map>
#include <queue>
#include "psapi.h"
#include "lodepng.h"
#include "resource.h"

HMODULE g_dllModule = NULL;

// Directory under AppData to write config.
#define CONFIG_FILE_DIR "\\OpenXR SteamVR Passthrough"
#define CONFIG_FILE_NAME "\\config.ini"
#define MENU_EXE_FILE_NAME L"\\passthrough-menu.exe"
#define MENU_EXE_ARGUMENTS L" --fromlayer"


namespace
{
    using namespace steamvr_passthrough;

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

			g_logger->flush();
			m_menuHandler.reset();
			m_menuIPCClient.reset();
			m_openVRManager.reset();
			g_logger->flush();
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
			bool bEnableAndroidCameraStateExtension = false;
			bool bEnableFBPassthroughExtension = false;
			bool bEnableVarjoDepthExtension = false;
			bool bEnableVarjoCompositionExtension = false;

			std::vector<std::string> extensions = GetRequestedExtensions();
			for (uint32_t i = 0; i < extensions.size(); i++)
			{
				if (extensions[i].compare(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
				{
					bEnableVulkan2Extension = true;
				}
				else if (extensions[i].compare(XR_EXT_COMPOSITION_LAYER_INVERTED_ALPHA_EXTENSION_NAME) == 0)
				{
					bInverseAlphaExtensionEnabled = true;
				}
				else if (extensions[i].compare(XR_ANDROID_PASSTHROUGH_CAMERA_STATE_EXTENSION_NAME) == 0)
				{
					bEnableAndroidCameraStateExtension = true;
				}
				else if (extensions[i].compare(XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
				{
					bEnableFBPassthroughExtension = true;
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
				g_logger->error("xrCreateInstance returned error {}", static_cast<int32_t>(result));
				return result;
			}

			g_logger->info("Application {} creating OpenXR instance...", GetApplicationName());

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
			g_logger->info("Application: {}", GetApplicationName().c_str());
			g_logger->info("Using OpenXR runtime: {}", runtimeName.c_str());
#endif

			std::wstring dllPath(MAX_PATH, L'\0');
			if (FAILED(GetModuleFileNameW(g_dllModule, (LPWSTR)dllPath.c_str(), (DWORD)dllPath.size())))
			{
				g_logger->error("Error retreiving DLL path!");
			}

#ifndef OPENVR_BUILD_STATIC
			// Try to load the OpenVR DLL from the same directory the current DLL is in.
			std::wstring openVRPath = dllPath.substr(0, dllPath.find_last_of(L"/\\")) + L"\\openvr_api.dll";

			// If loading fails without error, hopefully it means the library is already loaded.
			if (LoadLibraryExW((LPWSTR)openVRPath.c_str(), NULL, 0) == nullptr && GetLastError() != 0)
			{
				g_logger->error("Error loading OpenVR DLL: {}", GetLastError());
				return result;
			}
#endif
			{
				std::string filePath = GetRoamingAppData() + CONFIG_FILE_DIR + CONFIG_FILE_NAME;
				m_configManager = std::make_shared<ConfigManager>(filePath, false);
				m_bIsInitialConfig = m_configManager->ReadConfigFile();
			}

			// Check that the SteamVR OpenXR runtime is being used.
			if (m_configManager->GetConfig_Main().RequireSteamVRRuntime)
			{
				XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
				OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties);

				if (strncmp(instanceProperties.runtimeName, "SteamVR/OpenXR", 14))
				{
					g_logger->error("The active OpenXR runtime is {}, not SteamVR, passthrough layer not enabled!", instanceProperties.runtimeName);
					return result;
				}
			}

			if (m_configManager->GetConfig_Main().LaunchMenuOnStartup)
			{
				// Launch settings menu to the systray and dashboard.
				std::wstring menuEXEPath = dllPath.substr(0, dllPath.find_last_of(L"/\\")) + MENU_EXE_FILE_NAME;
				std::wstring menuEXECmdLine = menuEXEPath + MENU_EXE_ARGUMENTS;
				STARTUPINFOW startupInfo = { sizeof(STARTUPINFOW) };
				PROCESS_INFORMATION processInfo;

				if (CreateProcessW(menuEXEPath.data(), menuEXECmdLine.data(), NULL, NULL, false, 0, NULL, NULL, &startupInfo, &processInfo))
				{
					CloseHandle(processInfo.hProcess);
					CloseHandle(processInfo.hThread);
				}
				else
				{
					g_logger->error("Failed to launch settings menu process: {}", GetLastError());
				}
			}
			else
			{
				g_logger->info("Automatic settings menu launch disabled");
			}

			m_openVRManager = std::make_shared<OpenVRManager>();
			m_menuIPCClient = std::make_shared<MenuIPCClient>();
			m_menuHandler = std::make_unique<MenuHandler>(g_dllModule, m_configManager, m_menuIPCClient);
			m_menuIPCClient->RegisterReader(m_menuHandler);

			ClientData& data = m_menuHandler->GetClientData();

			if (bEnableVarjoDepthExtension && m_configManager->GetConfig_Extensions().ExtVarjoDepthEstimation)
			{
				m_bVarjoDepthExtensionEnabled = true;
				data.Values.bVarjoDepthEstimationExtensionActive = true;
				g_logger->info("Extension XR_VARJO_environment_depth_estimation enabled");
			}

			if (bEnableVarjoCompositionExtension && m_configManager->GetConfig_Extensions().ExtVarjoDepthComposition)
			{
				m_bVarjoCompositionExtensionEnabled = true;
				data.Values.bVarjoDepthCompositionExtensionActive = true;
				g_logger->info("Extension XR_VARJO_composition_layer_depth_test enabled");
			}

			if (bEnableVulkan2Extension)
			{
				m_bEnableVulkan2Extension = true;
				g_logger->info("Extension XR_KHR_vulkan_enable2 detected");
			}

			if (bInverseAlphaExtensionEnabled)
			{
				m_bInverseAlphaExtensionEnabled = true;
				data.Values.bExtInvertedAlphaActive = true;
				g_logger->info("Extension XR_EXT_composition_layer_inverted_alpha enabled");
			}

			if (bEnableAndroidCameraStateExtension)
			{
				m_bAndroidPassthroughStateExtensionEnabled = true;
				data.Values.bAndroidPassthroughStateActive = true;
				g_logger->info("Extension XR_ANDROID_passthrough_camera_state enabled");
			}

			if (bEnableFBPassthroughExtension && m_configManager->GetConfig_Extensions().ExtFBPassthrough)
			{
				m_bFBPassthroughExtensionEnabled = true;
				data.Values.bFBPassthroughExtensionActive = true;
				g_logger->info("Extension XR_FB_passthrough enabled");
			}
	
			data.ApplicationModuleName = GetProcessFileName();
			data.Values.ApplicationPID = GetCurrentProcessId();

			data.ApplicationName = std::string(createInfo->applicationInfo.applicationName);
			data.EngineName = std::string(createInfo->applicationInfo.engineName);
			data.Values.ApplicationVersion = createInfo->applicationInfo.applicationVersion;
			data.Values.EngineVersion = createInfo->applicationInfo.engineVersion;
			data.Values.XRVersion = createInfo->applicationInfo.apiVersion;

			m_menuHandler->DispatchApplicationModuleName();
			m_menuHandler->DispatchApplicationName();
			m_menuHandler->DispatchEngineName();
			m_menuHandler->DispatchClientDataValues();

			m_bSuccessfullyLoaded = true;
			g_logger->info("OpenXR instance successfully created");

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
						strncpy_s(&buffer[bufferCapacityInput - exts.size() - 2], bufferCapacityInput, exts.c_str(), exts.size());
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

				g_logger->info("Using OpenXR system: {}", systemProperties.systemName);
			}

			// Remember the XrSystemId to use.
			m_systemId = *systemId;
			}

#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg((int)*systemId, "SystemId"));
#endif

			return result;
		}


		XrResult xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties)
		{
			if (!m_bAndroidPassthroughStateExtensionEnabled && !m_bFBPassthroughExtensionEnabled)
			{
				return OpenXrApi::xrGetSystemProperties(instance, systemId, properties);
			}

			XrBaseOutStructure* prevProperty = reinterpret_cast<XrBaseOutStructure*>(properties);
			XrBaseOutStructure* property = reinterpret_cast<XrBaseOutStructure*>(properties->next);

			bool bFoundCamStateProperty = false;
			XrSystemPassthroughCameraStatePropertiesANDROID* camStateProperty = nullptr;
			XrBaseOutStructure* camStatePrevProperty = nullptr;

			bool bFoundFBPassthroughProperty = false;
			XrSystemPassthroughPropertiesFB* FBPassthroughProperty = nullptr;
			XrBaseOutStructure* FBPassthroughPrevProperty = nullptr;

			bool bFoundFBPassthrough2Property = false;
			XrSystemPassthroughProperties2FB* FBPassthrough2Property = nullptr;
			XrBaseOutStructure* FBPassthrough2PrevProperty = nullptr;

			while (property != nullptr)
			{
				if (property->type == XR_TYPE_SYSTEM_PASSTHROUGH_CAMERA_STATE_PROPERTIES_ANDROID)
				{
					bFoundCamStateProperty = true;
					camStatePrevProperty = prevProperty;
					prevProperty->next = property->next; // Temporarily remove property struct from chain.

					camStateProperty = reinterpret_cast<XrSystemPassthroughCameraStatePropertiesANDROID*>(property);
					camStateProperty->supportsPassthroughCameraState = XR_TRUE;
				}
				else if (property->type == XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB)
				{
					bFoundCamStateProperty = true;
					FBPassthroughPrevProperty = prevProperty;
					prevProperty->next = property->next;


					FBPassthroughProperty = reinterpret_cast<XrSystemPassthroughPropertiesFB*>(property);
					FBPassthroughProperty->supportsPassthrough = XR_TRUE;
				}
				else if (property->type == XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB)
				{
					bFoundCamStateProperty = true;
					FBPassthrough2PrevProperty = prevProperty;
					prevProperty->next = property->next;


					FBPassthrough2Property = reinterpret_cast<XrSystemPassthroughProperties2FB*>(property);
					FBPassthrough2Property->capabilities = XR_PASSTHROUGH_CAPABILITY_BIT_FB | XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB;
					if (m_configManager->GetConfig_Extensions().ExtFBPassthroughAllowDepth)
					{
						FBPassthrough2Property->capabilities |= XR_PASSTHROUGH_CAPABILITY_LAYER_DEPTH_BIT_FB;
					}
				}

				prevProperty = property;
				property = property->next;
			}

			XrResult result = OpenXrApi::xrGetSystemProperties(instance, systemId, properties);


			// Restore property structs to chain in same order to maintain consecutive removed structs.

			property = reinterpret_cast<XrBaseOutStructure*>(properties->next);

			while (property != nullptr)
			{

				if (bFoundCamStateProperty && property == camStatePrevProperty)
				{
					property->next = reinterpret_cast<XrBaseOutStructure*>(camStateProperty);
				}
				else if (bFoundFBPassthroughProperty && property == FBPassthroughPrevProperty)
				{
					property->next = reinterpret_cast<XrBaseOutStructure*>(FBPassthroughProperty);
				}
				else if (bFoundFBPassthrough2Property && property == FBPassthrough2PrevProperty)
				{
					property->next = reinterpret_cast<XrBaseOutStructure*>(FBPassthrough2Property);
				}

				property = property->next;
			}

			return result;
		}


		bool SetupRenderer(const XrInstance instance, const XrSessionCreateInfo* createInfo, const XrSession* session)
		{
			ClientData& clientData = m_menuHandler->GetClientData();

			const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);

			while (entry != nullptr)
			{
				switch (entry->type)
				{
				case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
				{
					g_logger->info("Initializing rendering for Direct3D 11...");

					const XrGraphicsBindingD3D11KHR* bindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
					m_Renderer = std::make_shared<PassthroughRendererDX11>(bindings->device, g_dllModule, m_configManager);
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

					ERenderAPI usedAPI = RenderAPI_Direct3D11;

					if (m_configManager->GetConfig_Main().UseLegacyD3D12Renderer)
					{
						m_Renderer = std::make_unique<PassthroughRendererDX12>(bindings->device, bindings->queue, g_dllModule, m_configManager);
						usedAPI = RenderAPI_Direct3D12;
						g_logger->info("Using legacy Direct3D 12 renderer");
					}
					else
					{
						m_Renderer = std::make_unique<PassthroughRendererDX11Interop>(bindings->device, bindings->queue, g_dllModule, m_configManager);
					}

					m_renderAPI = usedAPI;
					m_appRenderAPI = RenderAPI_Direct3D12;

					if (!SetupProcessingPipeline())
					{
						return false;
					}

					clientData.Values.bSessionActive = true;
					clientData.Values.RenderAPI = usedAPI;
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
					if (m_configManager->GetConfig_Main().UseLegacyVulkanRenderer)
					{

						m_Renderer = std::make_unique<PassthroughRendererVulkan>(*bindings, g_dllModule, m_configManager);
						g_logger->info("Using legacy Vulkan renderer");
					}
					else
					{
						if (!m_bEnableVulkan2Extension)
						{
							g_logger->error("The XR_KHR_vulkan_enable extension is only supported with the legacy renderer, passthrough rendering not enabled");
							return false;
						}

						usedAPI = RenderAPI_Direct3D11;
						m_Renderer = std::make_unique<PassthroughRendererDX11Interop>(*bindings, g_dllModule, m_configManager);
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

					m_appRenderAPI = RenderAPI_OpenGL;
					m_renderAPI = RenderAPI_Direct3D11;

					const XrGraphicsBindingOpenGLWin32KHR* bindings = reinterpret_cast<const XrGraphicsBindingOpenGLWin32KHR*>(entry);
					
					m_Renderer = std::make_unique<PassthroughRendererDX11Interop>(*bindings, g_dllModule, m_configManager);

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
				g_logger->error("Trying to initialize processing pipeline without renderer!");
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
						m_configManager->GetConfig_Main().ProjectionMode = Projection_StereoReconstruction;
					}*/
				}

				ClientData& data = m_menuHandler->GetClientData();
				m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
				m_menuHandler->DispatchClientDataValues();

				m_cameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_cameraManager->GetUndistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}
			else if (mainConfig.CameraProvider == CameraProvider_Augmented)
			{
				m_cameraManager = std::make_shared<CameraManagerOpenVR>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager);
				m_augmentedCameraManager = std::make_shared<CameraManagerOpenCV>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager, true);

				if (!m_cameraManager->InitCamera() || !m_augmentedCameraManager->InitCamera())
				{
					g_logger->error("Failed to initialize camera!");
					return false;
				}
				ClientData& data = m_menuHandler->GetClientData();
				m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
				m_menuHandler->DispatchClientDataValues();

				m_augmentedCameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_augmentedCameraManager->GetDistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}
			else
			{
				m_cameraManager = std::make_shared<CameraManagerOpenCV>(m_Renderer, m_renderAPI, m_appRenderAPI, m_configManager, m_openVRManager);

				if (!m_cameraManager->InitCamera())
				{
					g_logger->error("Failed to initialize camera!");
					return false;
				}
				ClientData& data = m_menuHandler->GetClientData();
				m_cameraManager->GetCameraDisplayStats(data.Values.CameraFrameWidth, data.Values.CameraFrameHeight, data.Values.CameraFrameRate, data.Values.CameraProvider, data.Values.bCameraActive);
				m_menuHandler->DispatchClientDataValues();

				m_cameraManager->GetDistortedTextureSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
				m_cameraManager->GetDistortedTextureSize(cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			}

			m_bCamerasInitialized = true;

			m_Renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize, cameraUndistortedTextureWidth, cameraUndistortedTextureHeight, cameraUndistortedFrameBufferSize);
			if (!m_Renderer->InitRenderer())
			{
				g_logger->error("Failed to initialize renderer!");
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
					g_logger->error("Loading failed, not attaching to session!");
				}
				else if (isSystemHandled(createInfo->systemId) && !isCurrentSession(*session))
				{

					m_currentInstance = instance;
					m_currentSession = *session;
					if (SetupRenderer(instance, createInfo, session))
					{
						g_logger->info("Passthrough API layer enabled for session");
						m_bUsePassthrough = m_configManager->GetConfig_Main().EnablePassthrough;
					}
					else
					{
						m_bUsePassthrough = false;
						m_currentSession = XR_NULL_HANDLE;
						g_logger->error("Failed to initialize rendering system!");
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
				g_logger->info("Passthrough session ending...");
				
				m_depthReconstruction.reset();
				m_augmentedDepthReconstruction.reset();
				m_cameraManager.reset();
				m_augmentedCameraManager.reset();

				m_Renderer.reset();

				m_currentSession = XR_NULL_HANDLE;
				m_currentInstance = XR_NULL_HANDLE;
			}
			g_logger->flush();

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
			
			if (m_appRenderAPI == RenderAPI_Vulkan && m_renderAPI == RenderAPI_Direct3D11)
			{
				newCreateInfo.usageFlags |= (XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT);
			}

			XrResult result = OpenXrApi::xrCreateSwapchain(session, &newCreateInfo, swapchain);
			if (XR_SUCCEEDED(result))
			{
				m_swapchainProperties[*swapchain] = *createInfo;

				g_logger->info("App created new {} x {} swapchain {}, createFlags 0x{:x}, usageFlags 0x{:X}, format {}, sampleCount {}, faceCount {}, arraySize {}, mipCount {}", createInfo->width, createInfo->height, reinterpret_cast<uint64_t>(*swapchain), createInfo->createFlags, createInfo->usageFlags, createInfo->format, createInfo->sampleCount, createInfo->faceCount, createInfo->arraySize, createInfo->mipCount);
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
				g_logger->error("Error in xrAcquireSwapchainImage: {}", static_cast<int32_t>(result));
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
					g_logger->error("App waiting on a second swapchain per-frame!");
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
				g_logger->info("Swapchain not acquired: {}", reinterpret_cast<uint64_t>(swapchain));
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
			XrSwapchain* storedSwapchain = (eye == RenderEye_Left) ? &m_swapChainLeft : &m_swapChainRight;
			XrSwapchain* storedDepthSwapchain = (eye == RenderEye_Left) ? &m_depthSwapChainLeft : &m_depthSwapChainRight;
			int viewIndex = (eye == RenderEye_Left) ? 0 : 1;

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
			
			if (eye == RenderEye_Left)
			{
				m_menuHandler->GetClientData().Values.FrameBufferFormat = props->second.format;
				renderParams.LeftFrameIndex = imageIndex;
			}
			else
			{
				renderParams.RightFrameIndex = imageIndex;
			}

			if (newSwapchain != *storedSwapchain)
			{
				g_logger->info("Updating swapchain {} to {} with eye {}, index {}, arraySize {}", reinterpret_cast<uint64_t>(*storedSwapchain), reinterpret_cast<uint64_t>(newSwapchain), static_cast<uint32_t>(eye), imageIndex, props->second.arraySize);
			
				XrStructureType type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;

				if (m_appRenderAPI == RenderAPI_Direct3D12)
				{
					type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
				}
				else if (m_appRenderAPI == RenderAPI_Vulkan)
				{
					type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
				}
				if (m_appRenderAPI == RenderAPI_OpenGL)
				{
					type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
				}

				// Assuming the structs are all the same size
				XrSwapchainImageD3D12KHR swapchainImages[3] = { };
				swapchainImages[0].type = type;
				swapchainImages[1].type = type;
				swapchainImages[2].type = type;

				uint32_t numImages = 0;

				XrResult result = OpenXrApi::xrEnumerateSwapchainImages(newSwapchain, 3, &numImages, (XrSwapchainImageBaseHeader*)swapchainImages);
				if (XR_SUCCEEDED(result))
				{
					for (uint32_t i = 0; i < numImages; i++)
					{
						m_Renderer->InitRenderTarget(eye, swapchainImages[i].texture, i, props->second, newSwapchain);
					}
					*storedSwapchain = newSwapchain;
				}
				else
				{
					g_logger->error("Error in xrEnumerateSwapchainImages: {}", static_cast<int32_t>(result));

					if (eye == RenderEye_Left)
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
					if (eye == RenderEye_Left)
					{
						ClientData& data = m_menuHandler->GetClientData();
						data.Values.DepthBufferFormat = depthProps->second.format;
						data.Values.NearZ = depthInfo->nearZ;
						data.Values.FarZ = depthInfo->farZ;
					}

					g_logger->info("Found depth swapchain {} for color swapchain {}, arraySize {}, depth range [{}:{}], Z-range[{:g}:{:g}]", reinterpret_cast<uint64_t>(depthInfo->subImage.swapchain), reinterpret_cast<uint64_t>(newSwapchain), depthProps->second.arraySize, depthInfo->minDepth, depthInfo->maxDepth, depthInfo->nearZ, depthInfo->farZ);

					const XrRect2Di& depthRect = depthInfo->subImage.imageRect;
					const XrRect2Di& colorRect = layer->views[viewIndex].subImage.imageRect;

					if (depthRect.offset.x != colorRect.offset.x || 
						depthRect.offset.y != colorRect.offset.y ||
						depthRect.extent.width != colorRect.extent.width || 
						depthRect.extent.height != depthRect.extent.height)
					{
						g_logger->error("The color and depth textures have mismatched imageRects, this is not supported by the layer: {}, {}, {}, {} : {}, {}, {}, {}", colorRect.offset.x, colorRect.offset.y, colorRect.extent.width, colorRect.extent.height, depthRect.offset.x, depthRect.offset.y, depthRect.extent.width, depthRect.extent.height);
					}

					XrStructureType type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;

					if (m_appRenderAPI == RenderAPI_Direct3D12)
					{
						type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
					}
					else if (m_appRenderAPI == RenderAPI_Vulkan)
					{
						type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
					}
					if (m_appRenderAPI == RenderAPI_OpenGL)
					{
						type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
					}

					XrSwapchainImageD3D12KHR depthImages[3] = { };
					depthImages[0].type = type;
					depthImages[1].type = type;
					depthImages[2].type = type;
					uint32_t numImages = 0;

					XrResult result = OpenXrApi::xrEnumerateSwapchainImages(depthInfo->subImage.swapchain, 3, &numImages, (XrSwapchainImageBaseHeader*)depthImages);
					if (XR_SUCCEEDED(result))
					{
						for (uint32_t i = 0; i < numImages; i++)
						{
							m_Renderer->InitDepthBuffer(eye, depthImages[i].texture, i, depthProps->second, depthInfo->subImage.swapchain);
						}
						*storedDepthSwapchain = depthInfo->subImage.swapchain;
					}
					else
					{
						g_logger->error("Error in xrEnumerateSwapchainImages when enumerating depthbuffers: {}", static_cast<int32_t>(result));
					}
				}
			}

			if (depthInfo != nullptr)
			{
				auto depth = m_heldSwapchains.find(*storedDepthSwapchain);
				if (depth != m_heldSwapchains.end() && !depth->second.empty())
				{
					if (eye == RenderEye_Left)
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
					g_logger->error("Error: No valid depth swapchain found!");
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
				g_logger->error("Error finding test pattern resource!");
				return;
			}
			HGLOBAL memory = LoadResource(g_dllModule, resInfo);
			if (memory == nullptr)
			{
				g_logger->error("Error loading test pattern resource!");
				return;
			}
			size_t data_size = SizeofResource(g_dllModule, resInfo);
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

			texture.Height = height;
			texture.Width = width;
			texture.PixelSize = 4;
			texture.Format = DebugTextureFormat_RGBA8;
			texture.CurrentTexture = DebugTexture_TestImage;
			texture.bDimensionsUpdated = true;

			return;
		}

		void RenderPassthroughOnAppLayer(const XrFrameEndInfo* frameEndInfo, uint32_t layerNum, bool bUseFBPassthrough, FBPassthroughLayerInstance* fbLayer)
		{
			XrCompositionLayerProjection* layer = (XrCompositionLayerProjection*)frameEndInfo->layers[layerNum];
			std::shared_ptr<CameraFrame> frame;

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

			ClientData& clientData = m_menuHandler->GetClientData();

			std::shared_lock readLock(frame->readWriteMutex);


			LARGE_INTEGER preRenderTime = StartPerfTimer();

			float frameToRenderTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, preRenderTime.QuadPart);
			clientData.Values.FrameToRenderLatencyMS = UpdateAveragePerfTime(m_frameToRenderTimes, frameToRenderTime, 20);

			LARGE_INTEGER displayTime;

			OpenXrApi::xrConvertTimeToWin32PerformanceCounterKHR(m_currentInstance, frameEndInfo->displayTime, &displayTime);

			float frameToPhotonsTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, displayTime.QuadPart);
			clientData.Values.FrameToPhotonsLatencyMS = UpdateAveragePerfTime(m_frameToPhotonTimes, frameToPhotonsTime, 20);


			float timeToPhotons = GetPerfTimerDiff(preRenderTime.QuadPart, displayTime.QuadPart);
			
			Config_Core& coreConf = m_configManager->GetConfig_Core();
			Config_Extensions& extConf = m_configManager->GetConfig_Extensions();
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

			if (bUseFBPassthrough)
			{
				if (extConf.ExtFBPassthroughAllowColorSettings && fbLayer->ColorAdjustmentEnabled)
				{
					renderParams.bForceColorSettings = true;
					renderParams.ForcedBrightness = fbLayer->Brightness;
					renderParams.ForcedContrast = fbLayer->Contrast;
					renderParams.ForcedSaturation = fbLayer->Saturation;
				}

				renderParams.RenderOpacity = fbLayer->Opacity;
			}

			UpdateSwapchains(RenderEye_Left, layer, renderParams);
			UpdateSwapchains(RenderEye_Right, layer, renderParams);

			if (renderParams.LeftFrameIndex < 0 || renderParams.RightFrameIndex < 0)
			{
				g_logger->error("Error: No swapchains found!");
				return;
			}			

			if (coreConf.CoreForcePassthrough && coreConf.CoreForceMode >= 0)
			{
				renderParams.BlendMode = (EPassthroughBlendMode)coreConf.CoreForceMode;
				clientData.Values.bCorePassthroughActive = true;
				clientData.Values.bFBPassthroughActive = false;
				clientData.Values.bFBPassthroughDepthActive = false;
			}
			else if (bUseFBPassthrough)
			{
				renderParams.BlendMode = AlphaBlendPremultiplied;
				clientData.Values.bCorePassthroughActive = false;
				clientData.Values.bFBPassthroughActive = true;
				clientData.Values.bFBPassthroughDepthActive = fbLayer->DepthEnabled;
			}
			else
			{
				renderParams.BlendMode = (EPassthroughBlendMode)frameEndInfo->environmentBlendMode;
				clientData.Values.bCorePassthroughActive = true;
				clientData.Values.bFBPassthroughActive = false;
				clientData.Values.bFBPassthroughDepthActive = false;
			}

			if (renderParams.BlendMode == AlphaBlendPremultiplied)
			{
				if (coreConf.CoreForcePremultipliedAlpha == 0 || 
					(coreConf.CoreForcePremultipliedAlpha == -1 && 
						(layer->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) != 0))
				{
					renderParams.BlendMode = AlphaBlendUnpremultiplied;
				}
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
				(bUseFBPassthrough && fbLayer->DepthEnabled) ||
				depthConf.DepthForceComposition);

			clientData.Values.bDepthBlendingActive = renderParams.bEnableDepthBlending;

			
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


			m_Renderer->RenderPassthroughFrame(layer, frame, renderParams, depthFrame, distParams);

			depthFrame->bIsFirstRender = false;


			float renderTime = EndPerfTimer(preRenderTime.QuadPart);
			clientData.Values.RenderTimeMS = UpdateAveragePerfTime(m_passthroughRenderTimes, renderTime, 20);

			clientData.Values.StereoReconstructionTimeMS = m_depthReconstruction->GetReconstructionPerfTime();
			clientData.Values.FrameRetrievalTimeMS = m_cameraManager->GetFrameRetrievalPerfTime();
		}


		XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) 
		{
			if (!isCurrentSession(session))
			{
				// Strip out blend mode even when we aren't handling the session.
				XrFrameEndInfo modifiedFrameEndInfo = *frameEndInfo;
				modifiedFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

				std::vector<const XrCompositionLayerBaseHeader*>newLayers;

				if (m_bFBPassthroughExtensionEnabled)
				{
					for (uint32_t i = 0; i < modifiedFrameEndInfo.layerCount; i++)
					{
						auto layer = modifiedFrameEndInfo.layers[i];

						if (layer->type == XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB)
						{
							continue;
						}

						newLayers.push_back(layer);
					}

					modifiedFrameEndInfo.layers = newLayers.data();
					modifiedFrameEndInfo.layerCount = static_cast<uint32_t>(newLayers.size());
				}
				return OpenXrApi::xrEndFrame(session, &modifiedFrameEndInfo);
			}

			Config_Main& mainConfig = m_configManager->GetConfig_Main();

			bool bResetPending = false;

			if (m_Renderer.get() &&m_configManager->CheckResetRendererResetPending())
			{
				bResetPending = true;
			}


			XrResult result;

			if (m_menuHandler.get())
			{
				ClientData& data = m_menuHandler->GetClientData();

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

			bool bInvalidEndFrame = false;

			if (frameEndInfo->displayTime == 0 || !m_bBeginframeCalled)
			{
				bInvalidEndFrame = true;
			}

			m_bBeginframeCalled = false;

			bool bDidRender = false;
			if (m_bUsePassthrough && !bResetPending && !bInvalidEndFrame)
			{
				bool bHasUnderlayLayer = false;
				FBPassthroughLayerInstance* fbLayer = nullptr;

				for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
				{
					if (m_bFBPassthroughExtensionEnabled && m_fbPassthough.PassthroughStarted && frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB)
					{
						auto layer = reinterpret_cast<const XrCompositionLayerPassthroughFB*>(frameEndInfo->layers[i]);

						for (auto& instance : m_fbPassthough.Layers)
						{
							if (instance.Handle != layer->layerHandle)
							{
								continue;
							}

							if (instance.LayerStarted)
							{
								bHasUnderlayLayer = true;
								fbLayer = &instance;
							}

							break;
						}
					}
					else if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
					{
						bool bCanRenderDirect = IsBlendModeEnabled(frameEndInfo->environmentBlendMode, reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]));
						bool bCanRenderFBPassthrough = bHasUnderlayLayer && frameEndInfo->layers[i]->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

						if (bCanRenderDirect || bCanRenderFBPassthrough)
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
											g_logger->error("Failed to reinitialize camera!");
											break;
										}
									}
									else if (!m_cameraManager->InitCamera())
									{
										g_logger->error("Failed to reinitialize camera!");
										break;
									}
									m_bCamerasInitialized = true;
								}
								m_bIsPaused = false;
							}

							RenderPassthroughOnAppLayer(frameEndInfo, i, bCanRenderFBPassthrough, fbLayer);
							bDidRender = true;

							break;
						}
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
						g_logger->error("Error in xrReleaseSwapchainImage: {}", static_cast<int32_t>(result));
					}
				}
			}

			m_heldSwapchains.clear();


			XrFrameEndInfo modifiedFrameEndInfo = *frameEndInfo;
			modifiedFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			std::vector<const XrCompositionLayerBaseHeader*>newLayers;

			if (m_bFBPassthroughExtensionEnabled)
			{
				for (uint32_t i = 0; i < modifiedFrameEndInfo.layerCount; i++)
				{
					auto layer = modifiedFrameEndInfo.layers[i];

					if (layer->type == XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB)
					{
						static bool bErrorShown = false;
						if (i > 0 && !bErrorShown)
						{
							bErrorShown = true;
							g_logger->error("Non-underlay XrCompositionLayerPassthroughFB detected (layer #{}). The API layer currently only supports underlay passthrough.", i);
						}

						continue;
					}

					newLayers.push_back(layer);
				}

				modifiedFrameEndInfo.layers = newLayers.data();
				modifiedFrameEndInfo.layerCount = static_cast<uint32_t>(newLayers.size());
			}

			result = OpenXrApi::xrEndFrame(session, &modifiedFrameEndInfo);

			if (bResetPending)
			{
				ResetRenderer();
			}
			else if (bDidRender)
			{
				m_lastRenderTime = StartPerfTimer();
				m_menuHandler->GetClientData().Values.LastFrameTimestamp = m_lastRenderTime.QuadPart;
				m_menuHandler->DispatchClientDataValues();
			}
			else
			{
				if (m_menuHandler.get())
				{
					ClientData& data = m_menuHandler->GetClientData();
					data.Values.bCorePassthroughActive = false;
					data.Values.bFBPassthroughActive = false;
					uint64_t frameTime = StartPerfTimer().QuadPart;
					data.Values.LastFrameTimestamp = frameTime;

					m_menuHandler->DispatchClientDataValues();
				}

				float time = EndPerfTimer(m_lastRenderTime);

				// Never consider idle as long as FB passthrough is unpaused.
				if (!m_bIsPaused && !m_fbPassthough.PassthroughStarted && mainConfig.PauseImageHandlingOnIdle && time > mainConfig.IdleTimeSeconds * 1000.0f)
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
				g_logger->error("xrSetEnvironmentDepthEstimationVARJO called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!isCurrentSession(session))
			{
				g_logger->error("xrSetEnvironmentDepthEstimationVARJO called on untracked session!");
				return XR_ERROR_HANDLE_INVALID;
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
					g_logger->error("Varjo depth estimation unsupported by current renderer.");
				}
				return XR_ERROR_FEATURE_UNSUPPORTED;
			}
			else
			{
				m_bVarjoDepthEnabled = false;
				return XR_SUCCESS;
			}
		}


		XrResult xrGetPassthroughCameraStateANDROID(XrSession session, const XrPassthroughCameraStateGetInfoANDROID* getInfo, XrPassthroughCameraStateANDROID* cameraStateOutput)
		{
			if (!m_bAndroidPassthroughStateExtensionEnabled)
			{
				g_logger->error("xrGetPassthroughCameraStateANDROID called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!isCurrentSession(session))
			{
				g_logger->error("xrGetPassthroughCameraStateANDROID called on untracked session!");
				return XR_ERROR_HANDLE_INVALID;
			}

			if (!m_cameraManager.get())
			{
				return XR_ERROR_RUNTIME_FAILURE;
			}

			switch (m_cameraManager->GetCameraState())
			{
			case CameraState_Uninitialized:
			case CameraState_Idle:
				*cameraStateOutput = XR_PASSTHROUGH_CAMERA_STATE_DISABLED_ANDROID;
				break;

			case CameraState_Waiting:
				*cameraStateOutput = XR_PASSTHROUGH_CAMERA_STATE_INITIALIZING_ANDROID;
				break;

			case CameraState_Active:
				*cameraStateOutput = XR_PASSTHROUGH_CAMERA_STATE_READY_ANDROID;
				break;

			case CameraState_Error:
			default:
				*cameraStateOutput = XR_PASSTHROUGH_CAMERA_STATE_ERROR_ANDROID;
			}

			return XR_SUCCESS;
		}



		XrResult xrCreatePassthroughFB(XrSession session, const XrPassthroughCreateInfoFB* createInfo, XrPassthroughFB* outPassthrough)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrCreatePassthroughFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!isCurrentSession(session))
			{
				g_logger->error("xrCreatePassthroughFB called on untracked session!");
				return XR_ERROR_HANDLE_INVALID;
			}

			if (m_fbPassthough.InstanceCreated)
			{
				g_logger->error("Multiple calls to xrCreatePassthroughFB!");
				return XR_ERROR_FEATURE_ALREADY_CREATED_PASSTHROUGH_FB;
			}

			m_fbPassthough.InstanceCreated = true;
			m_fbPassthough.Layers.clear();
			m_fbPassthough.LastLayerHandle = XR_NULL_HANDLE;
			m_fbPassthough.InstanceHandle = reinterpret_cast<XrPassthroughFB>(1);
			*outPassthrough = m_fbPassthough.InstanceHandle;

			m_fbPassthough.PassthroughStarted = (createInfo->flags & XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB);

			return XR_SUCCESS;
		}

		XrResult xrDestroyPassthroughFB(XrPassthroughFB passthrough)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrDestroyPassthroughFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!m_fbPassthough.InstanceCreated 
				|| passthrough == XR_NULL_HANDLE 
				|| passthrough != m_fbPassthough.InstanceHandle)
			{
				return XR_ERROR_HANDLE_INVALID;
			}

			m_fbPassthough.InstanceCreated = false;
			m_fbPassthough.Layers.clear();
			m_fbPassthough.LastLayerHandle = XR_NULL_HANDLE;
			m_fbPassthough.InstanceHandle = XR_NULL_HANDLE;

			return XR_SUCCESS;
		}

		XrResult xrPassthroughStartFB(XrPassthroughFB passthrough)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrPassthroughStartFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!m_fbPassthough.InstanceCreated
				|| passthrough == XR_NULL_HANDLE
				|| passthrough != m_fbPassthough.InstanceHandle)
			{
				return XR_ERROR_HANDLE_INVALID;
			}

			m_fbPassthough.PassthroughStarted = true;

			return XR_SUCCESS;
		}

		XrResult xrPassthroughPauseFB(XrPassthroughFB passthrough)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrPassthroughPauseFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!m_fbPassthough.InstanceCreated
				|| passthrough == XR_NULL_HANDLE
				|| passthrough != m_fbPassthough.InstanceHandle)
			{
				return XR_ERROR_HANDLE_INVALID;
			}

			m_fbPassthough.PassthroughStarted = false;

			return XR_SUCCESS;
		}

		XrResult xrCreatePassthroughLayerFB(XrSession session, const XrPassthroughLayerCreateInfoFB* createInfo, XrPassthroughLayerFB* outLayer)
		{

			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrCreatePassthroughLayerFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			if (!isCurrentSession(session))
			{
				g_logger->error("xrCreatePassthroughLayerFB called on untracked session!");
				return XR_ERROR_HANDLE_INVALID;
			}

			if (!m_fbPassthough.InstanceCreated
				|| createInfo->passthrough == XR_NULL_HANDLE
				|| createInfo->passthrough != m_fbPassthough.InstanceHandle)
			{
				return XR_ERROR_HANDLE_INVALID;
			}

			if (createInfo->purpose != XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB)
			{
				g_logger->error("xrCreatePassthroughLayerFB: unsupported purpose requested: {}", static_cast<int32_t>(createInfo->purpose));

				if (!m_configManager->GetConfig_Extensions().ExtFBPassthroughFakeUnsupportedFeatures)
				{
					return XR_ERROR_FEATURE_UNSUPPORTED;
				}
			}

			FBPassthroughLayerInstance& layer = m_fbPassthough.Layers.emplace_back();

			m_fbPassthough.LastLayerHandle = reinterpret_cast<XrPassthroughLayerFB>(reinterpret_cast<size_t>(m_fbPassthough.LastLayerHandle) + 1);
			layer.Handle = m_fbPassthough.LastLayerHandle;
			layer.LayerStarted = (createInfo->flags & XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB);
			layer.DepthEnabled = (createInfo->flags & XR_PASSTHROUGH_LAYER_DEPTH_BIT_FB) && m_configManager->GetConfig_Extensions().ExtFBPassthroughAllowDepth;
			layer.ColorAdjustmentEnabled = false;
			layer.Opacity = 1.0f;

			*outLayer = layer.Handle;

			return XR_SUCCESS;
		}

		XrResult xrDestroyPassthroughLayerFB(XrPassthroughLayerFB layer)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrDestroyPassthroughLayerFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			for (auto iterator = m_fbPassthough.Layers.begin(); iterator != m_fbPassthough.Layers.end();)
			{
				if ((*iterator).Handle == layer)
				{
					m_fbPassthough.Layers.erase(iterator);
					return XR_SUCCESS;
				}
			}

			return XR_ERROR_HANDLE_INVALID;
		}

		XrResult xrPassthroughLayerPauseFB(XrPassthroughLayerFB layer)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrPassthroughLayerPauseFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			for (auto& instance : m_fbPassthough.Layers)
			{
				if (instance.Handle == layer)
				{
					instance.LayerStarted = false;

					return XR_SUCCESS;
				}
			}

			return XR_ERROR_HANDLE_INVALID;
		}

		XrResult xrPassthroughLayerResumeFB(XrPassthroughLayerFB layer)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrPassthroughLayerResumeFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			for (auto& instance : m_fbPassthough.Layers)
			{
				if (instance.Handle == layer)
				{
					instance.LayerStarted = true;

					return XR_SUCCESS;
				}
			}

			return XR_ERROR_HANDLE_INVALID;
		}

		XrResult xrPassthroughLayerSetStyleFB(XrPassthroughLayerFB layer, const XrPassthroughStyleFB* style)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrPassthroughLayerSetStyleFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			for (auto& instance : m_fbPassthough.Layers)
			{
				if (instance.Handle == layer)
				{
					bool bFoundStruct = false;
					const XrPassthroughBrightnessContrastSaturationFB* colorStruct = nullptr;
					auto chained = reinterpret_cast<const XrBaseInStructure*>(style->next);
					while (chained != nullptr)
					{
						if (bFoundStruct)
						{
							g_logger->error("Multiple chained structs passed to xrPassthroughLayerSetStyleFB!");
							return XR_ERROR_VALIDATION_FAILURE;
						}

						if (chained->type != XR_TYPE_PASSTHROUGH_BRIGHTNESS_CONTRAST_SATURATION_FB)
						{
							g_logger->error("Currently unsupported chained struct %u passed to xrPassthroughLayerSetStyleFB!", static_cast<int32_t>(chained->type));
							if (!m_configManager->GetConfig_Extensions().ExtFBPassthroughFakeUnsupportedFeatures)
							{
								return XR_ERROR_FEATURE_UNSUPPORTED;
							}
						}

						bFoundStruct = true;
						colorStruct = reinterpret_cast<const XrPassthroughBrightnessContrastSaturationFB*>(chained);

						chained = chained->next;
					}

					if (bFoundStruct && m_configManager->GetConfig_Extensions().ExtFBPassthroughAllowColorSettings)
					{
						instance.ColorAdjustmentEnabled = true;
						instance.Brightness = colorStruct->brightness;
						instance.Contrast = colorStruct->contrast;
						instance.Saturation = colorStruct->saturation;
					}
					else
					{
						instance.ColorAdjustmentEnabled = false;
					}

					instance.Opacity = style->textureOpacityFactor;

					return XR_SUCCESS;
				}
			}

			return XR_ERROR_HANDLE_INVALID;
		}

		XrResult xrCreateGeometryInstanceFB(XrSession session, const XrGeometryInstanceCreateInfoFB* createInfo, XrGeometryInstanceFB* outGeometryInstance)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrCreateGeometryInstanceFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			g_logger->error("xrCreateGeometryInstanceFB is not currently supported!");

			if (!m_configManager->GetConfig_Extensions().ExtFBPassthroughFakeUnsupportedFeatures)
			{
				return XR_ERROR_FEATURE_UNSUPPORTED;
			}
			return XR_SUCCESS;
		}

		XrResult xrDestroyGeometryInstanceFB(XrGeometryInstanceFB  instance)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrDestroyGeometryInstanceFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			g_logger->error("xrDestroyGeometryInstanceFB is not currently supported!");

			if (!m_configManager->GetConfig_Extensions().ExtFBPassthroughFakeUnsupportedFeatures)
			{
				return XR_ERROR_FEATURE_UNSUPPORTED;
			}
			return XR_SUCCESS;
		}

		XrResult xrGeometryInstanceSetTransformFB(XrGeometryInstanceFB instance, const XrGeometryInstanceTransformFB* transformation)
		{
			if (!m_bFBPassthroughExtensionEnabled)
			{
				g_logger->error("xrGeometryInstanceSetTransformFB called without enabling extension!");
				return XR_ERROR_RUNTIME_FAILURE;
			}

			g_logger->error("xrGeometryInstanceSetTransformFB is not currently supported!");

			if (!m_configManager->GetConfig_Extensions().ExtFBPassthroughFakeUnsupportedFeatures)
			{
				return XR_ERROR_FEATURE_UNSUPPORTED;
			}
			return XR_SUCCESS;
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
		std::shared_ptr<MenuHandler> m_menuHandler;
		std::shared_ptr<MenuIPCClient> m_menuIPCClient;
		std::shared_ptr<OpenVRManager> m_openVRManager;
		std::shared_ptr<DepthReconstruction> m_depthReconstruction;
		std::shared_ptr<DepthReconstruction> m_augmentedDepthReconstruction;

		bool m_bSuccessfullyLoaded = false;
		bool m_bUsePassthrough = false;
		bool m_bInverseAlphaExtensionEnabled = false;
		bool m_bFBPassthroughExtensionEnabled = false;
		bool m_bVarjoDepthExtensionEnabled = false;
		bool m_bVarjoDepthEnabled = false;
		bool m_bVarjoCompositionExtensionEnabled = false;
		bool m_bAndroidPassthroughStateExtensionEnabled = false;
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

		LARGE_INTEGER m_lastRenderTime = {};
		bool m_bIsPaused = false;
		bool m_bCamerasInitialized = false;

		FBPassthroughInstance m_fbPassthough;

		ERenderAPI m_renderAPI = RenderAPI_Direct3D11;
		ERenderAPI m_appRenderAPI = RenderAPI_Direct3D11;

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
		TraceLoggingRegister(steamvr_passthrough::logging::g_traceProvider);
#endif
	break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
	break;
    }
    return TRUE;
}
