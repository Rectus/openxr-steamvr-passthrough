#include "pch.h"
#include "settings_menu.h"
#include "desktop_window_win32.h"
#include "dashboard_overlay.h"
#include "version.h"

#include "imgui.h"
#include "imgui_internal.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include "imgui_impl_vulkan.h"
#include "backends/imgui_impl_win32.h"

#include <vulkan/vulkan_beta.h>
#include "lodepng.h"
#include "resource.h"
#include "camera_enumerator.h"
#include "mathutil.h"

#include "fonts/roboto_medium.cpp"
#include "fonts/cousine_regular.cpp"


#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))



SettingsMenu::SettingsMenu(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<DesktopWindowWin32> window, std::shared_ptr<MenuIPCServer> IPCServer)
	: m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_window(window)
	, m_IPCServer(IPCServer)
	, m_bMenuIsVisible(false)
	, m_displayValues()
	, m_activeTab(TabMain)
{
	m_renderer = VulkanMenuRenderer();
	m_defualtDisplayValues.currentApplication = "No application";
}


SettingsMenu::~SettingsMenu()
{
}


bool SettingsMenu::InitMenu()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(OVERLAY_RES_WIDTH, OVERLAY_RES_HEIGHT);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	m_mainFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 24);
	m_smallFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 22);
	m_fixedFont = io.Fonts->AddFontFromMemoryCompressedTTF(cousine_regular_compressed_data, cousine_regular_compressed_size, 18);
	ImGui::StyleColorsDark();

	// TODO: Using single clicks since double clicks don't seem to be working (timing issue?)
	//io.ConfigDragClickToInputText = true;

	m_renderer.SetupRenderer(m_window);
	ImGui_ImplWin32_Init(m_window->GetWindowHandle());
	m_renderer.InitImGui();

	//CreateOverlay();

	// Hack to fix Dear ImGui not rendering (mostly) correctly to sRGB target
	// From: https://github.com/ocornut/imgui/issues/8271#issuecomment-2564954070
	//ImGuiStyle& style = ImGui::GetStyle();
	//for (int i = 0; i < ImGuiCol_COUNT; i++)
	//{
	//	ImVec4& col = style.Colors[i];

	//	// Multiply out the alpha factor, and add it back afterwards
	//	col.x *= col.w;
	//	col.y *= col.w;
	//	col.z *= col.w;

	//	col.x = (col.x <= 0.04045f ? col.x / 12.92f : pow((col.x + 0.055f) / 1.055f, 2.4f)) / max(col.w, 0.01f);
	//	col.y = (col.y <= 0.04045f ? col.y / 12.92f : pow((col.y + 0.055f) / 1.055f, 2.4f)) / max(col.w, 0.01f);
	//	col.z = (col.z <= 0.04045f ? col.z / 12.92f : pow((col.z + 0.055f) / 1.055f, 2.4f)) / max(col.w, 0.01f);
	//}

	return true;
}

void SettingsMenu::DeinitMenu()
{
	m_renderer.WaitDeinitImGui();

	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	m_renderer.CleanupRenderer();

	/*if (vrOverlay)
	{
		if (m_bIsKeyboardOpen)
		{
			vrOverlay->HideKeyboard();
		}
		vrOverlay->DestroyOverlay(m_overlayHandle);
	}*/
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

	case OpenGL:

		switch (format)
		{
		case 0x8C41:
			return "GL_SRGB8";
		case 0x8C43:
			return "GL_SRGB8_ALPHA8";
		case 0x8058:
			return "GL_RGBA8";
		case 0x8F97:
			return "GL_RGBA8_SNORM";
		case 0x8814:
			return "GL_RGBA32F";
		case 0x8815:
			return "GL_RGB32F";
		case 0x881A:
			return "GL_RGBA16F";
		case 0x8059:
			return "GL_RGB10_A2";
		case 0x8CAC:
			return "GL_DEPTH_COMPONENT32F";
		case 0x81A7:
			return "GL_DEPTH_COMPONENT32";
		case 0x81A6:
			return "GL_DEPTH_COMPONENT24";
		case 0x81A5:
			return "GL_DEPTH_COMPONENT16";
		case 0x88F0:
			return "GL_DEPTH24_STENCIL8";
		case 0x8CAD:
			return "GL_DEPTH32F_STENCIL8";
		case 0x8DAC:
			return "GL_DEPTH32F_STENCIL8_NV";

		default:
			return "Unknown format";
		}
		
	default:
		return "Unknown format";
	}
}


inline void ScrollableSlider(const char* label, float* v, float v_min, float v_max, const char* format, float scrollFactor)
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


inline void ScrollableSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, int scrollFactor)
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

#define IMGUI_BIG_SPACING ImGui::Dummy(ImVec2(0.0f, 20.0f))

inline void SettingsMenu::TextDescription(const char* fmt, ...)
{
	if (m_configManager->GetConfig_Main().ShowSettingDescriptions)
	{
		ImGui::Indent();
		ImGui::PushFont(m_smallFont);
		ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
		va_list args;
		va_start(args, fmt);
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), fmt, args);
		va_end(args);
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		ImGui::Unindent();
	}
}

inline void SettingsMenu::TextDescriptionSpaced(const char* fmt, ...)
{
	if (m_configManager->GetConfig_Main().ShowSettingDescriptions)
	{
		ImGui::Indent();
		ImGui::PushFont(m_smallFont);
		ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
		va_list args;
		va_start(args, fmt);
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), fmt, args);
		va_end(args);
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		ImGui::Unindent();
		IMGUI_BIG_SPACING;
	}
}

inline void BeginSoftDisabled(bool bIsDisabled)
{
	if (bIsDisabled)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().DisabledAlpha);
	}
}

inline void EndSoftDisabled(bool bIsDisabled)
{
	if (bIsDisabled)
	{
		ImGui::PopStyleVar();
	}
}


void SettingsMenu::TickMenu()
{
	//HandleEvents();


	if (!m_window->IsVisible())
	{
		return;
	}

	DrawMenu();

	m_renderer.RenderMenu();
}

