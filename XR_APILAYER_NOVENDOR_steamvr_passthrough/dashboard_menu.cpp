#include "pch.h"
#include "dashboard_menu.h"
#include <log.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


// DirectX 11 initialization
DashboardMenu::DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, ID3D11Device* d3dDevice)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
	, m_d3d11Device(d3dDevice)
	, m_API(DirectX11)
	, m_bMenuIsVisible(false)
	, m_displayValues()
{
	m_displayValues.bSessionActive = true;
	m_displayValues.renderAPI = DirectX11;
	d3dDevice->GetImmediateContext(&m_d3d11DeviceContext);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(OVERLAY_RES_WIDTH, OVERLAY_RES_HEIGHT);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	ImGui::StyleColorsDark();
	

	ImGui_ImplDX11_Init(d3dDevice, m_d3d11DeviceContext.Get());

	SetupDX11();
	CreateOverlay();
}


// DirectX 12 initialization
DashboardMenu::DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, ID3D12Device* device, ID3D12CommandQueue* commandQueue)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
	, m_d3d12Device(device)
	, m_d3d12CommandQueue(commandQueue)
	, m_API(DirectX12)
	, m_bMenuIsVisible(false)
{
	m_displayValues.bSessionActive = true;
	m_displayValues.renderAPI = DirectX12;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); //(void)io;
	io.DisplaySize = ImVec2(OVERLAY_RES_WIDTH, OVERLAY_RES_HEIGHT);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	ImGui::StyleColorsDark();

	SetupDX12();
	ImGui_ImplDX12_Init(device, 1, DXGI_FORMAT_R8G8B8A8_UNORM, m_d3d12SRVHeap.Get(), m_d3d12SRVHeap->GetCPUDescriptorHandleForHeapStart(), m_d3d12SRVHeap->GetGPUDescriptorHandleForHeapStart());

	CreateOverlay();
}


DashboardMenu::~DashboardMenu()
{
	if (m_API == DirectX11)
	{
		ImGui_ImplDX11_Shutdown();
		ImGui::GetIO().BackendRendererUserData = NULL;
	}
	else if (m_API == DirectX12)
	{
		ImGui_ImplDX12_Shutdown();
		ImGui::GetIO().BackendRendererUserData = NULL;
	}

	ImGui::DestroyContext();

	if (vr::VROverlay())
	{
		vr::VROverlay()->DestroyOverlay(m_overlayHandle);
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


	if (m_API == DirectX11)
	{
		ImGui_ImplDX11_NewFrame();
	}
	else if (m_API == DirectX12)
	{
		ImGui_ImplDX12_NewFrame();
	}
	
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::Begin("OpenXR Passthrough", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

	ImGui::BeginChild("Main Pane", ImVec2(OVERLAY_RES_WIDTH * 0.4f, 0));

	if (ImGui::CollapsingHeader("Session"), ImGuiTreeNodeFlags_DefaultOpen)
	{
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
	}

	ImGui::Text("Buffer format: %li", m_displayValues.frameBufferFormat);

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
		ScrollableSlider("Brightness", &mainConfig.Brightness, -10.0f, 10.0f, "%.2f", 0.1f);
		ScrollableSlider("Contrast", &mainConfig.Contrast, 0.9f, 1.1f, "%.3f", 0.001f);
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

	ImGui::BeginChild("Core Pane", ImVec2(OVERLAY_RES_WIDTH * 0.28f, ImGui::GetContentRegionAvail().y));
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
		ImGui::Separator();
		ImGui::Text("Masked Croma Key Settings");
		ScrollableSlider("Range", &coreConfig.CoreForceMaskedFraction, 0.0f, 1.0f, "%.2f", 0.01f);
		ScrollableSlider("Smoothing", &coreConfig.CoreForceMaskedSmoothing, 0.01f, 0.2f, "%.3f", 0.005f);
		ImGui::ColorEdit3("Key", coreConfig.CoreForceMaskedKeyColor);

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

	vr::D3D12TextureData_t textureData;
	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Auto;

	if (m_API == DirectX11)
	{
		ID3D11RenderTargetView* rtv = m_d3d11RTV.Get();
		m_d3d11DeviceContext->OMSetRenderTargets(1, &rtv, NULL);
		const float clearColor[4] = { 0, 0, 0, 1 };
		m_d3d11DeviceContext->ClearRenderTargetView(m_d3d11RTV.Get(), clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		ComPtr<IDXGIResource> DXGIResource;
		m_d3d11Texture->QueryInterface(IID_PPV_ARGS(&DXGIResource));

		texture.eType = vr::TextureType_DXGISharedHandle;
		DXGIResource->GetSharedHandle(&texture.handle);
	}
	else if (m_API == DirectX12)
	{
		m_d3d12CommandAllocator->Reset();
		m_d3d12CommandList->Reset(m_d3d12CommandAllocator.Get(), NULL);

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = m_d3d12Texture.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		m_d3d12CommandList->ResourceBarrier(1, &barrier);

		const float clearColor[4] = { 0, 0, 0, 1 };
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_d3d12RTVHeap->GetCPUDescriptorHandleForHeapStart();
		m_d3d12CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
		m_d3d12CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
		m_d3d12CommandList->SetDescriptorHeaps(1, m_d3d12SRVHeap.GetAddressOf());

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_d3d12CommandList.Get());

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		m_d3d12CommandList->ResourceBarrier(1, &barrier);
		m_d3d12CommandList->Close();

		m_d3d12CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)m_d3d12CommandList.GetAddressOf());

		textureData.m_pResource = m_d3d12Texture.Get();
		textureData.m_pCommandQueue = m_d3d12CommandQueue.Get();
		textureData.m_nNodeMask = 0;

		texture.eType = vr::TextureType_DirectX12;
		texture.handle = &textureData;
	}

	vr::EVROverlayError error = vr::VROverlay()->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVR had an error on updating overlay (%d)\n", error);
	}
}


