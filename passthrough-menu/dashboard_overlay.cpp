
#include "pch.h"
#include "dashboard_overlay.h"
#include "resource.h"



DashboardOverlay::DashboardOverlay()
	: m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
{
	
}

DashboardOverlay::~DashboardOverlay()
{
	if (m_bRuntimeInitialized)
	{
		m_bRuntimeInitialized = false;
		vr::VR_Shutdown();
	}
}

bool DashboardOverlay::InitRuntime()
{
	std::lock_guard<std::mutex> lock(m_runtimeMutex);

	if (m_bRuntimeInitialized) { return true; }

	if (!vr::VR_IsRuntimeInstalled())
	{
		g_logger->error("SteamVR installation not detected!");
		return false;
	}

	vr::EVRInitError error;

	vr::IVRSystem* system = vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Background);

	if (error == vr::EVRInitError::VRInitError_Init_NoServerForBackgroundApp)
	{
		g_logger->info("SteamVR not running. Not starting overlay.");
		return false;
	}
	else if (error != vr::EVRInitError::VRInitError_None)
	{
		g_logger->error("Failed to initialize SteamVR runtime, error {}", static_cast<int32_t>(error));
		return false;
	}

	vr::VR_Shutdown();

	system = vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Overlay);

	if (error != vr::EVRInitError::VRInitError_None)
	{
		g_logger->error("Failed to initialize SteamVR runtime, error {}", static_cast<int32_t>(error));
		return false;
	}

	m_bRuntimeInitialized = true;
	m_bHasOverlay = false;

	return true;
}

bool DashboardOverlay::CreateOverlay(uint32_t width, uint32_t height)
{
	vr::IVROverlay* vrOverlay = vr::VROverlay();

	if (!vrOverlay)
	{
		return false;
	}

	std::string overlayKey = std::format(DASHBOARD_OVERLAY_KEY, GetCurrentProcessId());

	vr::EVROverlayError error = vrOverlay->FindOverlay(overlayKey.c_str(), &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None && error != vr::EVROverlayError::VROverlayError_UnknownOverlay)
	{
		g_logger->info("Warning: SteamVR FindOverlay error (%d)", static_cast<int32_t>(error));
	}

	if (m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		error = vrOverlay->CreateDashboardOverlay(overlayKey.c_str(), "OpenXR Passthrough", &m_overlayHandle, &m_thumbnailHandle);
		if (error != vr::EVROverlayError::VROverlayError_None)
		{
			g_logger->error("SteamVR overlay init error {}", static_cast<int32_t>(error));
			return false;
		}

		vrOverlay->SetOverlayInputMethod(m_overlayHandle, vr::VROverlayInputMethod_Mouse);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_IsPremultiplied, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SortWithNonSceneOverlays, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_EnableControlBar, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_EnableControlBarKeyboard, true);
		vrOverlay->SetOverlayTextureColorSpace(m_overlayHandle, vr::ColorSpace_Gamma);

		vrOverlay->SetOverlayWidthInMeters(m_overlayHandle, 2.775f);
	}
	m_bHasOverlay = true;
	return true;
}

void DashboardOverlay::DestroyOverlay()
{
	m_bHasOverlay = false;
	m_bHasFocus = false;
	m_bOverlayVisible = false;

	vr::IVROverlay* vrOverlay = vr::VROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		m_overlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}

	vr::EVROverlayError error = vrOverlay->DestroyOverlay(m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None)
	{
		g_logger->error("SteamVR DestroyOverlay error {}", static_cast<int32_t>(error));
	}

	m_overlayHandle = vr::k_ulOverlayHandleInvalid;
}

void DashboardOverlay::SetThumbnail(vr::VRVulkanTextureData_t* textureData)
{
	vr::Texture_t texture;
	texture.handle = textureData;
	texture.eColorSpace = vr::ColorSpace_Auto;
	texture.eType = vr::TextureType_Vulkan;

	vr::EVROverlayError error = vr::VROverlay()->SetOverlayTexture(m_thumbnailHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		g_logger->error("SteamVR had an error on updating overlay thumbnail: {}", static_cast<int32_t>(error));
	}
}

void DashboardOverlay::OverlayFrameSync()
{
	vr::EVROverlayError error = vr::VROverlay()->WaitFrameSync(100);

	if (error != vr::VROverlayError_None)
	{
		g_logger->error("WaitFrameSync error: {}", static_cast<int32_t>(error));
	}
}

void DashboardOverlay::HandleOverlayEvents(ImGuiIO& io)
{
	vr::IVROverlay* vrOverlay = vr::VROverlay();

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

			if (m_overlayHeight > 0)
			{
				io.AddMousePosEvent(mouseData.x, m_overlayHeight - mouseData.y);
			}
			break;

		case vr::VREvent_ScrollDiscrete:
		case vr::VREvent_ScrollSmooth:

			io.AddMouseWheelEvent(scrollData.xdelta, scrollData.ydelta);
			break;

		case vr::VREvent_FocusEnter:

			m_bHasFocus = true;
			break;

		case vr::VREvent_FocusLeave:

			m_bHasFocus = false;
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
			m_bHasFocus = false;
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
			g_logger->info("SteamVR telling application to quit");
			break;
		}
	}
}

