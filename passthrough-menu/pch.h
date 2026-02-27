
#pragma once

#include <algorithm>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include <deque>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <limits>

using namespace std::chrono_literals;

#if true

	#define WIN32_LEAN_AND_MEAN

	#define VK_USE_PLATFORM_WIN32_KHR

	#include <windows.h>
	#include <unknwn.h>
	#include <wrl.h>
	#include <wingdi.h>

	using Microsoft::WRL::ComPtr;

	#define WM_TRAYMESSAGE (WM_USER + 1)
	#define WM_SETTINGS_UPDATED (WM_USER + 2)
	#define WM_MENU_QUIT (WM_USER + 3)

#endif

#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12

#include <openxr/openxr.h>

#include <vulkan/vulkan.h>


// Link OpenVR as a static library for compatibility with utilities
// that use modified versions of openvr_api.dll, such as OpenComposite.
#define OPENVR_BUILD_STATIC
#include <openvr.h>


#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#include "spdlog/spdlog.h"

template<typename Mutex> class spdlog_imgui_buffer_sink;
using spdlog_imgui_buffer_sink_mt = spdlog_imgui_buffer_sink<std::mutex>;

extern std::shared_ptr<spdlog::logger> g_logger;
extern std::shared_ptr<spdlog_imgui_buffer_sink_mt> g_logDisplayBuffer;
