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


void DashboardMenu::TickMenu() 
{
	HandleEvents();

	if (!m_bMenuIsVisible)
	{
		return;
	}

	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplDX11_NewFrame();

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::Begin("OpenXR Passthrough", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

	ImGui::BeginChild("Main Pane", ImVec2(OVERLAY_RES_WIDTH * 0.4f, 0));

	if (ImGui::CollapsingHeader("Session"), ImGuiTreeNodeFlags_DefaultOpen)
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

		ImGui::Text("Buffer format: %li", m_displayValues.frameBufferFormat);

		ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
		ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
		ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
	}

	

	if (ImGui::CollapsingHeader("Main Settings"), ImGuiTreeNodeFlags_DefaultOpen)
	{
		ImGui::BeginGroup();
		ImGui::Checkbox("Enable Passthrough", &mainConfig.EnablePassthough);
		ImGui::Checkbox("Show Test Image", &mainConfig.ShowTestImage);
		ImGui::Separator();

		ScrollableSlider("Projection Dist.", &mainConfig.ProjectionDistanceFar, 0.5f, 20.0f, "%.1f", 0.1f);
		//ScrollableSlider("Near Projection Distance", &mainConfig.ProjectionDistanceNear, 0.5f, 10.0f, "%.1f", 0.1f);

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

	ImGui::BeginChild("Core Pane", ImVec2(OVERLAY_RES_WIDTH * 0.25f, ImGui::GetContentRegionAvail().y));
	if (ImGui::CollapsingHeader("OpenXR Core"), ImGuiTreeNodeFlags_DefaultOpen)
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

	ImGui::EndChild();
	ImGui::SameLine();

	ImGui::BeginChild("Overrides Pane", ImVec2(0, ImGui::GetContentRegionAvail().y));
	if (ImGui::CollapsingHeader("Overrides"), ImGuiTreeNodeFlags_DefaultOpen)
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


		ImGui::EndGroup();
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