void DashboardOverlay::UpdateOverlay(vr::VRVulkanTextureData_t* textureData, ImGuiIO& io)
{
	vr::IVROverlay* vrOverlay = vr::VROverlay();

	if (textureData->m_nWidth != m_overlayWidth || textureData->m_nHeight != m_overlayHeight)
	{
		m_overlayWidth = textureData->m_nWidth;
		m_overlayHeight = textureData->m_nHeight;

		vr::HmdVector2_t ScaleVec;
		ScaleVec.v[0] = (float)m_overlayWidth;
		ScaleVec.v[1] = (float)m_overlayHeight;
		vrOverlay->SetOverlayMouseScale(m_overlayHandle, &ScaleVec);
	}

	vr::Texture_t texture;
	texture.handle = textureData;
	texture.eColorSpace = vr::ColorSpace_Auto;
	texture.eType = vr::TextureType_Vulkan;

	vr::EVROverlayError error = vrOverlay->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		g_logger->error("SteamVR had an error on updating overlay: {}", static_cast<int32_t>(error));
	}

	if (!m_bIsKeyboardOpen && io.WantTextInput)
	{
		vrOverlay->ShowKeyboardForOverlay(m_overlayHandle, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeMultipleLines, vr::KeyboardFlag_Modal | vr::KeyboardFlag_Minimal | vr::KeyboardFlag_ShowArrowKeys, "", 255, "", 0);
		m_bIsKeyboardOpen = true;
	}
	else if (m_bIsKeyboardOpen && !io.WantTextInput)
	{
		vrOverlay->HideKeyboard();
		m_bIsKeyboardOpen = false;
	}
}