void SettingsMenu::DrawMenu()
{
	std::lock_guard<std::mutex> lock(m_menuWriteMutex);

	bool rendererResetPending = false;
	bool cameraParamChangesPending = false;
	bool frameDumpPending = false;

	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();
	Config_Extensions& extConfig = m_configManager->GetConfig_Extensions();
	Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();
	Config_Stereo& stereoCustomConfig = m_configManager->GetConfig_CustomStereo();
	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	Config_Camera& cameraConfig = m_configManager->GetConfig_Camera();

	bool hasClient = m_activeClient >= 0 && m_activeClient < m_displayValues.size();
	MenuDisplayValues& displayValues = hasClient ? *m_displayValues[m_activeClient] : m_defualtDisplayValues;

	ImVec4 colorTextGreen(0.2f, 0.8f, 0.2f, 1.0f);
	ImVec4 colorTextRed(0.8f, 0.2f, 0.2f, 1.0f);
	ImVec4 colorTextOrange(0.85f, 0.7f, 0.2f, 1.0f);

	ImGuiIO& io = ImGui::GetIO();

	LARGE_INTEGER frameStart, perfFrequency;
	QueryPerformanceCounter(&frameStart);
	QueryPerformanceFrequency(&perfFrequency);

	io.DeltaTime = ((float)(frameStart.QuadPart - m_lastFrameStart.QuadPart)) / perfFrequency.QuadPart;
	m_lastFrameStart = frameStart;

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 20);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 20);
	ImGui::PushFont(m_mainFont);

	ImGui::Begin("OpenXR Passthrough", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

	

	ImGui::BeginChild("Tab buttons", ImVec2(OVERLAY_RES_WIDTH * 0.18f, 0));

	ImVec2 tabButtonSize(OVERLAY_RES_WIDTH * 0.17f, 55);
	ImVec4 colorActiveTab(0.25f, 0.52f, 0.88f, 1.0f);
	bool bIsActiveTab = false;

#define TAB_BUTTON(name, tab) if (m_activeTab == tab) { ImGui::PushStyleColor(ImGuiCol_Button, colorActiveTab); bIsActiveTab = true; } \
if (ImGui::Button(name, tabButtonSize)) { m_activeTab = tab; } \
if (bIsActiveTab) { ImGui::PopStyleColor(1); bIsActiveTab = false; }

	TAB_BUTTON("Main", TabMain);
	TAB_BUTTON("Application", TabApplication);
	TAB_BUTTON("Stereo", TabStereo);
	TAB_BUTTON("Overrides", TabOverrides);
	TAB_BUTTON("Camera", TabCamera);
	TAB_BUTTON("Debug", TabDebug);


	ImGui::BeginChild("Sep1", ImVec2(0, 20));
	ImGui::EndChild();

	ImGui::PushFont(m_smallFont);
	ImGui::Indent();
	ImGui::Text("Application:");
	ImGui::Text("%s", displayValues.currentApplication.c_str());

	ImGui::Separator();
	ImGui::Text("Session:");
	displayValues.bSessionActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");

	bool bPassthoughActive = (displayValues.bCorePassthroughActive || displayValues.bFBPassthroughActive);

	ImGui::Separator();
	ImGui::Text("Passthrough:");
	bPassthoughActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");

	if (mainConfig.EnablePassthrough && !bPassthoughActive && displayValues.CoreCurrentMode == 1)
	{
		ImGui::TextColored(colorTextRed, "Opaque");
	}

	if (mainConfig.EnablePassthrough && !bPassthoughActive && displayValues.CoreCurrentMode == 3 && !(displayValues.frameBufferFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT))
	{
		ImGui::TextColored(colorTextRed, "No Alpha");
	}

	if (bPassthoughActive && displayValues.bDepthBlendingActive)
	{
		ImGui::TextColored(colorTextGreen, "Depth Blending");
	}

	if (coreConfig.CoreForcePassthrough || depthConfig.DepthForceComposition || depthConfig.DepthForceRangeTest)
	{
		ImGui::Separator();
		ImGui::Text("Override:");
		if (depthConfig.DepthForceComposition) { ImGui::TextColored(colorTextOrange, "Depth Composition"); }
		if (depthConfig.DepthForceRangeTest) { ImGui::TextColored(colorTextOrange, "Depth Range"); }
		if (coreConfig.CoreForcePassthrough) { ImGui::TextColored(colorTextOrange, "Passthrough Mode"); }	
		
	}

	ImGui::Unindent();
	ImGui::PopFont();

	ImGui::BeginChild("Sep2", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 30));
	ImGui::EndChild();
	if (ImGui::Button("Reset To Defaults", tabButtonSize))
	{
		EProjectionMode mode = mainConfig.ProjectionMode;
		ECameraProvider cam = mainConfig.CameraProvider;

		m_configManager->ResetToDefaults();

		if (mainConfig.ProjectionMode != mode || mainConfig.CameraProvider != cam)
		{
			rendererResetPending = true;
		}
	}

	ImGui::EndChild();
	ImGui::SameLine();




	if (m_activeTab == TabMain)
	{
		ImGui::BeginChild("Main#TabMain");


		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Main Settings"))
		{
			ImGui::Checkbox("Enable Passthrough", &mainConfig.EnablePassthrough);

			IMGUI_BIG_SPACING;

			if (mainConfig.CameraProvider != CameraProvider_OpenVR) { ImGui::BeginDisabled(); }

			ImGui::Text("Projection Mode");
			TextDescription("Method for projecting the passthrough cameras to the VR view.");
			if (ImGui::RadioButton("2D Room View", mainConfig.ProjectionMode == Projection_RoomView2D))
			{
				mainConfig.ProjectionMode = Projection_RoomView2D;
			}
			TextDescription("Cylindrical projection with floor. Matches the projection in the SteamVR Room View 2D mode.");

			if (mainConfig.CameraProvider != CameraProvider_OpenVR) { ImGui::EndDisabled(); }

			if (mainConfig.CameraProvider == CameraProvider_Augmented) { ImGui::BeginDisabled(); }

			if (ImGui::RadioButton("2D Custom", mainConfig.ProjectionMode == Projection_Custom2D))
			{
				mainConfig.ProjectionMode = Projection_Custom2D;
			}
			TextDescription("Cylindrical projection with floor. Custom distortion correction and projection calculation.");

			if (mainConfig.CameraProvider == CameraProvider_Augmented) { ImGui::EndDisabled(); }

			if (mainConfig.CameraProvider == CameraProvider_OpenCV && cameraConfig.CameraFrameLayout == Mono) { ImGui::BeginDisabled(); }

			if (ImGui::RadioButton("3D Stereo", mainConfig.ProjectionMode == Projection_StereoReconstruction))
			{
				mainConfig.ProjectionMode = Projection_StereoReconstruction;
			}
			TextDescriptionSpaced("Full depth estimation.");

			if (mainConfig.CameraProvider == CameraProvider_OpenCV && cameraConfig.CameraFrameLayout == Mono) { ImGui::EndDisabled(); }

			ImGui::Checkbox("Project onto Render Models", &mainConfig.ProjectToRenderModels);
			TextDescriptionSpaced("Project the passthrough view to the correct distance on render models, such as controllers. Requires good camera calibration.");

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Image Controls"))
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSlider("Opacity", &mainConfig.PassthroughOpacity, 0.0f, 1.0f, "%.1f", 0.1f);
				ScrollableSlider("Brightness", &mainConfig.Brightness, -50.0f, 50.0f, "%.0f", 1.0f);
				ScrollableSlider("Contrast", &mainConfig.Contrast, 0.0f, 2.0f, "%.1f", 0.1f);
				ScrollableSlider("Saturation", &mainConfig.Saturation, 0.0f, 2.0f, "%.1f", 0.1f);
				ScrollableSlider("Sharpness", &mainConfig.Sharpness, -1.0f, 1.0f, "%.1f", 0.1f);
				if (fabsf(mainConfig.Sharpness) < 0.1) { mainConfig.Sharpness = 0.0f; }
				ImGui::PopItemWidth();

				IMGUI_BIG_SPACING;

				ImGui::Checkbox("Enable Temporal Filter", &mainConfig.EnableTemporalFiltering);
				TextDescriptionSpaced("Improves image quality by removing noise and flickering, and sharpening it. Possibly slightly increases image resolution. Expensive on the GPU.");

				if (ImGui::CollapsingHeader("Advanced"))
				{
					ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
					ScrollableSlider("Temporal Filter Factor", &mainConfig.TemporalFilteringFactor, 0.0f, 1.0f, "%.2f", 0.01f);
					ScrollableSlider("Temporal Filter Rejection Offset", &mainConfig.TemporalFilteringRejectionOffset, -0.2f, 0.2f, "%.2f", 0.01f);
					ImGui::PopItemWidth();

					ImGui::Text("Temporal Filter Sampling");
					if (ImGui::RadioButton("Nearest", mainConfig.TemporalFilteringSampling == 0))
					{
						mainConfig.TemporalFilteringSampling = 0;
					}
					if (ImGui::RadioButton("Bilinear", mainConfig.TemporalFilteringSampling == 1))
					{
						mainConfig.TemporalFilteringSampling = 1;
					}
					if (ImGui::RadioButton("Bicubic 4 samples", mainConfig.TemporalFilteringSampling == 2))
					{
						mainConfig.TemporalFilteringSampling = 2;
					}
					if (ImGui::RadioButton("Catmull-Rom 9 samples", mainConfig.TemporalFilteringSampling == 3))
					{
						mainConfig.TemporalFilteringSampling = 3;
					}
					if (ImGui::RadioButton("Lanczos 25 samples", mainConfig.TemporalFilteringSampling == 4))
					{
						mainConfig.TemporalFilteringSampling = 4;
					}
				}
				IMGUI_BIG_SPACING;

				ImGui::TreePop();
			}
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Projection Settings"))
		{
			IMGUI_BIG_SPACING;

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSlider("Depth Offset Calibration", &mainConfig.DepthOffsetCalibration, 0.5f, 1.5f, "%.2f", 0.01f);
			TextDescriptionSpaced("Calibration to compensate for incorrect distance between stereo cameras.");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSlider("Projection Distance (m)", &mainConfig.ProjectionDistanceFar, 0.5f, 20.0f, "%.1f", 0.1f);
			TextDescriptionSpaced("The horizontal projection distance in 2D modes, and maximum projection distance in the 3D mode.");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSlider("Floor Height Offset (m)", &mainConfig.FloorHeightOffset, 0.0f, 2.0f, "%.2f", 0.01f);
			TextDescriptionSpaced("Allows setting the floor height higher in the 2D modes,\nfor example to have correct projection on a table surface.");

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Misc."))
		{
			ImGui::Checkbox("Show Descriptions", &mainConfig.ShowSettingDescriptions);

			ImGui::Checkbox("Pause Passthrough When Idle", &mainConfig.PauseImageHandlingOnIdle);
			TextDescription("Stops the camera passthrough stream from being processed when no passthrough is being rendered.");

			BeginSoftDisabled(!mainConfig.PauseImageHandlingOnIdle);
			ImGui::Checkbox("Close Camera Stream On Pause", &mainConfig.CloseCameraStreamOnPause);
			TextDescription("Closes the camera provider when idle. It may take several seconds to start again.");
			ScrollableSlider("Idle Time (s)", &mainConfig.IdleTimeSeconds, 1.0f, 30.0f, "%.0f", 1.0f);
			TextDescription("How long to wait before stopping the processing when idle.");
			EndSoftDisabled(!mainConfig.PauseImageHandlingOnIdle);

			ImGui::Checkbox("Use legacy DirectX 12 renderer", &mainConfig.UseLegacyD3D12Renderer);
			TextDescription("Uses the old native DirectX 12 renderer for DirectX 12 applications. Not recommended since it is missing rendering features. Requires restart.");
			ImGui::Checkbox("Use legacy Vulkan renderer", &mainConfig.UseLegacyVulkanRenderer);
			TextDescription("Uses the old native Vulkan renderer for Vulkan applications. Not recommended since it is missing rendering features. Requires restart.");
		}
		IMGUI_BIG_SPACING;

		ImGui::EndChild();
	}



	if(m_activeTab == TabApplication)
	{
		ImGui::BeginChild("Setup#Tabsetup");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("OpenXR Core"))
		{
			TextDescriptionSpaced("Options for application controlled passthrough features built into the OpenXR core specification. Allows using the environment blend modes for passthrough.");

			ImGui::PushFont(m_fixedFont);
			ImGui::Text("Core passthrough:");
			ImGui::SameLine();
			if (displayValues.bCorePassthroughActive)
			{
				ImGui::TextColored(colorTextGreen, "Active");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Inactive");
			}

			ImGui::Text("Application requested mode:");
			ImGui::SameLine();
			if (displayValues.CoreCurrentMode == 3) { ImGui::Text("Alpha Blend"); }
			else if (displayValues.CoreCurrentMode == 2) { ImGui::Text("Additive"); }
			else if (displayValues.CoreCurrentMode == 1) { ImGui::Text("Opaque"); }
			else { ImGui::Text("Unknown"); }

			if(mainConfig.EnablePassthrough && !displayValues.bCorePassthroughActive && displayValues.CoreCurrentMode == 3 && !(displayValues.frameBufferFlags& XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT))
			{ ImGui::TextColored(colorTextRed, "No alpha channel provided!"); }

			ImGui::PopFont();

			IMGUI_BIG_SPACING;

			ImGui::Checkbox("Enable###CoreEnable", &coreConfig.CorePassthroughEnable);
			TextDescriptionSpaced("Allow OpenXR applications to enable passthrough.");
			IMGUI_BIG_SPACING;

			BeginSoftDisabled(!coreConfig.CorePassthroughEnable);
			ImGui::BeginGroup();
			ImGui::Text("Blend Modes");
			TextDescription("Controls what blend modes are presented to the application. Requires a restart to apply.");
			ImGui::Checkbox("Alpha Blend###CoreAlpha", &coreConfig.CoreAlphaBlend);
			ImGui::Checkbox("Additive###CoreAdditive", &coreConfig.CoreAdditive);
			ImGui::EndGroup();

			IMGUI_BIG_SPACING;

			ImGui::BeginGroup();
			ImGui::Text("Preferred Mode");
			TextDescription("Sets which blend mode the application should prefer. Most game engines will always use the preferred mode by default, even if the application does not support passthrough. Requires a restart to apply.");
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
			EndSoftDisabled(!coreConfig.CorePassthroughEnable);

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("OpenXR Extensions"))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Multivendor"))
			{
				ImGui::PushFont(m_fixedFont);
				ImGui::Text("Composition Layer Inverted Alpha extension:");
				ImGui::SameLine();
				if (displayValues.bExtInvertedAlphaActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}
				ImGui::PopFont();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Android"))
			{
				ImGui::PushFont(m_fixedFont);
				ImGui::Text("Android Passthrough Camera State extension:");
				ImGui::SameLine();
				if (displayValues.bAndroidPassthroughStateActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}
				ImGui::PopFont();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Facebook"))
			{
				ImGui::PushFont(m_fixedFont);
				ImGui::Text("Facebook Passthrough extension:");
				ImGui::SameLine();
				if (displayValues.bFBPassthroughExtensionActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}

				ImGui::Text("Facebook Passthrough:");
				ImGui::SameLine();
				if (displayValues.bFBPassthroughActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}

				ImGui::Text("Facebook Passthrough Depth:");
				ImGui::SameLine();
				if (displayValues.bFBPassthroughDepthActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}
				ImGui::PopFont();

				ImGui::Checkbox("Enable Facebook Passthrough", &extConfig.ExtFBPassthrough);
				TextDescription("Allow applications to use passthrough through the XR_FB_passthrough extension. Requires a restart to apply.");

				BeginSoftDisabled(!extConfig.ExtFBPassthrough);
				ImGui::Checkbox("Allow Facebook Passthrough Depth Composition", &extConfig.ExtFBPassthroughAllowDepth);
				TextDescription("Allow applications to composite passthough using depth testing. Requires a restart to apply.");
				ImGui::Checkbox("Allow Facebook Passthrough Color Settings", &extConfig.ExtFBPassthroughAllowColorSettings);
				TextDescription("Allow applications to modify Brightness, Contrast and Saturation. Overrides the layer color settings when activated by application.");
				ImGui::Checkbox("Fake Unsupported Extension Features", &extConfig.ExtFBPassthroughFakeUnsupportedFeatures);
				TextDescription("Pretends that passthrough features unsupported by the layer are working, by reporting success to the application when it tries using them.");
				EndSoftDisabled(!extConfig.ExtFBPassthrough);

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Varjo"))
			{
				ImGui::PushFont(m_fixedFont);
				ImGui::Text("Varjo Depth Estimation extension:");
				ImGui::SameLine();
				if (displayValues.bVarjoDepthEstimationExtensionActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}

				ImGui::Text("Varjo Depth Composition extension:");
				ImGui::SameLine();
				if (displayValues.bVarjoDepthCompositionExtensionActive)
				{
					ImGui::TextColored(colorTextGreen, "Active");
				}
				else
				{
					ImGui::TextColored(colorTextRed, "Inactive");
				}
				ImGui::PopFont();

				ImGui::Checkbox("Enable Varjo Depth estimation", &extConfig.ExtVarjoDepthEstimation);
				TextDescription("Allow applications to use depth blending using the XR_VARJO_environment_depth_estimation extension. Requires a restart to apply.");

				ImGui::Checkbox("Enable Varjo Composition layer depth testing", &extConfig.ExtVarjoDepthComposition);
				TextDescription("Allow applications to compose submitted layers based on depth using the XR_VARJO_composition_layer_depth_test extension. Requires a restart to apply.");

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();
	}



	if (m_activeTab == TabStereo)
	{
		ImGui::BeginChild("Stereo#TabStereo");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status"))
		{
			ImGui::PushFont(m_fixedFont);

			if (displayValues.renderAPI == Vulkan)
			{
				ImGui::TextColored(colorTextRed, "Stereo reconstruction not supported under Vulkan!");
			}
			else if (mainConfig.ProjectionMode == Projection_StereoReconstruction)
			{
				ImGui::TextColored(colorTextGreen, "Stereo reconstruction enabled");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Stereo reconstruction disabled");
			}			
			ImGui::Text("Exposure to render latency: %.1fms", displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", displayValues.stereoReconstructionTimeMS);
			ImGui::PopFont();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Stereo Presets"))
		{
			ImGui::BeginGroup();
			if (ImGui::RadioButton("Very Low", mainConfig.StereoPreset == StereoPreset_VeryLow))
			{
				mainConfig.StereoPreset = StereoPreset_VeryLow;
				rendererResetPending = true;
			}

			if (ImGui::RadioButton("Low", mainConfig.StereoPreset == StereoPreset_Low))
			{
				mainConfig.StereoPreset = StereoPreset_Low;
				rendererResetPending = true;
			}

			if (ImGui::RadioButton("Medium", mainConfig.StereoPreset == StereoPreset_Medium))
			{
				mainConfig.StereoPreset = StereoPreset_Medium;
				rendererResetPending = true;
			}

			if (ImGui::RadioButton("High", mainConfig.StereoPreset == StereoPreset_High))
			{
				mainConfig.StereoPreset = StereoPreset_High;
				rendererResetPending = true;
			}

			if (ImGui::RadioButton("Very High", mainConfig.StereoPreset == StereoPreset_VeryHigh))
			{
				mainConfig.StereoPreset = StereoPreset_VeryHigh;
				rendererResetPending = true;
			}

			if (ImGui::RadioButton("Custom", mainConfig.StereoPreset == StereoPreset_Custom))
			{
				mainConfig.StereoPreset = StereoPreset_Custom;
				rendererResetPending = true;
			}
			ImGui::EndGroup();

			IMGUI_BIG_SPACING;
		}

		BeginSoftDisabled(mainConfig.StereoPreset != StereoPreset_Custom);

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Main Settings"))
		{
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSliderInt("Image Downscale Factor", &stereoCustomConfig.StereoDownscaleFactor, 1, 16, "%d", 1);
			TextDescriptionSpaced("Ratio of the stereo processed image to the camera frame. Larger values will improve performance.");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSliderInt("Disparity Smoothing", &stereoCustomConfig.StereoDisparityFilterWidth, 0, 20, "%d", 1);
			TextDescriptionSpaced("Applies smoothing to areas with low projection confidence.");

			ImGui::Checkbox("Calculate Disparity for Both Cameras", &stereoCustomConfig.StereoDisparityBothEyes);
			TextDescriptionSpaced("Calculates a separate disparity map for each camera, instead of using the left one for both.");

			ImGui::Checkbox("Composite Both Cameras for Each Eye", &stereoCustomConfig.StereoCutoutEnabled);
			TextDescriptionSpaced("Detects areas occluded to the main camera and renders them with the other camera where possible.\nCan also combine cameas to improve resolution.");

			ImGui::Checkbox("Use Projection Temporal Filtering", &stereoCustomConfig.StereoUseDisparityTemporalFiltering);
			TextDescription("Smoothes out and improves quality of the projection depth.");

			IMGUI_BIG_SPACING;

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Performance"))
			{
				ImGui::Checkbox("Use Multiple Cores", &stereoCustomConfig.StereoUseMulticore);
				TextDescriptionSpaced("Allows the stereo calculations to use multiple CPU cores. This can be turned off for CPU limited applications.");

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSliderInt("Frame Skip Ratio", &stereoCustomConfig.StereoFrameSkip, 0, 14, "%d", 1);
				TextDescription("Skip stereo processing of this many frames for each frame processed. This does not affect the frame rate of viewed camera frames, every frame will still be reprojected on the latest stereo data.");

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			IMGUI_BIG_SPACING;
		}


		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Advanced GPU Settings"))
		{

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Projection"))
			{
				ImGui::Spacing();
				ImGui::BeginGroup();
				ImGui::Text("Rendering Path");
				if (ImGui::RadioButton("Direct Rendering (Legacy)", !stereoCustomConfig.StereoUseSeparateDepthPass))
				{
					stereoCustomConfig.StereoUseSeparateDepthPass = false;
					stereoCustomConfig.StereoUseFullscreenPass = false;
				}
				TextDescriptionSpaced("Legacy mode that reprojects from disparity and draws passthrough in one call.\nPoor support for temporal filtering and camera composition.");
				if (ImGui::RadioButton("Separate Depth Pass (Legacy)", stereoCustomConfig.StereoUseSeparateDepthPass && !stereoCustomConfig.StereoUseFullscreenPass))
				{
					stereoCustomConfig.StereoUseSeparateDepthPass = true;
					stereoCustomConfig.StereoUseFullscreenPass = false;
				}
				TextDescriptionSpaced("Uses a separate render pass for drawing depth maps.\nBetter support for camera composition, but still uses original vertex grid shaders.");
				if (ImGui::RadioButton("Separate Depth Pass Fullscreen", stereoCustomConfig.StereoUseSeparateDepthPass && stereoCustomConfig.StereoUseFullscreenPass))
				{
					stereoCustomConfig.StereoUseSeparateDepthPass = true;
					stereoCustomConfig.StereoUseFullscreenPass = true;
				}
				TextDescription("More effective version of the above renderer that proccesses the depth maps directly in the pixel shader.");
				ImGui::EndGroup();

				IMGUI_BIG_SPACING;

				ImGui::Checkbox("Use Hexagon Grid Mesh", &stereoCustomConfig.StereoUseHexagonGridMesh);
				TextDescription("Mesh with smoother corners for less artifacting. May introduce warping.");

				ImGui::Checkbox("Fill Holes", &stereoCustomConfig.StereoFillHoles);
				TextDescription("Fills in invalid depth values from neighboring areas.");

				ImGui::Checkbox("Draw Background", &stereoCustomConfig.StereoDrawBackground);
				TextDescription("Extra pass to render a cylinder mesh behind the stereo mesh.");

				IMGUI_BIG_SPACING;

				BeginSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSliderInt("Depth Map Scale", &stereoCustomConfig.StereoDepthMapScale, 1, 4, "%d", 1);
				TextDescriptionSpaced("Scale of generated depth maps releative to the proecessed disparity maps.");
				EndSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass);

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSlider("Disparity Smoothing Confidence Cutout", &stereoCustomConfig.StereoDisparityFilterConfidenceCutout, 0.0f, 1.0f, "%.2f", 0.01f);
				TextDescriptionSpaced("Maximum confidence for applying disparity smoothing. Setting this to 1 will always apply smoothing.");

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Camera Composition"))
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoCutoutEnabled);
				ScrollableSlider("Composition Cutout Factor", &stereoCustomConfig.StereoCutoutFactor, 0.0f, 3.0f, "%.2f", 0.01f);
				ScrollableSlider("Composition Cutout Offset", &stereoCustomConfig.StereoCutoutOffset, 0.0f, 2.0f, "%.2f", 0.01f);
				ScrollableSlider("Composition Cutout Filter Distance", &stereoCustomConfig.StereoCutoutFilterWidth, 0.1f, 2.0f, "%.1f", 0.1f);
				ScrollableSlider("Composition Combine Factor", &stereoCustomConfig.StereoCutoutCombineFactor, 0.0f, 1.0f, "%.1f", 0.1f);
				TextDescription("Merges pixels from both cameras where both have good confidence.");
				ScrollableSlider("Composition Cutout Secondary Camera Weight", &stereoCustomConfig.StereoCutoutSecondaryCameraWeight, 0.0f, 1.0f, "%.1f", 0.1f);
				EndSoftDisabled(!stereoCustomConfig.StereoCutoutEnabled);

				ImGui::PopItemWidth();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Depth Fold"))
			{
				TextDescription("Adjusts depth mesh vertices to smooth out contours in areas with large depth discontinuities.\nThis helps with depth aliasing. Not used in fullscreen mode");
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass || stereoCustomConfig.StereoUseFullscreenPass);
				ScrollableSlider("Depth Fold Strength", &stereoCustomConfig.StereoDepthContourStrength, 0.0f, 5.0f, "%.1f", 0.1f);
				TextDescription("Strength of the effect.");
				ScrollableSlider("Depth Fold Theshold", &stereoCustomConfig.StereoDepthContourThreshold, 0.0f, 2.0f, "%.1f", 0.1f);
				TextDescription("Minimum depth difference treshold for applying contour adjustment.");
				EndSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass || stereoCustomConfig.StereoUseFullscreenPass);
				ImGui::PopItemWidth();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Fullscreen Contour"))
			{
				TextDescription("Detects edges in the depth map and moves pixels toward the front or back to provide sharp contours.\nThis reduces interpolation artifacts from low resolution depth maps.");

				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass || !stereoCustomConfig.StereoUseFullscreenPass);
				ScrollableSlider("Fullscreen Contour Filter Strength", &stereoCustomConfig.StereoDepthFullscreenContourStrength, 0.0f, 2.0f, "%.2f", 0.01f);
				TextDescription("How far the depth is adjusted. Set to 0 to disable feature.");
				ScrollableSlider("Fullscreen Contour Filter Theshold", &stereoCustomConfig.StereoDepthFullscreenContourThreshold, 0.0f, 1.0f, "%.2f", 0.01f);
				TextDescription("Depth treshold to trigger filtering.");
				ScrollableSliderInt("Fullscreen Contour Filter Width", &stereoCustomConfig.StereoDepthFullscreenContourFilterWidth, 0, 5, "%d", 1);
				TextDescription("Adds Gaussian filtering to smooth out depth map pixles.\nProduces a smoother contour. Set to 0 to not use.");
				EndSoftDisabled(!stereoCustomConfig.StereoUseSeparateDepthPass || !stereoCustomConfig.StereoUseFullscreenPass);

				ImGui::PopItemWidth();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Projection Temporal Filtering"))
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoUseDisparityTemporalFiltering);
				ScrollableSlider("Projection Temporal Filtering Strength", &stereoCustomConfig.StereoDisparityTemporalFilteringStrength, 0.0f, 1.0f, "%.2f", 0.01f);
				ScrollableSlider("Projection Temporal Filtering Cutout Factor", &stereoCustomConfig.StereoDisparityTemporalFilteringDistance, 0.1f, 10.0f, "%.1f", 0.1f);
				EndSoftDisabled(!stereoCustomConfig.StereoUseDisparityTemporalFiltering);
				ImGui::PopItemWidth();
				ImGui::TreePop();
			}

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Advanced CPU Settings"))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Block Matching"))
			{
				ImGui::Spacing();

				ImGui::Checkbox("Use Color", &stereoCustomConfig.StereoUseColor);
				TextDescription("Uses full color images for stereo proecssing.");

				ImGui::Checkbox("Use Frame Alpha Channel", &stereoCustomConfig.StereoUseBWInputAlpha);
				TextDescription("Uses existing alpha channel in camera frames instead of desaturating the color channels.\n May not work on all HMDs.");

				ImGui::Checkbox("Rectification Filtering", &stereoCustomConfig.StereoRectificationFiltering);
				TextDescriptionSpaced("Applies linear filtering before stereo processing.");

				ImGui::BeginGroup();
				ImGui::Text("SGBM Algorithm");
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

				IMGUI_BIG_SPACING;

				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSliderInt("BlockSize", &stereoCustomConfig.StereoBlockSize, 1, 35, "%d", 2);
				if (stereoCustomConfig.StereoBlockSize % 2 == 0) { stereoCustomConfig.StereoBlockSize += 1; }

				//ScrollableSliderInt("MinDisparity", &stereoCustomConfig.StereoMinDisparity, 0, 128, "%d", 1);
				ScrollableSliderInt("MaxDisparity", &stereoCustomConfig.StereoMaxDisparity, 16, 256, "%d", 1);
				if (stereoCustomConfig.StereoMinDisparity % 2 != 0) { stereoCustomConfig.StereoMinDisparity += 1; }
				if (stereoCustomConfig.StereoMinDisparity >= stereoCustomConfig.StereoMaxDisparity) { stereoCustomConfig.StereoMaxDisparity = stereoCustomConfig.StereoMinDisparity + 2; }
				if (stereoCustomConfig.StereoMaxDisparity < 16) { stereoCustomConfig.StereoMaxDisparity = 16; }
				if (stereoCustomConfig.StereoMaxDisparity % 16 != 0) { stereoCustomConfig.StereoMaxDisparity -= stereoCustomConfig.StereoMaxDisparity % 16; }

				ScrollableSliderInt("SGBM P1", &stereoCustomConfig.StereoSGBM_P1, 0, 256, "%d", 8);
				ScrollableSliderInt("SGBM P2", &stereoCustomConfig.StereoSGBM_P2, 0, 256, "%d", 32);
				ScrollableSliderInt("SGBM DispMaxDiff", &stereoCustomConfig.StereoSGBM_DispMaxDiff, 0, 256, "%d", 1);
				ScrollableSliderInt("SGBM PreFilterCap", &stereoCustomConfig.StereoSGBM_PreFilterCap, 0, 128, "%d", 1);
				ScrollableSliderInt("SGBM UniquenessRatio", &stereoCustomConfig.StereoSGBM_UniquenessRatio, 1, 32, "%d", 1);
				ScrollableSliderInt("SGBM SpeckleWindowSize", &stereoCustomConfig.StereoSGBM_SpeckleWindowSize, 0, 300, "%d", 10);
				ScrollableSliderInt("SGBM SpeckleRange", &stereoCustomConfig.StereoSGBM_SpeckleRange, 1, 8, "%d", 1);
				ImGui::PopItemWidth();

				IMGUI_BIG_SPACING;
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Disparity Filtering"))
			{
				ImGui::Text("Filtering Passes");
				ImGui::BeginGroup();
				if (ImGui::RadioButton("None###FiltNone", stereoCustomConfig.StereoFiltering == StereoFiltering_None))
				{
					stereoCustomConfig.StereoFiltering = StereoFiltering_None;
				}
				TextDescription("Filtering from SGBM pass only. Noisy image with many invalid areas.");

				if (ImGui::RadioButton("Weighted Least Squares###FiltWLS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS))
				{
					stereoCustomConfig.StereoFiltering = StereoFiltering_WLS;
				}
				TextDescription("Patches up invalid areas. May still be noisy.");

				if (ImGui::RadioButton("Weighted Least Squares & Fast Bilateral Solver###FiltWLSFBS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS_FBS))
				{
					stereoCustomConfig.StereoFiltering = StereoFiltering_WLS_FBS;
				}
				TextDescription("Patches up invalid areas and filters the output. May produce worse depth results.");

				if (ImGui::RadioButton("Fast Bilateral Solver###FiltFBS", stereoCustomConfig.StereoFiltering == StereoFiltering_FBS))
				{
					stereoCustomConfig.StereoFiltering = StereoFiltering_FBS;
				}
				TextDescription("Patches up invalid areas and filters the output. May produce worse depth results.");
				ImGui::EndGroup();

				IMGUI_BIG_SPACING;

				BeginSoftDisabled(stereoCustomConfig.StereoFiltering == StereoFiltering_None || stereoCustomConfig.StereoFiltering == StereoFiltering_FBS);
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSlider("WLS Lambda", &stereoCustomConfig.StereoWLS_Lambda, 1.0f, 10000.0f, "%.0f", 100.0f);
				ScrollableSlider("WLS Sigma", &stereoCustomConfig.StereoWLS_Sigma, 0.5f, 2.0f, "%.1f", 0.1f);
				ScrollableSlider("WLS Confidence Radius", &stereoCustomConfig.StereoWLS_ConfidenceRadius, 0.1f, 2.0f, "%.1f", 0.1f);
				EndSoftDisabled(stereoCustomConfig.StereoFiltering == StereoFiltering_None || stereoCustomConfig.StereoFiltering == StereoFiltering_FBS);
				IMGUI_BIG_SPACING;

				BeginSoftDisabled(stereoCustomConfig.StereoFiltering == StereoFiltering_None || stereoCustomConfig.StereoFiltering == StereoFiltering_WLS);
				ScrollableSlider("FBS Spatial", &stereoCustomConfig.StereoFBS_Spatial, 0.0f, 50.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Luma", &stereoCustomConfig.StereoFBS_Luma, 0.0f, 16.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Chroma", &stereoCustomConfig.StereoFBS_Chroma, 0.0f, 16.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Lambda", &stereoCustomConfig.StereoFBS_Lambda, 0.0f, 256.0f, "%.0f", 1.0f);

				ScrollableSliderInt("FBS Iterations", &stereoCustomConfig.StereoFBS_Iterations, 1, 35, "%d", 1);
				EndSoftDisabled(stereoCustomConfig.StereoFiltering == StereoFiltering_None || stereoCustomConfig.StereoFiltering == StereoFiltering_WLS);
				ImGui::PopItemWidth();

				ImGui::TreePop();
			}
		}
		IMGUI_BIG_SPACING;

		EndSoftDisabled(mainConfig.StereoPreset != StereoPreset_Custom);

		if (mainConfig.StereoPreset == StereoPreset_Custom)
		{
			m_configManager->GetConfig_Stereo() = stereoCustomConfig;
		}

		ImGui::EndChild();
	}



	if (m_activeTab == TabOverrides)
	{
		ImGui::BeginChild("Overrides Pane");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Depth"))
		{
			BeginSoftDisabled(!depthConfig.DepthReadFromApplication);
			ImGui::Checkbox("Force Depth Composition", &depthConfig.DepthForceComposition);
			TextDescription("Enables composing the passthrough by depth for applications that submit a depth buffer.");
			EndSoftDisabled(!depthConfig.DepthReadFromApplication);

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Depth Range"))
			{
				ImGui::Checkbox("Force Depth Range testing", &depthConfig.DepthForceRangeTest);
				TextDescription("Force passthrough to only render in a certain depth range.");

				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSlider("Depth Range Min", &depthConfig.DepthForceRangeTestMin, 0.0f, 10.0f, "%.1f", 0.1f);
				ScrollableSlider("Depth Range Max", &depthConfig.DepthForceRangeTestMax, 0.0f, 10.0f, "%.1f", 0.1f);
				ImGui::PopItemWidth();

				ImGui::TreePop();
			}

			//ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Advanced"))
			{
				ImGui::Checkbox("Read Depth Buffers", &depthConfig.DepthReadFromApplication);
				TextDescription("Allow reading depth buffers submitted by the application.");

				BeginSoftDisabled(!depthConfig.DepthReadFromApplication);
				ImGui::Checkbox("Write Depth", &depthConfig.DepthWriteOutput);
				TextDescription("Allows writing passthrough depth to depth buffers submitted to the runtime.");
				EndSoftDisabled(!depthConfig.DepthReadFromApplication);

				ImGui::TreePop();
			}
			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Mode"))
		{
			ImGui::Checkbox("Force Passthrough Mode", &coreConfig.CoreForcePassthrough);
			TextDescription("Forces passthrough on even if the application does not support it.");

			BeginSoftDisabled(!coreConfig.CoreForcePassthrough);

			ImGui::BeginGroup();
			if (ImGui::RadioButton("Alpha Blend###CoreForce3", coreConfig.CoreForceMode == 3))
			{
				coreConfig.CoreForceMode = 3;
			}
			TextDescription("Blends passthrough with application provided alpha mask. This requires application support.");
			if (ImGui::RadioButton("Additive###CoreForcef2", coreConfig.CoreForceMode == 2))
			{
				coreConfig.CoreForceMode = 2;
			}
			TextDescription("Adds passthrough and application output together.");
			if (ImGui::RadioButton("Opaque###Coreforce1", coreConfig.CoreForceMode == 1))
			{
				coreConfig.CoreForceMode = 1;
			}
			TextDescription("Replaces the application output with passthrough.");
			if (ImGui::RadioButton("Masked###Coreforce0", coreConfig.CoreForceMode == 0))
			{
				coreConfig.CoreForceMode = 0;
			}
			TextDescription("Blends passthrough with the application output using a chroma key mask.");
			ImGui::EndGroup();
			
			ImGui::Spacing();

			ImGui::Text("Premultiplied Alpha");
			ImGui::BeginGroup();
			if (ImGui::RadioButton("Application Controlled", coreConfig.CoreForcePremultipliedAlpha == -1))
			{
				coreConfig.CoreForcePremultipliedAlpha = -1;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Force On", coreConfig.CoreForcePremultipliedAlpha == 1))
			{
				coreConfig.CoreForcePremultipliedAlpha = 1;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Force Off", coreConfig.CoreForcePremultipliedAlpha == 0))
			{
				coreConfig.CoreForcePremultipliedAlpha = 0;
			}
			TextDescription("Overrides alpha premultiplication handling for applications that report it incorrectly.");
			ImGui::EndGroup();

			IMGUI_BIG_SPACING;

			EndSoftDisabled(!coreConfig.CoreForcePassthrough);
		}

		BeginSoftDisabled(!coreConfig.CoreForcePassthrough);

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Masked Croma Key Settings"))
		{
			ImGui::BeginGroup();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
			ScrollableSlider("Chroma Range", &coreConfig.CoreForceMaskedFractionChroma, 0.0f, 1.0f, "%.2f", 0.01f);
			ScrollableSlider("Luma Range", &coreConfig.CoreForceMaskedFractionLuma, 0.0f, 1.0f, "%.2f", 0.01f);
			ScrollableSlider("Smoothing", &coreConfig.CoreForceMaskedSmoothing, 0.01f, 0.2f, "%.3f", 0.005f);
			ImGui::Checkbox("Invert mask", &coreConfig.CoreForceMaskedInvertMask);
			ImGui::Checkbox("Combine With Application Alpha", &coreConfig.CoreForceMaskedUseAppAlpha);

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
			ImGui::PopItemWidth();

			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.75f);
			ImGui::ColorPicker3("Key", coreConfig.CoreForceMaskedKeyColor, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar);
			ImGui::EndGroup();
		}

		EndSoftDisabled(!coreConfig.CoreForcePassthrough);

		ImGui::EndChild();
	}



	if (m_activeTab == TabCamera)
	{
		if (!m_cameraTabBeenOpened)
		{
			CameraEnumerator::EnumerateCameras(m_cameraDevices);
			if (m_openVRManager->IsRuntimeIntialized())
			{
				m_openVRManager->GetDeviceIdentProperties(m_deviceIdentProps);
			}
			m_cameraTabBeenOpened = true;
		}

		ImGui::BeginChild("Camera Pane");

		ImGui::BeginChild("Camera Settings", ImVec2(0, -60));


		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status###CameraStatus"))
		{
			ImGui::PushFont(m_fixedFont);
			ImGui::Text("Current Camera API: %s, %u x %u @ %.0f fps", displayValues.CameraAPI, displayValues.CameraFrameWidth, displayValues.CameraFrameHeight, displayValues.CameraFrameRate);
			IMGUI_BIG_SPACING;
			ImGui::PopFont();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Common"))
		{
			ImGui::Text("Camera Provider");
			TextDescription("Source for passthrough camera images.");
			if (ImGui::RadioButton("SteamVR", mainConfig.CameraProvider == CameraProvider_OpenVR))
			{
				if (mainConfig.CameraProvider != CameraProvider_OpenVR)
				{
					mainConfig.CameraProvider = CameraProvider_OpenVR;
					rendererResetPending = true;
				}
			}
			TextDescription("Use the passthrough cameras on a compatible HMD. Uses the OpenVR Tracked Camera interface.");

			if (ImGui::RadioButton("Webcam (Experimental)", mainConfig.CameraProvider == CameraProvider_OpenCV))
			{
				if (mainConfig.CameraProvider != CameraProvider_OpenCV)
				{
					mainConfig.CameraProvider = CameraProvider_OpenCV;
					mainConfig.ProjectionMode = Projection_Custom2D;
					rendererResetPending = true;
				}
			}
			TextDescription("Use a regular webcam from the OpenCV camera interface. Requires manual configuration.");

			if (ImGui::RadioButton("Augmented (Experimental)", mainConfig.CameraProvider == CameraProvider_Augmented))
			{
				if (mainConfig.CameraProvider != CameraProvider_Augmented)
				{
					mainConfig.CameraProvider = CameraProvider_Augmented;
					mainConfig.ProjectionMode = Projection_StereoReconstruction;
					rendererResetPending = true;
				}
			}
			TextDescription("Use SteamVR for calculating depth, and a webcam for color data. Requires a HMD with a stereo camera and manual configuration.");

			ImGui::Checkbox("Clamp Camera Frame", &cameraConfig.ClampCameraFrame);
			TextDescription("Only draws passthrough in the actual frame area. When turned off the edge pixels are extended past the frame into a 360 degree view.");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSlider("Field of View Scale", &mainConfig.FieldOfViewScale, 0.1f, 2.0f, "%.2f", 0.01f);
			TextDescription("Sets the size of the rendered area in the Custom 2D and Stereo 3D projection modes.");

			IMGUI_BIG_SPACING;

			ImGui::Text("Override Distortion Mode");
			if (ImGui::RadioButton("Off", cameraConfig.CameraForceDistortionMode == CameraDistortionMode_NotSet))
			{
				cameraConfig.CameraForceDistortionMode = CameraDistortionMode_NotSet;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Normal Lens", cameraConfig.CameraForceDistortionMode == CameraDistortionMode_RegularLens))
			{
				cameraConfig.CameraForceDistortionMode = CameraDistortionMode_RegularLens;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Fisheye", cameraConfig.CameraForceDistortionMode == CameraDistortionMode_Fisheye))
			{
				cameraConfig.CameraForceDistortionMode = CameraDistortionMode_Fisheye;
			}

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Webcam Configuration"))
		{
			TextDescription("These settings are for the experimental webcam provider only.");

			ImGui::Text("Camera Selection");
			if (ImGui::Button("Refresh###RefreshCams"))
			{
				CameraEnumerator::EnumerateCameras(m_cameraDevices);
			}

			ImGui::SameLine();

			std::string comboPreview = "No device";

			if (m_cameraDevices.size() > cameraConfig.Camera0DeviceIndex)
			{
				comboPreview.assign(std::format("[{}] {}", cameraConfig.Camera0DeviceIndex, m_cameraDevices[cameraConfig.Camera0DeviceIndex]));
			}


			if (ImGui::BeginCombo("Devices", comboPreview.c_str()))
			{
				for (int i = 0; i < m_cameraDevices.size(); i++)
				{
					std::string comboValue = std::format("[{}] {}", i, m_cameraDevices[i]);

					const bool bIsSelected = (cameraConfig.Camera0DeviceIndex == i);
					if (ImGui::Selectable(comboValue.c_str(), bIsSelected))
					{
						cameraConfig.Camera0DeviceIndex = i;
					}

					if (bIsSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			IMGUI_BIG_SPACING;

			ImGui::Checkbox("Camera is Attached to Tracked Device", &cameraConfig.UseTrackedDevice);
			TextDescription("Enable if the camera is attached to to a tracked device, such as a HMD or tracker.");

			BeginSoftDisabled(!cameraConfig.UseTrackedDevice);

			ImGui::BeginDisabled(m_openVRManager->IsRuntimeIntialized());
			if (ImGui::Button("Refresh"))
			{
				if (m_openVRManager->IsRuntimeIntialized())
				{
					m_openVRManager->GetDeviceIdentProperties(m_deviceIdentProps);
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();

			std::string camComboPreview = "No device";

			if (m_deviceIdentProps.size() > m_currentIdentDevice)
			{
				camComboPreview.assign(std::format("[{}] {} - {}", m_currentIdentDevice, m_deviceIdentProps[m_currentIdentDevice].DeviceName, m_deviceIdentProps[m_currentIdentDevice].DeviceSerial));
			}


			if (ImGui::BeginCombo("Cameras", camComboPreview.c_str()))
			{
				for (int i = 0; i < m_deviceIdentProps.size(); i++)
				{
					std::string comboValue = std::format("[{}] {} - {}", i, m_deviceIdentProps[i].DeviceName, m_deviceIdentProps[m_currentIdentDevice].DeviceSerial);

					const bool bIsSelected = (m_currentIdentDevice == i);
					if (ImGui::Selectable(comboValue.c_str(), bIsSelected))
					{
						m_currentIdentDevice = i;

						cameraConfig.TrackedDeviceSerialNumber = m_deviceIdentProps[i].DeviceSerial;
					}

					if (bIsSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Text("Device Serial Number set: %s", cameraConfig.TrackedDeviceSerialNumber);

			TextDescription("Select the device the camera is attached to. The device is tracked by its serial number.");

			EndSoftDisabled(!cameraConfig.UseTrackedDevice);

			IMGUI_BIG_SPACING;


			ImGui::Checkbox("Auto Exposure", &cameraConfig.AutoExposureEnable);

			bool prevAutoeExp = cameraConfig.AutoExposureEnable;
			float prefExp = cameraConfig.ExposureValue;

			BeginSoftDisabled(cameraConfig.AutoExposureEnable);
			ImGui::DragFloat("Exposure", &cameraConfig.ExposureValue, 0.1f, 0.0f, 0.0f, "%.1f");
			EndSoftDisabled(cameraConfig.AutoExposureEnable);

			if (prevAutoeExp != cameraConfig.AutoExposureEnable || prefExp != cameraConfig.ExposureValue)
			{
				cameraParamChangesPending = true;
			}

			IMGUI_BIG_SPACING;

			ImGui::DragFloat("Frame Delay Offset (s)", &cameraConfig.FrameDelayOffset, 0.001f, 0.0f, 1.0f, "%.3f");
			TextDescription("The delay from the camera capturing the image to it being received by the application. This may vary between cameras. Adjust until the view stops lagging when moving your head.");

			ImGui::Checkbox("Request Custom Resolution", &cameraConfig.RequestCustomFrameSize);
			TextDescription("Set a resolution to use. The system may select the closest matching available one. If turned off, the system will attempt to select the best available resolution.");

			BeginSoftDisabled(!cameraConfig.RequestCustomFrameSize);
			ImGui::DragInt2("Width x Height###FrameWH", cameraConfig.CustomFrameDimensions, 1.0f, 1, 8192);
			ImGui::DragInt("FPS###FrameFPS", &cameraConfig.CustomFrameRate, 1.0f, 1, 120);

			EndSoftDisabled(!cameraConfig.RequestCustomFrameSize);

			ImGui::Text("Camera Frame Layout");
			if (ImGui::RadioButton("Monocular", cameraConfig.CameraFrameLayout == Mono))
			{
				cameraConfig.CameraFrameLayout = Mono;
			}

			if (ImGui::RadioButton("Stereo Vertical", cameraConfig.CameraFrameLayout == StereoVerticalLayout))
			{
				cameraConfig.CameraFrameLayout = StereoVerticalLayout;
			}

			if (ImGui::RadioButton("Stereo Horizontal", cameraConfig.CameraFrameLayout == StereoHorizontalLayout))
			{
				cameraConfig.CameraFrameLayout = StereoHorizontalLayout;
			}
			ImGui::Checkbox("Camera Has Fisheye Lens", &cameraConfig.CameraHasFisheyeLens);

			IMGUI_BIG_SPACING;

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Left Camera"))
			{
				ImGui::Text("Camera Extrinsics");
				ImGui::DragFloat3("Camera Offset (m)", cameraConfig.Camera0_Translation, 0.001f, 0.0f, 0.0f, "%.3f");
				ImGui::DragFloat3("Camera Rotation (degrees)", cameraConfig.Camera0_Rotation, 0.001f, -180.0f, 180.0f, "%.3f");
				ImGui::Text("Camera Intrinsics");
				ImGui::DragFloat2("Focal Length", cameraConfig.Camera0_IntrinsicsFocal, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragFloat2("Center", cameraConfig.Camera0_IntrinsicsCenter, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragInt2("Sensor Pixels", cameraConfig.Camera0_IntrinsicsSensorPixels, 1.0f, 1, 8192);
				ImGui::DragFloat4("Distortion", cameraConfig.Camera0_IntrinsicsDist, 0.001f, 0.0f, 0.0f, "%.5f");

			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Right Camera"))
			{
				ImGui::Text("Camera Extrinsics");
				ImGui::DragFloat3("Camera Offset (m)###RightOffset", cameraConfig.Camera1_Translation, 0.001f, 0.0f, 0.0f, "%.3f");
				ImGui::DragFloat3("Camera Rotation (degrees)###RightRotation", cameraConfig.Camera1_Rotation, 0.001f, -180.0f, 180.0f, "%.3f");

				ImGui::Text("Camera Intrinsics");
				ImGui::DragFloat2("Focal Length###RightF", cameraConfig.Camera1_IntrinsicsFocal, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragFloat2("Center###RightC", cameraConfig.Camera1_IntrinsicsCenter, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragInt2("Sensor Pixels###RightPx", cameraConfig.Camera1_IntrinsicsSensorPixels, 1.0f, 1, 8192);
				ImGui::DragFloat4("Distortion##RightDist", cameraConfig.Camera1_IntrinsicsDist, 0.001f, 0.0f, 0.0f, "%.5f");
			}
		}
		IMGUI_BIG_SPACING;

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("SteamVR Camera Configuration"))
		{
			IMGUI_BIG_SPACING;

			ImGui::Checkbox("Enable Custom Calibration for SteamVR Camera", &cameraConfig.OpenVRCustomCalibration);

			IMGUI_BIG_SPACING;

			BeginSoftDisabled(!cameraConfig.OpenVRCustomCalibration);

			ImGui::Checkbox("SteamVR Camera Has Fisheye Lens", &cameraConfig.OpenVR_CameraHasFisheyeLens);

			IMGUI_BIG_SPACING;

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("SteamVR Camera 0"))
			{
				ImGui::Text("Camera Extrinsics");
				ImGui::DragFloat3("Camera Offset (m)###OpenVROffset", cameraConfig.OpenVR_Camera0_Translation, 0.001f, 0.0f, 0.0f, "%.3f");
				ImGui::DragFloat3("Camera Rotation (degrees)###OpenVRRot", cameraConfig.OpenVR_Camera0_Rotation, 0.001f, -180.0f, 180.0f, "%.3f");
				ImGui::Text("Camera Intrinsics");
				ImGui::DragFloat2("Focal Length###OpenVRF", cameraConfig.OpenVR_Camera0_IntrinsicsFocal, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragFloat2("Center###OpenVRC", cameraConfig.OpenVR_Camera0_IntrinsicsCenter, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragInt2("Sensor Pixels###OpenVRPx", cameraConfig.OpenVR_Camera0_IntrinsicsSensorPixels, 1.0f, 1, 8192);
				ImGui::DragFloat4("Distortion###OpenVRDist", cameraConfig.OpenVR_Camera0_IntrinsicsDist, 0.001f, 0.0f, 0.0f, "%.5f");
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("SteamVR Camera 1"))
			{
				ImGui::Text("Camera Extrinsics");
				ImGui::DragFloat3("Camera Offset (m)###OpenVRRightOffset", cameraConfig.OpenVR_Camera1_Translation, 0.001f, 0.0f, 0.0f, "%.3f");
				ImGui::DragFloat3("Camera Rotation (degrees)###OpenVRRightRotation", cameraConfig.OpenVR_Camera1_Rotation, 0.001f, -180.0f, 180.0f, "%.3f");
				ImGui::Text("Camera Intrinsics");
				ImGui::DragFloat2("Focal Length###OpenVRRightF", cameraConfig.OpenVR_Camera1_IntrinsicsFocal, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragFloat2("Center###OpenVRRightC", cameraConfig.OpenVR_Camera1_IntrinsicsCenter, 0.001f, 0.0f, 0.0f, "%.5f");
				ImGui::DragInt2("Sensor Pixels###OpenVRRightPx", cameraConfig.OpenVR_Camera1_IntrinsicsSensorPixels, 1.0f, 1, 8192);
				ImGui::DragFloat4("Distortion##OpenVRRightDist", cameraConfig.OpenVR_Camera1_IntrinsicsDist, 0.001f, 0.0f, 0.0f, "%.5f");
			}

			EndSoftDisabled(!cameraConfig.OpenVRCustomCalibration);
		}

		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::Spacing();

		if (ImGui::Button("Apply Camera Parameters"))
		{
			rendererResetPending = true;
		}

		ImGui::EndChild();
	}



	if (m_activeTab == TabDebug)
	{
		if (!m_debugTabBeenOpened)
		{
			if (m_openVRManager->IsRuntimeIntialized())
			{
				m_openVRManager->GetCameraDebugProperties(m_deviceDebugProps);
			}
			m_debugTabBeenOpened = true;
		}

		ImGui::BeginChild("TabDebug");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Debug"))
		{
			ImGui::BeginGroup();
			ImGui::Checkbox("Freeze Stereo Projection", &mainConfig.DebugStereoReconstructionFreeze);

			ImGui::BeginGroup();
			ImGui::Text("Debug Source");
			if (ImGui::RadioButton("None###DebugSourceNone", mainConfig.DebugSource == DebugSource_None))
			{
				mainConfig.DebugSource = DebugSource_None;
			}
			if (ImGui::RadioButton("Application Alpha", mainConfig.DebugSource == DebugSource_ApplicationAlpha))
			{
				mainConfig.DebugSource = DebugSource_ApplicationAlpha;
			}
			if (ImGui::RadioButton("Application Depth", mainConfig.DebugSource == DebugSource_ApplicationDepth))
			{
				mainConfig.DebugSource = DebugSource_ApplicationDepth;
			}
			if (ImGui::RadioButton("Output Depth", mainConfig.DebugSource == DebugSource_OutputDepth))
			{
				mainConfig.DebugSource = DebugSource_OutputDepth;
			}
			ImGui::EndGroup();

			ImGui::BeginGroup();
			ImGui::Text("Debug Overlay");
			if (ImGui::RadioButton("None###DebugOverlayNone", mainConfig.DebugOverlay == DebugOverlay_None))
			{
				mainConfig.DebugOverlay = DebugOverlay_None;
			}
			if (ImGui::RadioButton("Stereo Confidence", mainConfig.DebugOverlay == DebugOverlay_ProjectionConfidence))
			{
				mainConfig.DebugOverlay = DebugOverlay_ProjectionConfidence;
			}
			if (ImGui::RadioButton("Camera Selection", mainConfig.DebugOverlay == DebugOverlay_CameraSelction))
			{
				mainConfig.DebugOverlay = DebugOverlay_CameraSelction;
			}
			if (ImGui::RadioButton("Temporal Blending", mainConfig.DebugOverlay == DebugOverlay_TemporalBlending))
			{
				mainConfig.DebugOverlay = DebugOverlay_TemporalBlending;
			}
			if (ImGui::RadioButton("Temporal Clipping", mainConfig.DebugOverlay == DebugOverlay_TemporalClipping))
			{
				mainConfig.DebugOverlay = DebugOverlay_TemporalClipping;
			}
			if (ImGui::RadioButton("Disparity Filtering", mainConfig.DebugOverlay == DebugOverlay_DiscontinuityFiltering))
			{
				mainConfig.DebugOverlay = DebugOverlay_DiscontinuityFiltering;
			}
			ImGui::EndGroup();

			ImGui::BeginGroup();
			ImGui::Text("Debug Texture");
			if (ImGui::RadioButton("None", mainConfig.DebugTexture == DebugTexture_None))
			{
				mainConfig.DebugTexture = DebugTexture_None;
			}
			if (ImGui::RadioButton("Test Image", mainConfig.DebugTexture == DebugTexture_TestImage))
			{
				mainConfig.DebugTexture = DebugTexture_TestImage;
			}
			if (ImGui::RadioButton("Disparity Map", mainConfig.DebugTexture == DebugTexture_Disparity))
			{
				mainConfig.DebugTexture = DebugTexture_Disparity;
			}
			if (ImGui::RadioButton("Confidence Map", mainConfig.DebugTexture == DebugTexture_Confidence))
			{
				mainConfig.DebugTexture = DebugTexture_Confidence;
			}
			ImGui::EndGroup();

			ImGui::EndGroup();

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();

			ImGui::BeginGroup();
		
			ImGui::PushFont(m_fixedFont);
			ImGui::Text("Layer Version: %s", steamvr_passthrough::VersionString.c_str());

			switch (displayValues.appRenderAPI)
			{
			case DirectX11:
				ImGui::Text("Application Render API: DirectX 11");
				break;
			case DirectX12:
				ImGui::Text("Application Render API: DirectX 12");
				break;
			case Vulkan:
				ImGui::Text("Application Render API: Vulkan");
				break;
			case OpenGL:
				ImGui::Text("Application Render API: OpenGL");
				break;
			default:
				ImGui::Text("Application Render API: None");
			}

			switch (displayValues.renderAPI)
			{
			case DirectX11:
				ImGui::Text("Layer Render API: DirectX 11");
				break;
			case DirectX12:
				ImGui::Text("Layer Render API: DirectX 12 (Legacy)");
				break;
			case Vulkan:
				ImGui::Text("Layer Render API: Vulkan (Legacy)");
				break;
			default:
				ImGui::Text("Layer Render API: None");
			}

			ImGui::Text("Application: %s", displayValues.currentApplication.c_str());

			if (displayValues.lastFrameTimestamp != 0)
			{
				float timeSinceFrame = ((float)(frameStart.QuadPart - displayValues.lastFrameTimestamp)) / perfFrequency.QuadPart;
				if (timeSinceFrame > 1.0f)
				{
					ImGui::Text("Last frame submitted %.0f s ago", timeSinceFrame);
				}
				else
				{
					ImGui::Text("Last frame submitted %04.0f ms ago", timeSinceFrame * 1000.0f);
				}
				ImGui::Text("Submitted %i composition layers, %s", displayValues.numCompositionLayers, displayValues.bDepthLayerSubmitted ? "depth submitted" : "depth NOT submitted");
				ImGui::Text("Resolution: %i x %i", displayValues.frameBufferWidth, displayValues.frameBufferHeight);
				ImGui::Text("Framebuffer Flags: 0x%x", displayValues.frameBufferFlags);

				ImGui::Indent();
				if (ImGui::BeginTable("FrameBufferValsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
				{
					ImGui::TableNextColumn();
					ImGui::Text("Chromatic Abberation Correction");
					ImGui::TableNextColumn();
					displayValues.frameBufferFlags& XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT ?
						ImGui::TextColored(colorTextOrange, "Forced (1) (Deprecated)") :
						ImGui::TextColored(colorTextGreen, "Runtime Controlled (0)");

					ImGui::TableNextColumn();
					ImGui::Text("Alpha Channel");
					ImGui::TableNextColumn();
					displayValues.frameBufferFlags& XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT ?
						ImGui::TextColored(colorTextGreen, "Enabled (1)") :
						ImGui::TextColored(colorTextRed, "Disabled (0)");

					ImGui::TableNextColumn();
					ImGui::Text("Alpha Premultiplication");
					ImGui::TableNextColumn();
					displayValues.frameBufferFlags& XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT ?
						ImGui::TextColored(colorTextRed, "Not Premultipled (1)") :
						ImGui::TextColored(colorTextGreen, "Premultipled by Application (0)");

					ImGui::TableNextColumn();
					ImGui::Text("Inverted Alpha");
					ImGui::TableNextColumn();
					displayValues.frameBufferFlags& XR_COMPOSITION_LAYER_INVERTED_ALPHA_BIT_EXT ?
						(displayValues.bExtInvertedAlphaActive ?
							ImGui::TextColored(colorTextGreen, "Enabled (1)") :
							ImGui::TextColored(colorTextOrange, "Enabled (1) (Without extension!)")) :
						ImGui::TextColored(colorTextRed, "Disabled (0)");
					ImGui::EndTable();
				}
				ImGui::Unindent();
			}
			else
			{
				ImGui::Text("No frames submitted");
			}

			ImGui::Text("Framebuffer format: %s (%li)", GetImageFormatName(displayValues.appRenderAPI, displayValues.frameBufferFormat).c_str(), displayValues.frameBufferFormat);
			ImGui::Text("Depthbuffer format: %s (%li)", GetImageFormatName(displayValues.appRenderAPI, displayValues.depthBufferFormat).c_str(), displayValues.depthBufferFormat);
			std::isinf(displayValues.nearZ) ? ImGui::Text("Near Z: Infinity") : ImGui::Text("Near Z: %.3f", displayValues.nearZ);
			std::isinf(displayValues.farZ) ? ImGui::Text("Far Z: Infinity") : ImGui::Text("Far Z: %.3f", displayValues.farZ);


			ImGui::Text("Exposure to render latency: %.1fms", displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", displayValues.stereoReconstructionTimeMS);
			ImGui::Text("Camera frame retrieval duration: %.2fms", displayValues.frameRetrievalTimeMS);
			ImGui::Text("Menu framerate: %.1fHz", io.Framerate);
			ImGui::PopFont();

			IMGUI_BIG_SPACING;

			if (ImGui::Button("Dump Camera Frame to File"))
			{
				frameDumpPending = true;
			}

			ImGui::EndGroup();		
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Device Properties"))
		{

			ImGui::BeginDisabled(m_openVRManager->IsRuntimeIntialized());
			if (ImGui::Button("Refresh"))
			{
				if (m_openVRManager->IsRuntimeIntialized())
				{
					m_openVRManager->GetCameraDebugProperties(m_deviceDebugProps);
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();

			std::string comboPreview = "No device";

			if (m_deviceDebugProps.size() > m_currentDebugDevice)
			{
				comboPreview.assign(std::format("[{}] {}", m_currentDebugDevice, m_deviceDebugProps[m_currentDebugDevice].DeviceName));
			}
			 

			if (ImGui::BeginCombo("Devices", comboPreview.c_str()))
			{
				for (int i = 0; i < m_deviceDebugProps.size(); i++)
				{
					std::string comboValue = std::format("[{}] {}", i, m_deviceDebugProps[i].DeviceName);

					const bool bIsSelected = (m_currentDebugDevice == i);
					if (ImGui::Selectable(comboValue.c_str(), bIsSelected))
					{
						m_currentDebugDevice = i;
					}

					if (bIsSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::Spacing();
			if (m_deviceDebugProps.size() > m_currentDebugDevice)
			{
				DeviceDebugProperties& props = m_deviceDebugProps[m_currentDebugDevice];

				ImGui::PushFont(m_fixedFont);

				ImGui::Text("Class:");
				ImGui::SameLine();
				switch (props.DeviceClass)
				{
				case vr::TrackedDeviceClass_HMD:
					ImGui::Text("HMD");
					break;
				case vr::TrackedDeviceClass_Controller:
					ImGui::Text("Controller");
					break;
				case vr::TrackedDeviceClass_GenericTracker:
					ImGui::Text("Generic Tracker");
					break;
				case vr::TrackedDeviceClass_TrackingReference:
					ImGui::Text("Tracking Reference");
					break;
				case vr::TrackedDeviceClass_DisplayRedirect:
					ImGui::Text("Display Redirect");
					break;
				default:
					ImGui::Text("Unknown");
				}

				if (props.bHasCamera) { ImGui::Text("Has camera: True"); } else { ImGui::Text("Has camera: False"); }
				ImGui::Text("Number of cameras: %u", props.NumCameras);
				ImGui::Text("Camera firmware: %s, version: %lu", props.CameraFirmwareDescription.c_str(), props.CameraFirmwareVersion);
				ImGui::Text("HMD firmware version: %lu, FPGA version: %lu",props.HMDFirmwareVersion, props.FPGAFirmwareVersion);

				if (props.bAllowCameraToggle) { ImGui::Text("Camera toggle allowed: True"); }
				else { ImGui::Text("Camera toggle allowed: False"); }
				if (props.bAllowLightSourceFrequency) { ImGui::Text("Camera supports changing light source frequency: True"); }
				else { ImGui::Text("Camera supports changing light source frequency: False"); }
				if (props.bHMDSupportsRoomViewDirect) { ImGui::Text("HMD supports Room View Direct: True"); }
				else { ImGui::Text("HMD supports Room View Direct: False"); }
				if (props.bSupportsRoomViewDepthProjection) { ImGui::Text("Camera supports Room View depth projection: True"); }
				else { ImGui::Text("Camera supports Room View depth projection: False"); }

				ImGui::Text("Camera compatibility mode: %u", props.CameraCompatibilityMode);
				if (props.bCameraSupportsCompatibilityModes) { ImGui::Text("Camera supports compatibility modes: True"); }
				else { ImGui::Text("Camera supports compatibility modes: False"); }
				ImGui::Text("Camera exposure time: %f", props.CameraExposureTime);
				ImGui::Text("Camera global gain: %f", props.CameraGlobalGain);

				ImGui::Text("Camera frame layout:");
				if (props.CameraFrameLayout & vr::EVRTrackedCameraFrameLayout_Mono)
				{
					ImGui::SameLine();
					ImGui::Text("Mono");
				}
				if (props.CameraFrameLayout & vr::EVRTrackedCameraFrameLayout_Stereo)
				{
					ImGui::SameLine();
					ImGui::Text("Stereo");
				}
				if (props.CameraFrameLayout & vr::EVRTrackedCameraFrameLayout_HorizontalLayout)
				{
					ImGui::SameLine();
					ImGui::Text("Horizontal");
				}
				if (props.CameraFrameLayout & vr::EVRTrackedCameraFrameLayout_VerticalLayout)
				{
					ImGui::SameLine();
					ImGui::Text("Vertical");
				}
				if (props.CameraFrameLayout & ~0x33)
				{
					ImGui::SameLine();
					ImGui::Text("Unknown flags!");
				}

				ImGui::Text("Camera stream format:");
				ImGui::SameLine();
				switch (props.CameraStreamFormat)
				{
				case 1:
					ImGui::Text("RAW10");
					break;
				case 2:
					ImGui::Text("NV12");
					break;
				case 3:
					ImGui::Text("RGB24");
					break;
				case 4:
					ImGui::Text("NV12_2");
					break;
				case 5:
					ImGui::Text("YUYV16");
					break;
				case 6:
					ImGui::Text("BAYER16BG");
					break;
				case 7:
					ImGui::Text("MJPEG");
					break;
				case 8:
					ImGui::Text("RGBX32");
					break;
				default:
					ImGui::Text("Unknown");
				}

				ImGui::Text("Camera to head transform:");
				for (int y = 0; y < 3; y++)
				{
					ImGui::Text("[");
					ImGui::SameLine();
					for (int x = 0; x < 4; x++)
					{
						ImGui::Text("%f", props.CameraToHeadTransform.m[y][x]);
						ImGui::SameLine();
					}
					ImGui::Text("]");
				}

				ImGui::Text("Distorted camera frame size: %d x %d", props.DistortedFrameWidth, props.DistortedFrameHeight);
				ImGui::Text("Undistorted camera frame size: %d x %d", props.UndistortedFrameWidth, props.UndistortedFrameHeight);
				ImGui::Text("Max undistorted camera frame size: %d x %d", props.MaximumUndistortedFrameWidth, props.MaximumUndistortedFrameHeight);

				IMGUI_BIG_SPACING;

				for (uint32_t i = 0; i < props.NumCameras; i++)
				{
					ImGui::BeginGroup();

					ImGui::Text("Camera %u:", i);

					ImGui::Text("Undistorted intrinsics:\nf = [ %.2f %.2f ] c = [ %.2f %.2f ]", props.CameraProps[i].UndistortedFocalLength.v[0], props.CameraProps[i].UndistortedFocalLength.v[1], props.CameraProps[i].UndistortedOpticalCenter.v[0], props.CameraProps[i].UndistortedOpticalCenter.v[1]);

					ImGui::Text("Max undistorted intrinsics:\nf = [ %.2f %.2f ] c = [ %.2f %.2f ]", props.CameraProps[i].MaximumUndistortedFocalLength.v[0], props.CameraProps[i].MaximumUndistortedFocalLength.v[1], props.CameraProps[i].MaximumUndistortedOpticalCenter.v[0], props.CameraProps[i].MaximumUndistortedOpticalCenter.v[1]);

					ImGui::Spacing();

					ImGui::Text("Camera to head transform:");
					for (int y = 0; y < 3; y++)
					{
						ImGui::Text("[");
						ImGui::SameLine();
						for (int x = 0; x < 4; x++)
						{
							ImGui::Text("%f", props.CameraProps[i].CameraToHeadTransform.m[y][x]);
							ImGui::SameLine();
						}
						ImGui::Text("]");
					}
					EulerAngles angles = HMDMatRotationToEuler(props.CameraProps[i].CameraToHeadTransform);
					ImGui::Text("Rotation(deg): %.2f %.2f %.2f", angles.X, angles.Y, angles.Z);
					
					ImGui::Spacing();

					ImGui::Text("Undistorted projection:");
					for (int y = 0; y < 4; y++)
					{
						ImGui::Text("[");
						ImGui::SameLine();
						for (int x = 0; x < 4; x++)
						{
							ImGui::Text("%f", props.CameraProps[i].UndistortedProjecton.m[y][x]);
							ImGui::SameLine();
						}
						ImGui::Text("]");
					}

					ImGui::Text("Max undistorted projection:");
					for (int y = 0; y < 4; y++)
					{
						ImGui::Text("[");
						ImGui::SameLine();
						for (int x = 0; x < 4; x++)
						{
							ImGui::Text("%f", props.CameraProps[i].MaximumUndistortedProjecton.m[y][x]);
							ImGui::SameLine();
						}
						ImGui::Text("]");
					}

					ImGui::Spacing();

					ImGui::Text("White balance:\n[ %f %f %f %f ]", props.CameraProps[i].WhiteBalance.v[0], props.CameraProps[i].WhiteBalance.v[1], props.CameraProps[i].WhiteBalance.v[2], props.CameraProps[i].WhiteBalance.v[3]);

					ImGui::Spacing();

					ImGui::Text("Distortion function:");
					ImGui::SameLine();
					switch (props.CameraProps[i].DistortionFunction)
					{
					case vr::VRDistortionFunctionType_None:
						ImGui::Text("None");
						break;
					case vr::VRDistortionFunctionType_FTheta:
						ImGui::Text("F-theta");
						break;
					case vr::VRDistortionFunctionType_Extended_FTheta:
						ImGui::Text("Extended F-theta");
						break;
					default:
						ImGui::Text("Unknown");
					}

					ImGui::Text("Distortion coefficients:\n[ %f %f %f %f ]\n[ %f %f %f %f ]", props.CameraProps[i].DistortionCoefficients[0], props.CameraProps[i].DistortionCoefficients[1], props.CameraProps[i].DistortionCoefficients[2], props.CameraProps[i].DistortionCoefficients[3], props.CameraProps[i].DistortionCoefficients[4], props.CameraProps[i].DistortionCoefficients[5], props.CameraProps[i].DistortionCoefficients[6], props.CameraProps[i].DistortionCoefficients[7]);


					ImGui::EndGroup();

					if (i % 2 == 0 && i < props.NumCameras - 1)
					{
						ImGui::SameLine();
					}
				}

				ImGui::PopFont();
			}
			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Log"))
		{
			//ImGui::BeginChild("Log", ImVec2(0, 0), true);
			ImGui::PushFont(m_fixedFont);
			ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);

			ReadLogBuffer([](std::deque<std::string>& logBuffer) 
			{
				for (auto it = logBuffer.begin(); it != logBuffer.end(); it++)
				{
					ImGui::Text(it->c_str());
				}		
			});

			ImGui::PopTextWrapPos();
			ImGui::PopFont();
			//ImGui::EndChild();
		}
		ImGui::EndChild();
	}

	ImGui::End();

	bool bInteractionEnded = !ImGui::IsAnyItemActive() && m_bElementActiveLastFrame;
	m_bElementActiveLastFrame = ImGui::IsAnyItemActive();	

	if (bInteractionEnded) { m_bSettingsUpdatedThisSession = true; }

	if (bInteractionEnded)
	{
		m_configManager->ConfigUpdated();

		MenuIPCMessage message = {};

		switch (m_activeTab)
		{
		case TabMain:
		{
			message.Header.Type = MessageType_SendConfig_Main;
			message.Header.PayloadSize = sizeof(Config_Main);
			memcpy(message.Payload, &mainConfig, sizeof(Config_Main));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		case TabApplication:
		{
			message.Header.Type = MessageType_SendConfig_Core;
			message.Header.PayloadSize = sizeof(Config_Core);
			memcpy(message.Payload, &coreConfig, sizeof(Config_Core));
			m_IPCServer->BroadcastMessage(message);

			message.Header.Type = MessageType_SendConfig_Extensions;
			message.Header.PayloadSize = sizeof(Config_Extensions);
			memcpy(message.Payload, &extConfig, sizeof(Config_Extensions));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		case TabCamera:
		{
			message.Header.Type = MessageType_SendConfig_Main;
			message.Header.PayloadSize = sizeof(Config_Main);
			memcpy(message.Payload, &mainConfig, sizeof(Config_Main));
			m_IPCServer->BroadcastMessage(message);

			message.Header.Type = MessageType_SendConfig_Camera;
			message.Header.PayloadSize = sizeof(Config_Camera);
			memcpy(message.Payload, &cameraConfig, sizeof(Config_Camera));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		case TabStereo:
		{
			message.Header.Type = MessageType_SendConfig_Main;
			message.Header.PayloadSize = sizeof(Config_Main);
			memcpy(message.Payload, &mainConfig, sizeof(Config_Main));
			m_IPCServer->BroadcastMessage(message);

			message.Header.Type = MessageType_SendConfig_Stereo;
			message.Header.PayloadSize = sizeof(Config_Stereo);
			memcpy(message.Payload, &stereoConfig, sizeof(Config_Stereo));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		case TabOverrides:
		{
			message.Header.Type = MessageType_SendConfig_Core;
			message.Header.PayloadSize = sizeof(Config_Core);
			memcpy(message.Payload, &coreConfig, sizeof(Config_Core));
			m_IPCServer->BroadcastMessage(message);

			message.Header.Type = MessageType_SendConfig_Depth;
			message.Header.PayloadSize = sizeof(Config_Depth);
			memcpy(message.Payload, &depthConfig, sizeof(Config_Depth));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		case TabDebug:
		{
			message.Header.Type = MessageType_SendConfig_Main;
			message.Header.PayloadSize = sizeof(Config_Main);
			memcpy(message.Payload, &mainConfig, sizeof(Config_Main));
			m_IPCServer->BroadcastMessage(message);

			break;
		}
		default:
			break;
		}
		
	}

	if (rendererResetPending)
	{
		MenuIPCMessage message = {};
		message.Header.Type = MessageType_SendCommand_ApplyRendererReset;
		message.Header.PayloadSize = 0;
		m_IPCServer->BroadcastMessage(message);
	}

	if (cameraParamChangesPending)
	{
		MenuIPCMessage message = {};
		message.Header.Type = MessageType_SendCommand_ApplyCameraParamChanges;
		message.Header.PayloadSize = 0;
		m_IPCServer->BroadcastMessage(message);
	}

	if (frameDumpPending)
	{
		MenuIPCMessage message = {};
		message.Header.Type = MessageType_SendCommand_DumpFrameTexture;
		message.Header.PayloadSize = 0;
		m_IPCServer->BroadcastMessage(message);
	}

	ImGui::PopFont();
	ImGui::PopStyleVar(3);

}



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT SettingsMenu::HandleWin32Events(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			return 0;
		}
		uint32_t resizeWidth = (UINT)LOWORD(lParam);
		uint32_t resizeHeight = (UINT)HIWORD(lParam);

		m_renderer.ResizeWindow(resizeWidth, resizeHeight);

		return 0;


	}
	return 0;
}

void SettingsMenu::MenuIPCClientConnected(int clientIndex)
{
	if (m_displayValues.size() != clientIndex)
	{
		ErrorLog("Client count desynced: %d != %d\n", clientIndex, m_displayValues.size());
	}

	while (m_displayValues.size() <= clientIndex)
	{
		m_displayValues.push_back(std::make_unique<MenuDisplayValues>());
	}

	if (m_activeClient < 0) { m_activeClient = clientIndex; }

	m_window->OnClientConnected();

	if (m_bSettingsUpdatedThisSession)
	{
		MenuIPCMessage message = {};

		message.Header.Type = MessageType_SendConfig_Main;
		message.Header.PayloadSize = sizeof(Config_Main);
		memcpy(message.Payload, &m_configManager->GetConfig_Main(), sizeof(Config_Main));
		m_IPCServer->WriteMessage(message, clientIndex);

		message.Header.Type = MessageType_SendConfig_Core;
		message.Header.PayloadSize = sizeof(Config_Core);
		memcpy(message.Payload, &m_configManager->GetConfig_Core(), sizeof(Config_Core));
		m_IPCServer->WriteMessage(message, clientIndex);

		message.Header.Type = MessageType_SendConfig_Extensions;
		message.Header.PayloadSize = sizeof(Config_Extensions);
		memcpy(message.Payload, &m_configManager->GetConfig_Extensions(), sizeof(Config_Extensions));
		m_IPCServer->WriteMessage(message, clientIndex);

		message.Header.Type = MessageType_SendConfig_Camera;
		message.Header.PayloadSize = sizeof(Config_Camera);
		memcpy(message.Payload, &m_configManager->GetConfig_Camera(), sizeof(Config_Camera));
		m_IPCServer->WriteMessage(message, clientIndex);

		message.Header.Type = MessageType_SendConfig_Stereo;
		message.Header.PayloadSize = sizeof(Config_Stereo);
		memcpy(message.Payload, &m_configManager->GetConfig_Stereo(), sizeof(Config_Stereo));
		m_IPCServer->WriteMessage(message, clientIndex);

		message.Header.Type = MessageType_SendConfig_Depth;
		message.Header.PayloadSize = sizeof(Config_Depth);
		memcpy(message.Payload, &m_configManager->GetConfig_Depth(), sizeof(Config_Depth));
		m_IPCServer->WriteMessage(message, clientIndex);
	}
}

void SettingsMenu::MenuIPCClientDisconnected(int clientIndex)
{
	if (clientIndex < m_displayValues.size())
	{
		m_displayValues.erase(m_displayValues.begin() + clientIndex);
	}
	else
	{
		ErrorLog("Client count desynced: %d >= %d\n", clientIndex, m_displayValues.size());
	}

	if (m_IPCServer->GetNumClients() == 0)
	{
		m_window->OnAllClientsDisconnected();
	}
}

void SettingsMenu::MenuIPCMessageReceived(MenuIPCMessage& message, int clientIndex)
{
	if (clientIndex >= m_displayValues.size())
	{
		ErrorLog("Invalid client message: %d >= %d\n", clientIndex, m_displayValues.size());
		return;
	}

	switch (message.Header.Type)
	{
	case MessageType_SetDisplayValues:

		if (message.Header.PayloadSize == sizeof(MenuDisplayValues))
		{
			std::lock_guard<std::mutex> lock(m_menuWriteMutex);

			std::string appname = std::move(m_displayValues[clientIndex]->currentApplication);

			*m_displayValues[clientIndex] = *reinterpret_cast<MenuDisplayValues*>(message.Payload);
			m_displayValues[clientIndex]->currentApplication = std::move(appname);
		}
		else
		{
			ErrorLog("Invalid size MenuDisplayValues from IPC: %d\n", (int)message.Header.PayloadSize);
		}

		break;

	case MessageType_SetAppName:

		if (message.Header.PayloadSize < IPC_PAYLOAD_SIZE && message.Payload[message.Header.PayloadSize] == '\0')
		{
			std::lock_guard<std::mutex> lock(m_menuWriteMutex);

			m_displayValues[clientIndex]->currentApplication = std::string(reinterpret_cast<const char*>(message.Payload));
		}
		else
		{
			ErrorLog("Invalid application name string from IPC!\n");
		}

		break;

	default:

		ErrorLog("Unhandled IPC message type %d\n", (int)message.Header.Type);

	}
}


