// MIT License
//
// Copyright(c) 2022 Rectus
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


#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32


#ifdef XR_USE_PLATFORM_WIN32

	#define XR_USE_GRAPHICS_API_D3D11
	#define XR_USE_GRAPHICS_API_D3D12
	#define WIN32_LEAN_AND_MEAN

	#include <windows.h>
	#include <unknwn.h>
	#include <wrl.h>
	#include <d3d11_4.h>
	#include <dxgi1_4.h>
	#include <d3d12.h>

	using Microsoft::WRL::ComPtr;

#endif



#define XR_USE_GRAPHICS_API_VULKAN

#ifdef XR_USE_GRAPHICS_API_VULKAN

	#ifdef XR_USE_PLATFORM_WIN32
		#define VK_USE_PLATFORM_WIN32_KHR
	#endif
	#include <vulkan/vulkan.h>

#endif


#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <xr_linear.h>
#include <loader_interfaces.h>

#include <openvr.h>

#include "check.h"

#define USE_TRACELOGGING 0

#if USE_TRACELOGGING
#include <wil/resource.h>
#include <traceloggingactivity.h>
#include <traceloggingprovider.h>
#include <fmt/format.h>

#include <XrToString.h>
#endif