void DashboardOverlay::GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties)
{
	properties.clear();

	vr::IVRSystem* vrSystem = vr::VRSystem();
	vr::IVRTrackedCamera* trackedCamera = vr::VRTrackedCamera();

	char stringPropBuffer[256];

	for (uint32_t deviceId = 0; deviceId < vr::k_unMaxTrackedDeviceCount; deviceId++)
	{
		if (vrSystem->GetTrackedDeviceClass(deviceId) == vr::TrackedDeviceClass_Invalid)
		{
			continue;
		}

		properties.push_back(DeviceDebugProperties());
		DeviceDebugProperties& deviceProps = properties.at(properties.size() - 1);

		deviceProps.DeviceClass = vrSystem->GetTrackedDeviceClass(deviceId);
		deviceProps.DeviceId = deviceId;


		deviceProps.bHasCamera = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_HasCamera_Bool);
		deviceProps.NumCameras = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_NumCameras_Int32);

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		uint32_t numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ManufacturerName_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.DeviceName.assign(stringPropBuffer);

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ModelNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.DeviceName.append(" ");
		deviceProps.DeviceName.append(stringPropBuffer);

		vr::HmdMatrix34_t cameraToHeadmatrices[4];
		uint32_t numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &cameraToHeadmatrices, sizeof(cameraToHeadmatrices));

		int32_t cameraDistortionFunctions[4];
		numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraDistortionFunction_Int32_Array, vr::k_unInt32PropertyTag, &cameraDistortionFunctions, sizeof(cameraDistortionFunctions));

		double cameraDistortionCoeffs[4][vr::k_unMaxDistortionFunctionParameters];
		numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraDistortionCoefficients_Float_Array, vr::k_unFloatPropertyTag, &cameraDistortionCoeffs, sizeof(cameraDistortionCoeffs));

		vr::HmdVector4_t whiteBalance[4];
		numBytes = vrSystem->GetArrayTrackedDeviceProperty(deviceId, vr::Prop_CameraWhiteBalance_Vector4_Array, vr::k_unHmdVector4PropertyTag, &whiteBalance, sizeof(whiteBalance));

		uint32_t fbSize;

		trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_Distorted, &deviceProps.DistortedFrameWidth, &deviceProps.DistortedFrameHeight, &fbSize);
		trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_Undistorted, &deviceProps.UndistortedFrameWidth, &deviceProps.UndistortedFrameHeight, &fbSize);
		trackedCamera->GetCameraFrameSize(deviceId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &deviceProps.MaximumUndistortedFrameWidth, &deviceProps.MaximumUndistortedFrameHeight, &fbSize);

		for (uint32_t cameraId = 0; cameraId < deviceProps.NumCameras; cameraId++)
		{
			CameraDebugProperties& cameraProps = deviceProps.CameraProps[cameraId];

			trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_Distorted, &cameraProps.DistortedFocalLength, &cameraProps.DistortedOpticalCenter);
			trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_Undistorted, &cameraProps.UndistortedFocalLength, &cameraProps.UndistortedOpticalCenter);
			trackedCamera->GetCameraIntrinsics(deviceId, cameraId, vr::VRTrackedCameraFrameType_MaximumUndistorted, &cameraProps.MaximumUndistortedFocalLength, &cameraProps.MaximumUndistortedOpticalCenter);

			trackedCamera->GetCameraProjection(deviceId, cameraId, vr::VRTrackedCameraFrameType_Undistorted, 0.1f, 1.0f, &cameraProps.UndistortedProjecton);
			trackedCamera->GetCameraProjection(deviceId, cameraId, vr::VRTrackedCameraFrameType_MaximumUndistorted, 0.1f, 1.0f, &cameraProps.MaximumUndistortedProjecton);

			memcpy(&cameraProps.CameraToHeadTransform, &cameraToHeadmatrices[cameraId], sizeof(vr::HmdMatrix34_t));
			cameraProps.DistortionFunction = (vr::EVRDistortionFunctionType)cameraDistortionFunctions[cameraId];

			for (uint32_t coeff = 0; coeff < vr::k_unMaxDistortionFunctionParameters; coeff++)
			{
				cameraProps.DistortionCoefficients[coeff] = cameraDistortionCoeffs[cameraId][coeff];
			}
			cameraProps.WhiteBalance = whiteBalance[cameraId];
		}

		deviceProps.CameraFrameLayout = (vr::EVRTrackedCameraFrameLayout)vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraFrameLayout_Int32);
		deviceProps.CameraStreamFormat = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraStreamFormat_Int32);
		deviceProps.CameraToHeadTransform = vrSystem->GetMatrix34TrackedDeviceProperty(deviceId, vr::Prop_CameraToHeadTransform_Matrix34);
		deviceProps.CameraFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_CameraFirmwareVersion_Uint64);

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_CameraFirmwareDescription_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.CameraFirmwareDescription.assign(stringPropBuffer);

		deviceProps.CameraCompatibilityMode = vrSystem->GetInt32TrackedDeviceProperty(deviceId, vr::Prop_CameraCompatibilityMode_Int32);
		deviceProps.bCameraSupportsCompatibilityModes = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_CameraSupportsCompatibilityModes_Bool);
		deviceProps.CameraExposureTime = vrSystem->GetFloatTrackedDeviceProperty(deviceId, vr::Prop_CameraExposureTime_Float);
		deviceProps.CameraGlobalGain = vrSystem->GetFloatTrackedDeviceProperty(deviceId, vr::Prop_CameraGlobalGain_Float);

		deviceProps.HMDFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_FirmwareVersion_Uint64);
		deviceProps.FPGAFirmwareVersion = vrSystem->GetUint64TrackedDeviceProperty(deviceId, vr::Prop_FPGAVersion_Uint64);
		deviceProps.bHMDSupportsRoomViewDirect = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_Hmd_SupportsRoomViewDirect_Bool);
		deviceProps.bSupportsRoomViewDepthProjection = vrSystem->GetBoolTrackedDeviceProperty(deviceId, vr::Prop_SupportsRoomViewDepthProjection_Bool);
		deviceProps.bAllowCameraToggle = vrSystem->GetBoolTrackedDeviceProperty(deviceId, (vr::ETrackedDeviceProperty)1055); // vr::Prop_AllowCameraToggle_Bool
		deviceProps.bAllowLightSourceFrequency = vrSystem->GetBoolTrackedDeviceProperty(deviceId, (vr::ETrackedDeviceProperty)1056); // vr::Prop_AllowLightSourceFrequency_Bool

	}
}

void DashboardOverlay::GetDeviceIdentProperties(std::vector<DeviceIdentProperties>& properties)
{
	properties.clear();

	vr::IVRSystem* vrSystem = vr::VRSystem();

	char stringPropBuffer[256];

	for (uint32_t deviceId = 0; deviceId < vr::k_unMaxTrackedDeviceCount; deviceId++)
	{
		if (vrSystem->GetTrackedDeviceClass(deviceId) == vr::TrackedDeviceClass_Invalid)
		{
			continue;
		}

		properties.push_back(DeviceIdentProperties());
		DeviceIdentProperties& deviceProps = properties.at(properties.size() - 1);

		deviceProps.DeviceId = deviceId;

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		uint32_t numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ManufacturerName_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.DeviceName.assign(stringPropBuffer);

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_ModelNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.DeviceName.append(" ");
		deviceProps.DeviceName.append(stringPropBuffer);

		memset(stringPropBuffer, 0, sizeof(stringPropBuffer));
		numChars = vrSystem->GetStringTrackedDeviceProperty(deviceId, vr::Prop_SerialNumber_String, stringPropBuffer, sizeof(stringPropBuffer));
		deviceProps.DeviceSerial.assign(stringPropBuffer);
	}
}
