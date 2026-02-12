
#include "pch.h"
#include "dashboard_overlay.h"
#include "resource.h"

#include <lodepng.h>





DashboardOverlay::DashboardOverlay(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
	: m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();
	vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
}

DashboardOverlay::~DashboardOverlay()
{
}

// WaitFrameSync Does not work under OpenXR applications.
		/*if (vr::EVROverlayError error = vrOverlay->WaitFrameSync(100); error != vr::VROverlayError_None)
		{
			ErrorLog("WaitFrameSync error: %d\n", error);
		}*/

void DashboardOverlay::CreateOverlay()
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
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SortWithNonSceneOverlays, true);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_EnableControlBar, true);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_EnableControlBarKeyboard, true);
			vrOverlay->SetOverlayTextureColorSpace(m_overlayHandle, vr::ColorSpace_Gamma);

			vrOverlay->SetOverlayWidthInMeters(m_overlayHandle, 2.775f);

			vr::HmdVector2_t ScaleVec;
			ScaleVec.v[0] = OVERLAY_RES_WIDTH;
			ScaleVec.v[1] = OVERLAY_RES_HEIGHT;
			vrOverlay->SetOverlayMouseScale(m_overlayHandle, &ScaleVec);

			CreateThumbnail();
		}
	}
}

void DashboardOverlay::DestroyOverlay()
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

void DashboardOverlay::CreateThumbnail()
{
	HRSRC resInfo = FindResource(NULL, MAKEINTRESOURCE(IDB_PNG_DASHBOARD_ICON), L"PNG");
	if (resInfo == nullptr)
	{
		ErrorLog("Error finding icon resource.\n");
		return;
	}
	HGLOBAL memory = LoadResource(NULL, resInfo);
	if (memory == nullptr)
	{
		ErrorLog("Error loading icon resource.\n");
		return;
	}
	size_t data_size = SizeofResource(NULL, resInfo);
	void* data = LockResource(memory);

	if (data == nullptr)
	{
		ErrorLog("Error reading icon resource.\n");
		return;
	}
	std::vector<uint8_t> buffer;

	uint32_t width, height;
	uint32_t error = lodepng::decode(buffer, width, height, (uint8_t*)data, data_size);

	if (error)
	{
		ErrorLog("Error decoding icon.\n");
		return;
	}

	m_openVRManager->GetVROverlay()->SetOverlayRaw(m_thumbnailHandle, &buffer[0], width, height, 4);
}

void DashboardOverlay::HandleOverlayEvents(ImGuiIO& io)
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		return;
	}

	vr::VREvent_t event;
	while (vrOverlay->PollNextOverlayEvent(m_overlayHandle, &event, sizeof(event)))
	{
		vr::VREvent_Overlay_t& overlayData = (vr::VREvent_Overlay_t&)event.data;
		vr::VREvent_Mouse_t& mouseData = (vr::VREvent_Mouse_t&)event.data;
		vr::VREvent_Scroll_t& scrollData = (vr::VREvent_Scroll_t&)event.data;
		vr::VREvent_Keyboard_t& keyboardData = (vr::VREvent_Keyboard_t&)event.data;

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

			m_bOverlayVisible = true;
			break;

		case vr::VREvent_OverlayHidden:

			m_bOverlayVisible = false;
			m_configManager->DispatchUpdate();
			break;

		case vr::VREvent_DashboardActivated:
			//bThumbnailNeedsUpdate = true;
			break;

		case vr::VREvent_KeyboardCharInput:

			if (keyboardData.cNewInput[0] == 0x0A || keyboardData.cNewInput[0] == 0x0D)
			{
				io.AddKeyEvent(ImGuiKey_Enter, true);
				io.AddKeyEvent(ImGuiKey_Enter, false);
			}
			else if (keyboardData.cNewInput[0] == 0x08)
			{
				io.AddKeyEvent(ImGuiKey_Backspace, true);
				io.AddKeyEvent(ImGuiKey_Backspace, false);
			}
			else if (keyboardData.cNewInput[0] == 0x1b && keyboardData.cNewInput[1] == 0x5b && keyboardData.cNewInput[2] == 0x44)
			{
				io.AddKeyEvent(ImGuiKey_LeftArrow, true);
				io.AddKeyEvent(ImGuiKey_LeftArrow, false);
			}
			else if (keyboardData.cNewInput[0] == 0x1b && keyboardData.cNewInput[1] == 0x5b && keyboardData.cNewInput[2] == 0x43)
			{
				io.AddKeyEvent(ImGuiKey_RightArrow, true);
				io.AddKeyEvent(ImGuiKey_RightArrow, false);
			}
			else
			{
				io.AddInputCharactersUTF8(keyboardData.cNewInput);
			}

			break;

		case vr::VREvent_KeyboardDone:

			io.AddKeyEvent(ImGuiKey_Enter, true);
			io.AddKeyEvent(ImGuiKey_Enter, false);
			break;

		case vr::VREvent_KeyboardClosed_Global:

			if (m_bIsKeyboardOpen && keyboardData.overlayHandle == m_overlayHandle)
			{
				io.AddKeyEvent(ImGuiKey_Enter, true);
				io.AddKeyEvent(ImGuiKey_Enter, false);
				m_bIsKeyboardOpen = false;
			}
			break;

		case vr::VREvent_Quit:

			vr::VRSystem()->AcknowledgeQuit_Exiting();
			Log("SteamVR telling application to quit\n");
			break;
		}
	}
}

void DashboardOverlay::UpdateOverlay()
{
	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Linear;
	texture.eType = vr::TextureType_DXGISharedHandle;

	/*vr::EVROverlayError error = m_openVRManager->GetVROverlay()->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVR had an error on updating overlay (%d)\n", error);
	}

	if (!m_bIsKeyboardOpen && io.WantTextInput)
	{
		m_openVRManager->GetVROverlay()->ShowKeyboardForOverlay(m_overlayHandle, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeMultipleLines, vr::KeyboardFlag_Modal | vr::KeyboardFlag_Minimal | vr::KeyboardFlag_ShowArrowKeys, "", 255, "", 0);
		m_bIsKeyboardOpen = true;
	}
	else if (m_bIsKeyboardOpen && !io.WantTextInput)
	{
		m_openVRManager->GetVROverlay()->HideKeyboard();
		m_bIsKeyboardOpen = false;
	}*/
}
