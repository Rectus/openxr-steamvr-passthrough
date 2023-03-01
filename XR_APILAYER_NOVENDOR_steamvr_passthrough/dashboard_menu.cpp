#include "pch.h"
#include "dashboard_menu.h"
#include <log.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


DashboardMenu::DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
	, m_bMenuIsVisible(false)
	, m_displayValues()
{
	m_bRunThread = true;
	m_menuThread = std::thread(&DashboardMenu::RunThread, this);
}


DashboardMenu::~DashboardMenu()
{
	m_bRunThread = false;
	if (m_menuThread.joinable())
	{
		m_menuThread.join();
	}
}


void DashboardMenu::RunThread()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	while (!vrOverlay)
	{
		if (!m_bRunThread)
		{
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		vrOverlay = m_openVRManager->GetVROverlay();
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(OVERLAY_RES_WIDTH, OVERLAY_RES_HEIGHT);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	ImGui::StyleColorsDark();

	SetupDX11();
	ImGui_ImplDX11_Init(m_d3d11Device.Get(), m_d3d11DeviceContext.Get());
	CreateOverlay();


	while (m_bRunThread)
	{
		TickMenu();

		vrOverlay->WaitFrameSync(100);
	}


	ImGui_ImplDX11_Shutdown();
	ImGui::GetIO().BackendRendererUserData = NULL;
	ImGui::DestroyContext();

	if (vrOverlay)
	{
		vrOverlay->DestroyOverlay(m_overlayHandle);
	}
}


std::string GetImageFormatName(ERenderAPI api, int64_t format)
{
	switch (api)
	{
	case DirectX11:
	case DirectX12:

		switch (format)
		{		
		case 29:
			return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
		case 91:
			return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
		case 2:
			return "DXGI_FORMAT_R32G32B32A32_FLOAT";
		case 10:
			return "DXGI_FORMAT_R16G16B16A16_FLOAT";
		case 24:
			return "DXGI_FORMAT_R10G10B10A2_UNORM";
		case 40:
			return "DXGI_FORMAT_D32_FLOAT";
		case 55:
			return "DXGI_FORMAT_D16_UNORM";
		case 45:
			return "DXGI_FORMAT_D24_UNORM_S8_UINT";
		case 20:
			return "DXGI_FORMAT_D32_FLOAT_S8X24_UINT";

		default:
			return "Unknown format";
		}
	case Vulkan:

		switch (format)
		{
		case 43:
			return "VK_FORMAT_R8G8B8A8_SRGB";
		case 50:
			return "VK_FORMAT_B8G8R8A8_SRGB";
		case 109: 
			return "VK_FORMAT_R32G32B32A32_SFLOAT";
		case 106: 
			return "VK_FORMAT_R32G32B32_SFLOAT";
		case 97: 
			return "VK_FORMAT_R16G16B16A16_SFLOAT";
		case 126: 
			return "VK_FORMAT_D32_SFLOAT";
		case 124: 
			return "VK_FORMAT_D16_UNORM";
		case 129: 
			return "VK_FORMAT_D24_UNORM_S8_UINT";
		case 130:
			return "VK_FORMAT_D32_SFLOAT_S8_UINT";

		default:
			return "Unknown format";
		}
		
	default:
		return "Unknown format";
	}
}


void ScrollableSlider(const char* label, float* v, float v_min, float v_max, const char* format, float scrollFactor)
{
	ImGui::SliderFloat(label, v, v_min, v_max, format, ImGuiSliderFlags_None);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	if (ImGui::IsItemHovered())
	{
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel)
		{
			if (ImGui::IsItemActive())
			{
				ImGui::ClearActiveID();
			}
			else
			{
				*v += wheel * scrollFactor;
				if (*v < v_min) { *v = v_min; }
				else if (*v > v_max) { *v = v_max; }
			}
		}
	}
}


void ScrollableSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, int scrollFactor)
{
	ImGui::SliderInt(label, v, v_min, v_max, format, ImGuiSliderFlags_None);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	if (ImGui::IsItemHovered())
	{
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel)
		{
			if (ImGui::IsItemActive())
			{
				ImGui::ClearActiveID();
			}
			else
			{
				*v += (int)wheel * scrollFactor;
				if (*v < v_min) { *v = v_min; }
				else if (*v > v_max) { *v = v_max; }
			}
		}
	}
}


