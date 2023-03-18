﻿// MIT License
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
#include <shlobj_core.h>
#include <pathcch.h>
#include "lodepng.h"

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

		~OpenXrLayer() = default;

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
			
			XrResult result = OpenXrApi::xrCreateInstance(createInfo);

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

			PWSTR path;
			std::wstring filePath(PATHCCH_MAX_CCH, L'\0');

			SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
			lstrcpyW((PWSTR)filePath.c_str(), path);
			PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_DIR);
			CreateDirectoryW((PWSTR)filePath.data(), NULL);
			PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_NAME);

			m_configManager = std::make_shared<ConfigManager>(filePath);
			m_configManager->ReadConfigFile();


			// Check that the SteamVR OpenXR runtime is being used.
			if (m_configManager->GetConfig_Main().RequireSteamVRRuntime)
			{
				XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
				OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties);

				if (strncmp(instanceProperties.runtimeName, "SteamVR/OpenXR", 14))
				{
					ErrorLog("The active OpenXR runtime is not SteamVR, passthrough layer not enabled");
					return result;
				}
			}

			m_openVRManager = std::make_shared<OpenVRManager>();
			m_dashboardMenu = std::make_unique<DashboardMenu>(g_dllModule, m_configManager, m_openVRManager);

			m_bSuccessfullyLoaded = true;
			Log("OpenXR instance successfully created\n");

			return result;
		}

		XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override
		{
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


		bool SetupRenderer(const XrInstance instance,
			const XrSessionCreateInfo* createInfo,
			const XrSession* session)
		{
			uint32_t cameraTextureWidth;
			uint32_t cameraTextureHeight;
			uint32_t cameraFrameBufferSize;
			const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);

			while (entry)
			{
				switch (entry->type)
				{
				case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
				{
					const XrGraphicsBindingD3D11KHR* dx11bindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
					m_Renderer = std::make_shared<PassthroughRendererDX11>(dx11bindings->device, g_dllModule, m_configManager);

					if (!m_cameraManager.get())
					{
						m_cameraManager = std::make_shared<CameraManager>(m_Renderer, DirectX11, m_configManager, m_openVRManager);
					}
					if (!m_cameraManager->InitCamera())
					{
						return false;
					}
					
					m_cameraManager->GetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					m_Renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					if(!m_Renderer->InitRenderer())
					{
						return false;
					}

					m_depthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_cameraManager);

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = DirectX11;
					Log("Direct3D 11 renderer initialized\n");

					return true;
				}

				case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR:
				{
					const XrGraphicsBindingD3D12KHR* dx12bindings = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry);
					m_Renderer = std::make_unique<PassthroughRendererDX12>(dx12bindings->device, dx12bindings->queue, g_dllModule, m_configManager);

					if (!m_cameraManager.get())
					{
						m_cameraManager = std::make_unique<CameraManager>(m_Renderer, DirectX12, m_configManager, m_openVRManager);
					}
					if (!m_cameraManager->InitCamera())
					{
						return false;
					}

					m_cameraManager->GetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					m_Renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					if (!m_Renderer->InitRenderer())
					{
						return false;
					}
					
					m_depthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_cameraManager);

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = DirectX12;
					Log("Direct3D 12 renderer initialized\n");

					return true;
				}

				case XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR: // same as XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR
				{
					const XrGraphicsBindingVulkanKHR* vulkanbindings = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(entry);
					m_Renderer = std::make_unique<PassthroughRendererVulkan>(*vulkanbindings, g_dllModule, m_configManager);
					
					if (!m_cameraManager.get())
					{
						m_cameraManager = std::make_unique<CameraManager>(m_Renderer, Vulkan, m_configManager, m_openVRManager);
					}
					if (!m_cameraManager->InitCamera())
					{
						return false;
					}
					
					m_cameraManager->GetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					m_Renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
					if (!m_Renderer->InitRenderer())
					{
						return false;
					}

					m_depthReconstruction = std::make_shared<DepthReconstruction>(m_configManager, m_openVRManager, m_cameraManager);

					m_dashboardMenu->GetDisplayValues().bSessionActive = true;
					m_dashboardMenu->GetDisplayValues().renderAPI = Vulkan;
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


		XrResult xrCreateSession(XrInstance instance,
					 const XrSessionCreateInfo* createInfo,
					 XrSession* session) override
		{
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
					ErrorLog("Loading failed, not attaching to session!");
				}
				else if (isSystemHandled(createInfo->systemId) && !isCurrentSession(*session))
				{
					m_currentInstance = instance;
					m_currentSession = *session;
					if (SetupRenderer(instance, createInfo, session))
					{
						Log("Passthrough API layer enabled for session\n");
						m_bPassthroughAvailable = true;
						m_bUsePassthrough = m_configManager->GetConfig_Main().EnablePassthrough;
						m_dashboardMenu->GetDisplayValues().currentApplication = GetApplicationName();
					}
					else
					{
						ErrorLog("Failed to initialize renderer\n");
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
				m_Renderer.reset();

				if (m_cameraManager.get())
				{
					m_cameraManager->DeinitCamera();
				}
				m_cameraManager.reset();
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


		XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance,
							  XrSystemId systemId,
							  XrViewConfigurationType viewConfigurationType,
							  uint32_t environmentBlendModeCapacityInput,
							  uint32_t* environmentBlendModeCountOutput,
							  XrEnvironmentBlendMode* environmentBlendModes)
		{
			const XrResult result = OpenXrApi::xrEnumerateEnvironmentBlendModes(instance,
								systemId,
								viewConfigurationType,
								environmentBlendModeCapacityInput,
								environmentBlendModeCountOutput,
								environmentBlendModes);

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

			*environmentBlendModeCountOutput = numBlendModes;

			return XR_SUCCESS;
		}


		XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) 
		{
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
			XrResult result = OpenXrApi::xrCreateSwapchain(session, createInfo, swapchain);
			if (XR_SUCCEEDED(result))
			{
				m_swapchainProperties[*swapchain] = *createInfo;
			}
			return result;
		}


		XrResult xrDestroySwapchain(XrSwapchain swapchain)
		{
			return OpenXrApi::xrDestroySwapchain(swapchain);
		}


		XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index)
		{
			if (!m_bUsePassthrough)
			{
				return OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);
			}

			// If the swapchain is held just act like it was reaquired.
			auto It = m_heldSwapchains.find(swapchain);
			if (It != m_heldSwapchains.end())
			{
				m_heldSwapchains.erase(swapchain);
				m_acquiredSwapchains[swapchain] = *index;
				return XR_SUCCESS;
			}

			 XrResult result = OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);
			 if (XR_SUCCEEDED(result))
			 {
				 m_acquiredSwapchains[swapchain] = *index;
				 //Log("Acquired: %i, %i\n", swapchain, *index);
			 }
			 else
			 {
				 ErrorLog("Error in xrAcquireSwapchainImage: %i\n", result);
			 }

			 return result;
		}


		XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo)
		{
			if (!m_bUsePassthrough)
			{
				return OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
			}

			// Delay releasing the swapchains until we can render the passthrough.
			auto It = m_acquiredSwapchains.find(swapchain);

			if (It != m_acquiredSwapchains.end())
			{
				//Log("Held: %i, %i\n", swapchain, It->second);
				m_heldSwapchains[swapchain] = It->second;
				m_acquiredSwapchains.erase(It);
				return XR_SUCCESS;
			}
			else
			{
				DebugLog("Swapchain not acquired: %i\n", swapchain);
				return OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
			}
		}


		XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
		{
			if (isCurrentSession(session))
			{
				m_bUsePassthrough = m_bPassthroughAvailable && m_configManager->GetConfig_Main().EnablePassthrough;
			}

			return OpenXrApi::xrBeginFrame(session, frameBeginInfo);
		}


		int UpdateSwapchains(const ERenderEye eye, const XrCompositionLayerProjection* layer)
		{
			XrSwapchain* storedSwapchain = (eye == LEFT_EYE) ? &m_swapChainLeft : &m_swapChainRight;
			int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

			const XrSwapchain newSwapchain = layer->views[viewIndex].subImage.swapchain;

			auto props = m_swapchainProperties.find(newSwapchain);

			if (props == m_swapchainProperties.end())
			{
				return -1;
			}

			int64_t imageFormat = props->second.format;

			auto held = m_heldSwapchains.find(newSwapchain);

			if (held == m_heldSwapchains.end())
			{
				return -1;
			}

			if (eye == LEFT_EYE)
			{
				m_dashboardMenu->GetDisplayValues().frameBufferFormat = props->second.format;
			}

			int imageIndex = held->second;

			if (newSwapchain == *storedSwapchain)
			{
				return imageIndex;
			}

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
				return -1;
			}

			if (!m_configManager->GetConfig_Depth().DepthReadFromApplication)
			{
				return imageIndex;
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

			if (depthInfo != nullptr)
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
					numImages = 0;

					XrResult result = OpenXrApi::xrEnumerateSwapchainImages(depthInfo->subImage.swapchain, 3, &numImages, (XrSwapchainImageBaseHeader*)depthImages);
					if (XR_SUCCEEDED(result))
					{
						for (uint32_t i = 0; i < numImages; i++)
						{
							m_Renderer->InitDepthBuffer(eye, depthImages[i].texture, i, depthProps->second);
						}
					}
					else
					{
						ErrorLog("Error in xrEnumerateSwapchainImages when enumerating depthbuffers: %i\n", result);
					}
				}
			}

			return imageIndex;
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
			char path[MAX_PATH];

			if (FAILED(GetModuleFileNameA(g_dllModule, path, sizeof(path))))
			{
				ErrorLog("Error opening test pattern.\n");
				return;
			}

			std::string pathStr = path;
			std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\testpattern.png";

			std::lock_guard<std::mutex> writelock(texture.RWMutex);
			
			if (texture.CurrentTexture != DebugTexture_TestImage)
			{
				texture.Texture = std::vector<uint8_t>();
			}

			unsigned width, height;
			unsigned error = lodepng::decode(texture.Texture, width, height, imgPath.c_str());
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
			const XrCompositionLayerProjection* layer = (const XrCompositionLayerProjection*)frameEndInfo->layers[layerNum];
			std::shared_ptr<CameraFrame> frame;

			m_dashboardMenu->GetDisplayValues().frameBufferHeight = layer->views[0].subImage.imageRect.extent.height;
			m_dashboardMenu->GetDisplayValues().frameBufferWidth = layer->views[0].subImage.imageRect.extent.width;
			m_dashboardMenu->GetDisplayValues().frameBufferFlags = layer->layerFlags;

			if (!m_cameraManager->GetCameraFrame(frame))
			{
				return;
			}

			std::shared_lock readLock(frame->readWriteMutex);


			LARGE_INTEGER preRenderTime = StartPerfTimer();

			float frameToRenderTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, preRenderTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().frameToRenderLatencyMS = UpdateAveragePerfTime(m_frameToRenderTimes, frameToRenderTime, 20);

			LARGE_INTEGER displayTime;

			OpenXrApi::xrConvertTimeToWin32PerformanceCounterKHR(m_currentInstance, frameEndInfo->displayTime, &displayTime);

			float frameToPhotonsTime = GetPerfTimerDiff(frame->header.ulFrameExposureTime, displayTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().frameToPhotonsLatencyMS = UpdateAveragePerfTime(m_frameToPhotonTimes, frameToPhotonsTime, 20);

			
			m_cameraManager->CalculateFrameProjection(frame, *layer, frameEndInfo->displayTime, m_refSpaces[layer->space], m_depthReconstruction->GetDistortionParameters());
	

			int leftIndex = UpdateSwapchains(LEFT_EYE, layer);
			int rightIndex = UpdateSwapchains(RIGHT_EYE, layer);

			EPassthroughBlendMode blendMode = (EPassthroughBlendMode)frameEndInfo->environmentBlendMode;

			if (m_configManager->GetConfig_Core().CoreForcePassthrough && m_configManager->GetConfig_Core().CoreForceMode >= 0)
			{
				blendMode = (EPassthroughBlendMode)m_configManager->GetConfig_Core().CoreForceMode;
			}

			if (blendMode == AlphaBlendPremultiplied && layer->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)
			{
				blendMode = AlphaBlendUnpremultiplied;
			}

			if (m_configManager->GetConfig_Main().DebugTexture == DebugTexture_TestImage &&
				m_configManager->GetDebugTexture().CurrentTexture != DebugTexture_TestImage)
			{
				GetTestPattern(m_configManager->GetDebugTexture());
			}
			
			std::shared_ptr<DepthFrame> depthFrame = m_depthReconstruction->GetDepthFrame();

			m_Renderer->RenderPassthroughFrame(layer, frame.get(), blendMode, leftIndex, rightIndex, depthFrame, m_depthReconstruction->GetDistortionParameters());


			float renderTime = EndPerfTimer(preRenderTime.QuadPart);
			m_dashboardMenu->GetDisplayValues().renderTimeMS = UpdateAveragePerfTime(m_passthroughRenderTimes, renderTime, 20);

			m_dashboardMenu->GetDisplayValues().stereoReconstructionTimeMS = m_depthReconstruction->GetReconstructionPerfTime();
			m_dashboardMenu->GetDisplayValues().frameRetrievalTimeMS = m_cameraManager->GetFrameRetrievalPerfTime();
		}


		XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) 
		{
			if (!isCurrentSession(session))
			{
				return OpenXrApi::xrEndFrame(session, frameEndInfo);
			}


			XrResult result;

			if (m_dashboardMenu.get())
			{
				m_dashboardMenu->GetDisplayValues().CoreCurrentMode = frameEndInfo->environmentBlendMode;
				m_dashboardMenu->GetDisplayValues().bCorePassthroughActive = false;
			}

			if (m_bUsePassthrough)
			{
				for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
				{
					if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && IsBlendModeEnabled(frameEndInfo->environmentBlendMode, (const XrCompositionLayerProjection*)frameEndInfo->layers[i]))
					{
						m_dashboardMenu->GetDisplayValues().bCorePassthroughActive = true;
						RenderPassthroughOnAppLayer(frameEndInfo, i);
						
						break;
					}
				}

				for (const auto& It : m_heldSwapchains)
				{
					result = OpenXrApi::xrReleaseSwapchainImage(It.first, nullptr);
					if (XR_FAILED(result))
					{
						ErrorLog("Error in xrReleaseSwapchainImage: %i\n", result);
					}
					//Log("Released: %i, %i\n", It.first, It.second);
				}

				m_heldSwapchains.clear();
			}

			XrFrameEndInfo modifiedFrameEndInfo = *frameEndInfo;
			modifiedFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

			result = OpenXrApi::xrEndFrame(session, &modifiedFrameEndInfo);
			return result;
		}


		private:

		bool isSystemHandled(XrSystemId systemId) const { return systemId == m_systemId; }
		bool isCurrentSession(XrSession session) const { return session == m_currentSession; }

		XrInstance m_currentInstance{XR_NULL_HANDLE};
		XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
		XrSession m_currentSession{XR_NULL_HANDLE};
		std::shared_ptr<IPassthroughRenderer> m_Renderer;
		std::shared_ptr<ConfigManager> m_configManager;
		std::shared_ptr<CameraManager> m_cameraManager;
		std::unique_ptr<DashboardMenu> m_dashboardMenu;
		std::shared_ptr<OpenVRManager> m_openVRManager;
		std::shared_ptr<DepthReconstruction> m_depthReconstruction;

		bool m_bSuccessfullyLoaded = false;
		bool m_bPassthroughAvailable = false;
		bool m_bUsePassthrough = false;

		XrSwapchain m_swapChainLeft{XR_NULL_HANDLE};
		XrSwapchain m_swapChainRight{XR_NULL_HANDLE};

		std::map<XrSpace, XrReferenceSpaceCreateInfo> m_refSpaces{};
		std::map<XrSwapchain, XrSwapchainCreateInfo> m_swapchainProperties{};
		std::map<XrSwapchain, uint32_t> m_acquiredSwapchains{};
		std::map<XrSwapchain, uint32_t> m_heldSwapchains{};

		std::deque<float> m_frameToRenderTimes;
		std::deque<float> m_frameToPhotonTimes;
		std::deque<float> m_passthroughRenderTimes;

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
