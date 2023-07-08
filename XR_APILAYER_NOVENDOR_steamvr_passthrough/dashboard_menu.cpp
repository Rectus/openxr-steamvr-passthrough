#include "pch.h"
#include "dashboard_menu.h"
#include <log.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"

#include "fonts/roboto_medium.cpp"
#include "fonts/cousine_regular.cpp"

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
	, m_activeTab(TabMain)
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
	m_mainFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 24);
	m_smallFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 22);
	m_fixedFont = io.Fonts->AddFontFromMemoryCompressedTTF(cousine_regular_compressed_data, cousine_regular_compressed_size, 18);
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

inline void DashboardMenu::TextDescription(const char* fmt, ...)
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

inline void DashboardMenu::TextDescriptionSpaced(const char* fmt, ...)
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


void DashboardMenu::TickMenu() 
{
	HandleEvents();

	if (!m_bMenuIsVisible)
	{
		return;
	}

	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();
	Config_Extensions& extConfig = m_configManager->GetConfig_Extensions();
	Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();
	Config_Stereo& stereoCustomConfig = m_configManager->GetConfig_CustomStereo();
	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();

	ImVec4 colorTextGreen(0.2f, 0.8f, 0.2f, 1.0f);
	ImVec4 colorTextRed(0.8f, 0.2f, 0.2f, 1.0f);
	ImVec4 colorTextOrange(0.85f, 0.7f, 0.2f, 1.0f);

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplDX11_NewFrame();

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
	TAB_BUTTON("Debug", TabDebug);


	ImGui::BeginChild("Sep1", ImVec2(0, 40));
	ImGui::EndChild();

	ImGui::PushFont(m_smallFont);
	ImGui::Indent();
	ImGui::Text("Application:");
	ImGui::Text("%s", m_displayValues.currentApplication.c_str());

	ImGui::Separator();
	ImGui::Text("Session:");
	m_displayValues.bSessionActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");

	ImGui::Separator();
	ImGui::Text("Passthrough:");
	m_displayValues.bCorePassthroughActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");

	if (m_displayValues.bCorePassthroughActive && m_displayValues.bDepthBlendingActive)
	{
		ImGui::TextColored(colorTextGreen, "Depth Blending");
	}

	if (coreConfig.CoreForcePassthrough || depthConfig.DepthForceComposition)
	{
		ImGui::Separator();
		ImGui::Text("Override:");
		if (depthConfig.DepthForceComposition) { ImGui::TextColored(colorTextOrange, "Depth Composition"); }
		if (coreConfig.CoreForcePassthrough) { ImGui::TextColored(colorTextOrange, "Passthrough Mode"); }	
	}

	ImGui::Unindent();
	ImGui::PopFont();

	ImGui::BeginChild("Sep2", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 30));
	ImGui::EndChild();
	if (ImGui::Button("Reset To Defaults", tabButtonSize))
	{
		m_configManager->ResetToDefaults();
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

			ImGui::Text("Projection Mode");
			TextDescription("Method for projecting the passthrough cameras to the VR view.");
			if (ImGui::RadioButton("2D Room View", mainConfig.ProjectionMode == Projection_RoomView2D))
			{
				mainConfig.ProjectionMode = Projection_RoomView2D;
			}
			TextDescription("Cylindrical projection with floor. Matches the projection in the SteamVR Room View 2D mode.");

			if (ImGui::RadioButton("2D Custom", mainConfig.ProjectionMode == Projection_Custom2D))
			{
				mainConfig.ProjectionMode = Projection_Custom2D;
			}
			TextDescription("Cylindrical projection with floor. Custom distortion correction and projection calculation.");

			if (ImGui::RadioButton("3D Stereo (Experimental)", mainConfig.ProjectionMode == Projection_StereoReconstruction))
			{
				mainConfig.ProjectionMode = Projection_StereoReconstruction;
			}
			TextDescriptionSpaced("Full depth estimation.");

			ImGui::Checkbox("Project onto Render Models (Experimental)", &mainConfig.ProjectToRenderModels);
			TextDescriptionSpaced("Project the passthough view to the correct distance on render models, such as controllers. Requires good camera calibration.");

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

				ImGui::Checkbox("Enable Temporal Filtering (Experimental)", &mainConfig.EnableTemporalFiltering);
				TextDescriptionSpaced("Improves image quality by removing noise and flickering, and sharpening it. Possibly slightly increases image resolution. Expensive on the GPU.");

				if (ImGui::CollapsingHeader("Advanced"))
				{
					ImGui::Text("Temporal Filtering Sampling");
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
			
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSlider("Field of View Scale", &mainConfig.FieldOfViewScale, 0.0f, 1.0f, "%.1f", 0.1f);
			TextDescription("Sets the size of the rendered area in the Custom 2D and Stereo 3D projection modes.");

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Misc."))
		{
			ImGui::Checkbox("Show Descriptions", &mainConfig.ShowSettingDescriptions);
			ImGui::Checkbox("Use legacy DirectX 12 renderer", &mainConfig.UseLegacyD3D12Renderer);
			TextDescription("Uses the old native DirectX12 renderer for DirectX 12 applications. Not recommended since it is missing rendering features. Requires restart.");
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
			if (m_displayValues.bCorePassthroughActive)
			{
				ImGui::TextColored(colorTextGreen, "Active");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Inactive");
			}

			ImGui::Text("Application requested mode:");
			ImGui::SameLine();
			if (m_displayValues.CoreCurrentMode == 3) { ImGui::Text("Alpha Blend"); }
			else if (m_displayValues.CoreCurrentMode == 2) { ImGui::Text("Additive"); }
			else if (m_displayValues.CoreCurrentMode == 1) { ImGui::Text("Opaque"); }
			else { ImGui::Text("Unknown"); }
			ImGui::PopFont();

			IMGUI_BIG_SPACING;

			ImGui::Checkbox("Enable###CoreEnable", &coreConfig.CorePassthroughEnable);
			TextDescriptionSpaced("Allow OpenXR applications to enable passthrough.");

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
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Varjo Extensions"))
		{
			ImGui::PushFont(m_fixedFont);
			ImGui::Text("Varjo Depth Estimation extension:");
			ImGui::SameLine();
			if (m_displayValues.bVarjoDepthEstimationExtensionActive)
			{
				ImGui::TextColored(colorTextGreen, "Active");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Inactive");
			}

			ImGui::Text("Varjo Depth Composition extension:");
			ImGui::SameLine();
			if (m_displayValues.bVarjoDepthCompositionExtensionActive)
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

			if (m_displayValues.renderAPI == Vulkan)
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
			ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", m_displayValues.stereoReconstructionTimeMS);
			ImGui::PopFont();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Stereo Presets"))
		{
			ImGui::BeginGroup();
			if (ImGui::RadioButton("Very Low", mainConfig.StereoPreset == StereoPreset_VeryLow))
			{
				mainConfig.StereoPreset = StereoPreset_VeryLow;
			}

			if (ImGui::RadioButton("Low", mainConfig.StereoPreset == StereoPreset_Low))
			{
				mainConfig.StereoPreset = StereoPreset_Low;
			}

			if (ImGui::RadioButton("Medium", mainConfig.StereoPreset == StereoPreset_Medium))
			{
				mainConfig.StereoPreset = StereoPreset_Medium;
			}

			if (ImGui::RadioButton("High", mainConfig.StereoPreset == StereoPreset_High))
			{
				mainConfig.StereoPreset = StereoPreset_High;
			}

			if (ImGui::RadioButton("Very High", mainConfig.StereoPreset == StereoPreset_VeryHigh))
			{
				mainConfig.StereoPreset = StereoPreset_VeryHigh;
			}

			if (ImGui::RadioButton("Custom", mainConfig.StereoPreset == StereoPreset_Custom))
			{
				mainConfig.StereoPreset = StereoPreset_Custom;
			}
			ImGui::EndGroup();

			IMGUI_BIG_SPACING;
		}

		BeginSoftDisabled(mainConfig.StereoPreset != StereoPreset_Custom);

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Main Settings"))
		{
			ImGui::BeginGroup();
			ImGui::Text("Filtering");
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

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSliderInt("Image Downscale Factor", &stereoCustomConfig.StereoDownscaleFactor, 1, 16, "%d", 1);
			TextDescriptionSpaced("Ratio of the stereo processed image to the camera frame. Larger values will improve performance.");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
			ScrollableSliderInt("Disparity Smoothing", &stereoCustomConfig.StereoDisparityFilterWidth, 0, 20, "%d", 1);
			TextDescriptionSpaced("Applies smoothing to areas with low projection confidence.");

			ImGui::Checkbox("Calculate Disparity for Both Cameras", &stereoCustomConfig.StereoDisparityBothEyes);
			TextDescriptionSpaced("Calculates a separate disparity map for each camera, instead of using the left one for both.");

			ImGui::Checkbox("Composite Both Cameras for Each Eye", &stereoCustomConfig.StereoCutoutEnabled);
			TextDescriptionSpaced("Detects areas occluded to the main camera and renders them with the other camera where possible.");

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Performance"))
		{
				ImGui::Checkbox("Use Multiple Cores", &stereoCustomConfig.StereoUseMulticore);
				TextDescriptionSpaced("Allows the stereo calculations to use multiple CPU cores. This can be turned off for CPU limited applications.");

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				ScrollableSliderInt("Frame Skip Ratio", &stereoCustomConfig.StereoFrameSkip, 0, 14, "%d", 1);
				TextDescription("Skip stereo processing of this many frames for each frame processed. This does not affect the frame rate of viewed camera frames, every frame will still be reprojected on the latest stereo data.");

			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Advanced"))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Projection"))
			{
				ImGui::Spacing();
				ImGui::Checkbox("Use Hexagon Grid Mesh", &stereoCustomConfig.StereoUseHexagonGridMesh);
				TextDescription("Mesh with smoother corners for less artifacting. May introduce warping.");

				ImGui::Checkbox("Fill Holes", &stereoCustomConfig.StereoFillHoles);
				TextDescription("Extra pass to render a cylinder mesh behind the stereo mesh.");

				IMGUI_BIG_SPACING;
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoCutoutEnabled);
				ScrollableSlider("Composition Cutout Factor", &stereoCustomConfig.StereoCutoutFactor, 0.0f, 3.0f, "%.2f", 0.01f);
				ScrollableSlider("Composition Cutout Offset", &stereoCustomConfig.StereoCutoutOffset, 0.0f, 2.0f, "%.2f", 0.01f);
				ScrollableSlider("Composition Cutout Filter Distance", &stereoCustomConfig.StereoCutoutFilterWidth, 0.1f, 2.0f, "%.1f", 0.1f);
				EndSoftDisabled(!stereoCustomConfig.StereoCutoutEnabled);
				ImGui::PopItemWidth();
				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Temporal Filtering"))
			{
				ImGui::Checkbox("Use Disparity Temporal Filtering", &stereoCustomConfig.StereoUseDisparityTemporalFiltering);
				TextDescription("Possibly smoothes out and improves quality of the projection depth.");

				IMGUI_BIG_SPACING;
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
				BeginSoftDisabled(!stereoCustomConfig.StereoUseDisparityTemporalFiltering);
				ScrollableSlider("Disparity Temporal Filtering Strength", &stereoCustomConfig.StereoDisparityTemporalFilteringStrength, 0.0f, 1.0f, "%.1f", 0.1f);
				ScrollableSlider("Disparity Temporal Filtering Cutout Factor", &stereoCustomConfig.StereoDisparityTemporalFilteringDistance, 0.1f, 10.0f, "%.1f", 0.1f);
				EndSoftDisabled(!stereoCustomConfig.StereoUseDisparityTemporalFiltering);
				ImGui::PopItemWidth();
				ImGui::TreePop();
			}

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

				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Filtering"))
			{
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
			stereoConfig = stereoCustomConfig;
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
			TextDescription("Enables composing the passthough by depth for applications that submit a depth buffer.");
			EndSoftDisabled(!depthConfig.DepthReadFromApplication);

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



	if (m_activeTab == TabDebug)
	{
		ImGui::BeginChild("TabDebug");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Debug"))
		{
			ImGui::BeginGroup();
			ImGui::Checkbox("Freeze Stereo Projection", &stereoConfig.StereoReconstructionFreeze);
			ImGui::Checkbox("Debug Depth", &mainConfig.DebugDepth);
			ImGui::Checkbox("Debug Valid Stereo", &mainConfig.DebugStereoValid);

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

			switch (m_displayValues.renderAPI)
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

			ImGui::Text("Framebuffer format: %s (%li)", GetImageFormatName(m_displayValues.renderAPI, m_displayValues.frameBufferFormat).c_str(), m_displayValues.frameBufferFormat);
			ImGui::Text("Depthbuffer format: %s (%li)", GetImageFormatName(m_displayValues.renderAPI, m_displayValues.depthBufferFormat).c_str(), m_displayValues.depthBufferFormat);

			ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", m_displayValues.stereoReconstructionTimeMS);
			ImGui::Text("Camera frame retrieval duration: %.2fms", m_displayValues.frameRetrievalTimeMS);
			ImGui::PopFont();
			ImGui::EndGroup();		
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Device Properties"))
		{
			if (ImGui::Button("Refresh"))
			{
				m_openVRManager->GetCameraDebugProperties(m_deviceDebugProps);
			}

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


	if (ImGui::IsAnyItemActive())
	{
		m_configManager->ConfigUpdated();
	}

	ImGui::End();

	ImGui::PopFont();
	ImGui::PopStyleVar(3);

	ImGui::Render();

	ID3D11RenderTargetView* rtv = m_d3d11RTV.Get();
	m_d3d11DeviceContext->OMSetRenderTargets(1, &rtv, NULL);
	const float clearColor[4] = { 0, 0, 0, 1 };
	m_d3d11DeviceContext->ClearRenderTargetView(m_d3d11RTV.Get(), clearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	m_d3d11DeviceContext->Flush();

	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Linear;
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