void DashboardMenu::TickMenu() 
{
	HandleEvents();

	if (!m_bMenuIsVisible)
	{
		return;
	}

	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();
	Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();
	Config_Stereo& stereoCustomConfig = m_configManager->GetConfig_CustomStereo();

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplDX11_NewFrame();

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::Begin("OpenXR Passthrough", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

	ImGui::BeginChild("Main Pane", ImVec2(OVERLAY_RES_WIDTH * 0.35f, 0));

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Session"))
	{
		if (m_displayValues.bSessionActive)
		{
			ImGui::Text("Session: Active");
		}
		else
		{
			ImGui::Text("Session: Inactive");
		}

		switch(m_displayValues.renderAPI)
		{
		case DirectX11:
			ImGui::Text("Render API: DirectX 11");
			break;
		case DirectX12:
			ImGui::Text("Render API: DirectX 12");
			break;
		case Vulkan:
			ImGui::Text("Render API: Vulkan");
			break;
		default:
			ImGui::Text("Render API: None");
		}

		ImGui::Text("Application: %s", m_displayValues.currentApplication.c_str());
		ImGui::Text("Resolution: %i x %i", m_displayValues.frameBufferWidth, m_displayValues.frameBufferHeight);
		if (m_displayValues.frameBufferFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)
		{
			if (m_displayValues.frameBufferFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)
			{
				ImGui::Text("Flags: Unpremultiplied alpha");
			}
			else
			{
				ImGui::Text("Flags: Premultiplied alpha");
			}
		}
		else
		{
			ImGui::Text("Flags: No alpha");
		}

		ImGui::Text("Buffer format: %s (%li)", GetImageFormatName(m_displayValues.renderAPI, m_displayValues.frameBufferFormat).c_str(), m_displayValues.frameBufferFormat);

		ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
		ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
		ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
		ImGui::Text("Stereo reconstruction duration: %.2fms", m_displayValues.stereoReconstructionTimeMS);
	}

	
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Main Settings"))
	{
		ImGui::BeginGroup();
		ImGui::Checkbox("Enable Passthrough", &mainConfig.EnablePassthough);

		ImGui::Text("Projection Mode");
		if (ImGui::RadioButton("Room View 2D", mainConfig.ProjectionMode == ProjectionRoomView2D))
		{
			mainConfig.ProjectionMode = ProjectionRoomView2D;
		}
		if (ImGui::RadioButton("Custom 2D (Experimental)", mainConfig.ProjectionMode == ProjectionCustom2D))
		{
			mainConfig.ProjectionMode = ProjectionCustom2D;
		}
		if (ImGui::RadioButton("Stereo 3D (Experimental)", mainConfig.ProjectionMode == ProjectionStereoReconstruction))
		{
			mainConfig.ProjectionMode = ProjectionStereoReconstruction;
		}

		ImGui::Separator();

		ScrollableSlider("Projection Dist.", &mainConfig.ProjectionDistanceFar, 0.5f, 20.0f, "%.1f", 0.1f);
		ScrollableSlider("Floor Height Offset", &mainConfig.FloorHeightOffset, 0.0f, 2.0f, "%.2f", 0.01f);
		ScrollableSlider("Field of View Scale", &mainConfig.FieldOfViewScale, 0.0f, 1.0f, "%.1f", 0.0f);

		ScrollableSlider("Opacity", &mainConfig.PassthroughOpacity, 0.0f, 1.0f, "%.1f", 0.1f);
		ImGui::Separator();
		ScrollableSlider("Brightness", &mainConfig.Brightness, -50.0f, 50.0f, "%.0f", 1.0f);
		ScrollableSlider("Contrast", &mainConfig.Contrast, 0.0f, 2.0f, "%.1f", 0.1f);
		ScrollableSlider("Saturation", &mainConfig.Saturation, 0.0f, 2.0f, "%.1f", 0.1f);
		ImGui::EndGroup();
		
	}

	ImGui::BeginChild("Sep", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
	ImGui::EndChild();

	if (ImGui::Button("Reset To Default"))
	{
		m_configManager->ResetToDefaults();
	}

	ImGui::EndChild();
	ImGui::SameLine();

	ImGui::BeginChild("Core Pane", ImVec2(OVERLAY_RES_WIDTH * 0.38f, ImGui::GetContentRegionAvail().y));

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("OpenXR Core"))
	{
		if (m_displayValues.bCorePassthroughActive)
		{
			ImGui::Text("Core passthrough: Active");
		}
		else
		{
			ImGui::Text("Core passthrough: Inactive");
		}

		ImGui::Text("Application requested mode: ");
		if (m_displayValues.CoreCurrentMode == 3) { ImGui::Text("Alpha Blend"); }
		else if (m_displayValues.CoreCurrentMode == 2) { ImGui::Text("Additive"); }
		else if (m_displayValues.CoreCurrentMode == 1) { ImGui::Text("Opaque"); }
		else { ImGui::Text("Unknown"); }

		ImGui::Separator();

		ImGui::Checkbox("Enable###CoreEnable", &coreConfig.CorePassthroughEnable);
		ImGui::Separator();

		ImGui::BeginGroup();
		ImGui::Text("Blend Modes");
		ImGui::Checkbox("Alpha Blend###CoreAlpha", &coreConfig.CoreAlphaBlend);
		ImGui::Checkbox("Additive###CoreAdditive", &coreConfig.CoreAdditive);
		ImGui::EndGroup();

		ImGui::Separator();

		ImGui::BeginGroup();
		ImGui::Text("Preferred Mode");
		if (ImGui::RadioButton("Alpha Blend###CorePref3", coreConfig.CorePreferredMode == 3))
		{
			coreConfig.CorePreferredMode = 3;
		}
		if (ImGui::RadioButton("Additive###CorePref2", coreConfig.CorePreferredMode == 2))
		{
			coreConfig.CorePreferredMode = 2;
		}
		if (ImGui::RadioButton("Opaque###CorePref1", coreConfig.CorePreferredMode == 1))
		{
			coreConfig.CorePreferredMode = 1;
		}
		ImGui::EndGroup();
	}

	if (ImGui::CollapsingHeader("Stereo Reconstuction"))
	{
		ImGui::Checkbox("Use Multiple Cores", &stereoCustomConfig.StereoUseMulticore);
		ImGui::Checkbox("Rectification Filtering", &stereoCustomConfig.StereoRectificationFiltering);
		ScrollableSliderInt("Frame Skip Ratio", &stereoCustomConfig.StereoFrameSkip, 0, 14, "%d", 1);
		
		ScrollableSliderInt("Image Downscale Factor", &stereoCustomConfig.StereoDownscaleFactor, 1, 16, "%d", 1);
		ImGui::Separator();

		/*ImGui::BeginGroup();
		ImGui::Text("Algorithm");
		if (ImGui::RadioButton("BM###AlgBM", sterestereoCustomConfigoConfig.StereoAlgorithm == StereoAlgorithm_BM))
		{
			stereoCustomConfig.StereoAlgorithm = StereoAlgorithm_BM;
		}
		if (ImGui::RadioButton("SGBM###AlgSGBM", stereoCustomConfig.StereoAlgorithm == StereoAlgorithm_SGBM))
		{
			stereoCustomConfig.StereoAlgorithm = StereoAlgorithm_SGBM;
		}
		ImGui::EndGroup();

		ImGui::Separator();*/

		ImGui::BeginGroup();
		ImGui::Text("SGBM Mode");
		if (ImGui::RadioButton("Single Pass: 3 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_SGBM3Way))
		{
			stereoCustomConfig.StereoSGBM_Mode = StereoMode_SGBM3Way;
		}
		if (ImGui::RadioButton("Single Pass: 5 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_SGBM))
		{
			stereoCustomConfig.StereoSGBM_Mode = StereoMode_SGBM;
		}
		if (ImGui::RadioButton("Full Pass: 4 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_HH4))
		{
			stereoCustomConfig.StereoSGBM_Mode = StereoMode_HH4;
		}
		if (ImGui::RadioButton("Full Pass: 8 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_HH))
		{
			stereoCustomConfig.StereoSGBM_Mode = StereoMode_HH;
		}
		ImGui::EndGroup();

		ImGui::Separator();

		ScrollableSliderInt("BlockSize", &stereoCustomConfig.StereoBlockSize, 1, 35, "%d", 2);
		if (stereoCustomConfig.StereoBlockSize % 2 == 0) { stereoCustomConfig.StereoBlockSize += 1;	}

		//ScrollableSliderInt("MinDisparity", &stereoCustomConfig.StereoMinDisparity, 0, 128, "%d", 1);
		ScrollableSliderInt("MaxDisparity", &stereoCustomConfig.StereoMaxDisparity, 16, 256, "%d", 1);
		if (stereoCustomConfig.StereoMinDisparity % 2 != 0) { stereoCustomConfig.StereoMinDisparity += 1; }
		if (stereoCustomConfig.StereoMinDisparity >= stereoCustomConfig.StereoMaxDisparity) { stereoCustomConfig.StereoMaxDisparity = stereoCustomConfig.StereoMinDisparity + 2; }
		if (stereoCustomConfig.StereoMaxDisparity < 16) { stereoCustomConfig.StereoMaxDisparity = 16; }
		if (stereoCustomConfig.StereoMaxDisparity % 16 != 0) { stereoCustomConfig.StereoMaxDisparity -= stereoCustomConfig.StereoMaxDisparity % 16; }

		ScrollableSliderInt("SGBM_P1", &stereoCustomConfig.StereoSGBM_P1, 0, 256, "%d", 8);
		ScrollableSliderInt("SGBM_P2", &stereoCustomConfig.StereoSGBM_P2, 0, 256, "%d", 32);
		ScrollableSliderInt("SGBM_DispMaxDiff", &stereoCustomConfig.StereoSGBM_DispMaxDiff, 0, 256, "%d", 1);
		ScrollableSliderInt("SGBM_PreFilterCap", &stereoCustomConfig.StereoSGBM_PreFilterCap, 0, 128, "%d", 1);
		ScrollableSliderInt("SGBM_UniquenessRatio", &stereoCustomConfig.StereoSGBM_UniquenessRatio, 1, 32, "%d", 1);
		ScrollableSliderInt("SGBM_SpeckleWindowSize", &stereoCustomConfig.StereoSGBM_SpeckleWindowSize, 0, 300, "%d", 10);
		ScrollableSliderInt("SGBM_SpeckleRange", &stereoCustomConfig.StereoSGBM_SpeckleRange, 1, 8, "%d", 1);

		ImGui::BeginGroup();
		ImGui::Text("Filtering");
		if (ImGui::RadioButton("None###FiltNone", stereoCustomConfig.StereoFiltering == StereoFiltering_None))
		{
			stereoCustomConfig.StereoFiltering = StereoFiltering_None;
		}
		if (ImGui::RadioButton("WLS###FiltWLS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS))
		{
			stereoCustomConfig.StereoFiltering = StereoFiltering_WLS;
		}
		if (ImGui::RadioButton("WLS & FBS###FiltFBS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS_FBS))
		{
			stereoCustomConfig.StereoFiltering = StereoFiltering_WLS_FBS;
		}
		ImGui::EndGroup();

		ScrollableSlider("WLS_Lambda", &stereoCustomConfig.StereoWLS_Lambda, 1.0f, 10000.0f, "%.0f", 100.0f);
		ScrollableSlider("WLS_Sigma", &stereoCustomConfig.StereoWLS_Sigma, 0.5f, 2.0f, "%.1f", 0.1f);
		ScrollableSlider("FBS_Spatial", &stereoCustomConfig.StereoFBS_Spatial, 0.0f, 50.0f, "%.0f", 1.0f);
		ScrollableSlider("FBS_Luma", &stereoCustomConfig.StereoFBS_Luma, 0.0f, 16.0f, "%.0f", 1.0f);
		ScrollableSlider("FBS_Chroma", &stereoCustomConfig.StereoFBS_Chroma, 0.0f, 16.0f, "%.0f", 1.0f);
		ScrollableSlider("FBS_Lambda", &stereoCustomConfig.StereoFBS_Lambda, 0.0f, 256.0f, "%.0f", 1.0f);

		ScrollableSliderInt("FBS_Iterations", &stereoCustomConfig.StereoFBS_Iterations, 1, 35, "%d", 1);
	}

	stereoCustomConfig.StereoReconstructionFreeze = stereoConfig.StereoReconstructionFreeze;
	stereoConfig = stereoCustomConfig;

	ImGui::EndChild();

	ImGui::SameLine();


	ImGui::BeginChild("Overrides Pane", ImVec2(0, ImGui::GetContentRegionAvail().y));
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Overrides"))
	{
		ImGui::BeginGroup();
		ImGui::Checkbox("Force Passthough Mode", &coreConfig.CoreForcePassthrough);

		ImGui::BeginGroup();
		if (ImGui::RadioButton("Alpha Blend###CoreForce3", coreConfig.CoreForceMode == 3))
		{
			coreConfig.CoreForceMode = 3;
		}
		if (ImGui::RadioButton("Additive###CoreForcef2", coreConfig.CoreForceMode == 2))
		{
			coreConfig.CoreForceMode = 2;
		}
		if (ImGui::RadioButton("Opaque###Coreforce1", coreConfig.CoreForceMode == 1))
		{
			coreConfig.CoreForceMode = 1;
		}
		if (ImGui::RadioButton("Masked###Coreforce0", coreConfig.CoreForceMode == 0))
		{
			coreConfig.CoreForceMode = 0;
		}
		ImGui::EndGroup();
		ImGui::Separator();
		ImGui::Text("Masked Croma Key Settings");
		ScrollableSlider("Chroma Range", &coreConfig.CoreForceMaskedFractionChroma, 0.0f, 1.0f, "%.2f", 0.01f);
		ScrollableSlider("Luma Range", &coreConfig.CoreForceMaskedFractionLuma, 0.0f, 1.0f, "%.2f", 0.01f);
		ScrollableSlider("Smoothing", &coreConfig.CoreForceMaskedSmoothing, 0.01f, 0.2f, "%.3f", 0.005f);
		ImGui::ColorEdit3("Key", coreConfig.CoreForceMaskedKeyColor);

		ImGui::BeginGroup();
		ImGui::Text("Chroma Key Source");
		if (ImGui::RadioButton("Application", !coreConfig.CoreForceMaskedUseCameraImage))
		{
			coreConfig.CoreForceMaskedUseCameraImage = false;
		}
		if (ImGui::RadioButton("Passthrough Camera", coreConfig.CoreForceMaskedUseCameraImage))
		{
			coreConfig.CoreForceMaskedUseCameraImage = true;
		}
		ImGui::EndGroup();

		ImGui::Checkbox("Invert mask", &coreConfig.CoreForceMaskedInvertMask);

		ImGui::EndGroup();
	}
	if (ImGui::CollapsingHeader("Debug"))
	{
		ImGui::Checkbox("Freeze Stereo Projection", &stereoConfig.StereoReconstructionFreeze);
		ImGui::Checkbox("Debug Depth", &mainConfig.DebugDepth);
		ImGui::Checkbox("Debug Valid Stereo", &mainConfig.DebugStereoValid);
		ImGui::Checkbox("Show Test Image", &mainConfig.ShowTestImage);
	}
	ImGui::EndChild();

	if (ImGui::IsAnyItemActive())
	{
		m_configManager->ConfigUpdated();
	}

	ImGui::End();

	ImGui::PopStyleVar();

	ImGui::Render();

	ID3D11RenderTargetView* rtv = m_d3d11RTV.Get();
	m_d3d11DeviceContext->OMSetRenderTargets(1, &rtv, NULL);
	const float clearColor[4] = { 0, 0, 0, 1 };
	m_d3d11DeviceContext->ClearRenderTargetView(m_d3d11RTV.Get(), clearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	m_d3d11DeviceContext->Flush();

	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Auto;
	texture.eType = vr::TextureType_DXGISharedHandle;

	ComPtr<IDXGIResource> DXGIResource;
	m_d3d11Texture->QueryInterface(IID_PPV_ARGS(&DXGIResource));
	DXGIResource->GetSharedHandle(&texture.handle);

	vr::EVROverlayError error = m_openVRManager->GetVROverlay()->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVR had an error on updating overlay (%d)\n", error);
	}
}


void DashboardMenu::CreateOverlay()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay)
	{
		return;
	}

	std::string overlayKey = std::format(DASHBOARD_OVERLAY_KEY, GetCurrentProcessId());

	vr::EVROverlayError error = vrOverlay->FindOverlay(overlayKey.c_str(), &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None && error != vr::EVROverlayError::VROverlayError_UnknownOverlay)
	{
		Log("Warning: SteamVR FindOverlay error (%d)\n", error);
	}

	if (m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		error = vrOverlay->CreateDashboardOverlay(overlayKey.c_str(), "OpenXR Passthrough", &m_overlayHandle, &m_thumbnailHandle);
		if (error != vr::EVROverlayError::VROverlayError_None)
		{
			ErrorLog("SteamVR overlay init error (%d)\n", error);
		}
		else
		{
			vrOverlay->SetOverlayInputMethod(m_overlayHandle, vr::VROverlayInputMethod_Mouse);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_IsPremultiplied, true);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

			vrOverlay->SetOverlayWidthInMeters(m_overlayHandle, 2.775f);

			vr::HmdVector2_t ScaleVec;
			ScaleVec.v[0] = OVERLAY_RES_WIDTH;
			ScaleVec.v[1] = OVERLAY_RES_HEIGHT;
			vrOverlay->SetOverlayMouseScale(m_overlayHandle, &ScaleVec);

			CreateThumbnail();
		}
	}
}


void DashboardMenu::DestroyOverlay()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		m_overlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}

	vr::EVROverlayError error = vrOverlay->DestroyOverlay(m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None)
	{
		ErrorLog("SteamVR DestroyOverlay error (%d)\n", error);
	}

	m_overlayHandle = vr::k_ulOverlayHandleInvalid;
}


void DashboardMenu::CreateThumbnail()
{
	char path[MAX_PATH];

	if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	{
		ErrorLog("Error opening icon.\n");
		return;
	}

	std::string pathStr = path;
	std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\passthrough_icon.png";
	
	m_openVRManager->GetVROverlay()->SetOverlayFromFile(m_thumbnailHandle, imgPath.c_str());
}


void DashboardMenu::HandleEvents()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	vr::VREvent_t event;
	while (vrOverlay->PollNextOverlayEvent(m_overlayHandle, &event, sizeof(event)))
	{
		vr::VREvent_Overlay_t& overlayData = (vr::VREvent_Overlay_t&)event.data;
		vr::VREvent_Mouse_t& mouseData = (vr::VREvent_Mouse_t&)event.data;
		vr::VREvent_Scroll_t& scrollData = (vr::VREvent_Scroll_t&)event.data;

		switch (event.eventType)
		{
		case vr::VREvent_MouseButtonDown:

			if (mouseData.button & vr::VRMouseButton_Left)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			}
			if (mouseData.button & vr::VRMouseButton_Middle)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
			}
			if (mouseData.button & vr::VRMouseButton_Right)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
			}

			break;

		case vr::VREvent_MouseButtonUp:

			if (mouseData.button & vr::VRMouseButton_Left)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			}
			if (mouseData.button & vr::VRMouseButton_Middle)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
			}
			if (mouseData.button & vr::VRMouseButton_Right)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
			}

			break;

		case vr::VREvent_MouseMove:

			io.AddMousePosEvent(mouseData.x, OVERLAY_RES_HEIGHT - mouseData.y);

			break;

		case vr::VREvent_ScrollDiscrete:
		case vr::VREvent_ScrollSmooth:

			io.AddMouseWheelEvent(scrollData.xdelta, scrollData.ydelta);
			break;

		case vr::VREvent_FocusEnter:


			break;

		case vr::VREvent_FocusLeave:

			if (((vr::VREvent_Overlay_t&)event.data).overlayHandle == m_overlayHandle)
			{
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			}

			break;

		case vr::VREvent_OverlayShown:

			m_bMenuIsVisible = true;
			break;

		case vr::VREvent_OverlayHidden:

			m_bMenuIsVisible = false;
			m_configManager->DispatchUpdate();
			break;

		case vr::VREvent_DashboardActivated:
			//bThumbnailNeedsUpdate = true;
			break;

		}
	}
}


void DashboardMenu::SetupDX11()
{
	D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &m_d3d11Device, NULL, &m_d3d11DeviceContext);

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = OVERLAY_RES_WIDTH;
	textureDesc.Height = OVERLAY_RES_HEIGHT;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	m_d3d11Device->CreateTexture2D(&textureDesc, nullptr, &m_d3d11Texture);

	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	m_d3d11Device->CreateRenderTargetView(m_d3d11Texture.Get(), &renderTargetViewDesc, &m_d3d11RTV);
}