void DashboardMenu::CreateOverlay()
{
	vr::IVROverlay* VROverlay = vr::VROverlay();

	if (VROverlay == nullptr)
	{
		return;
	}

	vr::EVROverlayError error = VROverlay->FindOverlay(DASHBOARD_OVERLAY_KEY, &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None && error != vr::EVROverlayError::VROverlayError_UnknownOverlay)
	{
		Log("Warning: SteamVR FindOverlay error (%d)\n", error);
	}

	if (m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		error = VROverlay->CreateDashboardOverlay(DASHBOARD_OVERLAY_KEY, "OpenXR Passthrough", &m_overlayHandle, &m_thumbnailHandle);
		if (error != vr::EVROverlayError::VROverlayError_None)
		{
			ErrorLog("SteamVR overlay init error (%d)\n", error);
		}
		else
		{
			VROverlay->SetOverlayInputMethod(m_overlayHandle, vr::VROverlayInputMethod_Mouse);
			VROverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_IsPremultiplied, true);
			VROverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

			vr::VROverlay()->SetOverlayWidthInMeters(m_overlayHandle, 2.775f);

			vr::HmdVector2_t ScaleVec;
			ScaleVec.v[0] = OVERLAY_RES_WIDTH;
			ScaleVec.v[1] = OVERLAY_RES_HEIGHT;
			vr::VROverlay()->SetOverlayMouseScale(m_overlayHandle, &ScaleVec);

			CreateThumbnail();
		}
	}
}


void DashboardMenu::DestroyOverlay()
{
	if (vr::VROverlay() == nullptr || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		m_overlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}

	vr::EVROverlayError error = vr::VROverlay()->DestroyOverlay(m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None)
	{
		ErrorLog("SteamVR DestroyOverlay error (%d)\n", error);
	}
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

	vr::VROverlay()->SetOverlayFromFile(m_thumbnailHandle, imgPath.c_str());
}


void DashboardMenu::HandleEvents()
{
	if (vr::VROverlay() == nullptr || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	vr::VREvent_t event;
	while (vr::VROverlay()->PollNextOverlayEvent(m_overlayHandle, &event, sizeof(event)))
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


void DashboardMenu::SetupDX12()
{
	m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_d3d12CommandAllocator));

	m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_d3d12CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_d3d12CommandList));

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = 1;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NodeMask = 0;
	m_d3d12Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_d3d12RTVHeap));

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = OVERLAY_RES_WIDTH;
	textureDesc.Height = OVERLAY_RES_HEIGHT;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	m_d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_SHARED, &textureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_d3d12Texture));

	m_d3d12Device->CreateRenderTargetView(m_d3d12Texture.Get(), NULL, m_d3d12RTVHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
	srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDesc.NumDescriptors = 1;
	srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3d12Device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_d3d12SRVHeap));
}