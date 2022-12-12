// *********** THIS FILE IS GENERATED - DO NOT EDIT ***********
// MIT License
//
// Copyright(c) 2021-2022 Matthieu Bucchianeri
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

#include "pch.h"

#include <layer.h>

#include "dispatch.h"
#include "log.h"

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

using namespace LAYER_NAMESPACE::log;

namespace LAYER_NAMESPACE
{

	// Auto-generated wrappers for the requested APIs.

	XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrGetSystem");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrGetSystem(instance, getInfo, systemId);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrGetSystem_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrGetSystem: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrGetSystem_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrGetSystem failed with %d\n", result);
		}

		return result;
	}

	XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t environmentBlendModeCapacityInput, uint32_t* environmentBlendModeCountOutput, XrEnvironmentBlendMode* environmentBlendModes)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrEnumerateEnvironmentBlendModes");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrEnumerateEnvironmentBlendModes_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrEnumerateEnvironmentBlendModes: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrEnumerateEnvironmentBlendModes_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrEnumerateEnvironmentBlendModes failed with %d\n", result);
		}

		return result;
	}

	XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateSession");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrCreateSession(instance, createInfo, session);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrCreateSession_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrCreateSession: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateSession_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrCreateSession failed with %d\n", result);
		}

		return result;
	}

	XrResult xrDestroySession(XrSession session)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySession");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrDestroySession(session);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrDestroySession_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrDestroySession: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySession_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrDestroySession failed with %d\n", result);
		}

		return result;
	}

	XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateReferenceSpace");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrCreateReferenceSpace(session, createInfo, space);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrCreateReferenceSpace_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrCreateReferenceSpace: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateReferenceSpace_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrCreateReferenceSpace failed with %d\n", result);
		}

		return result;
	}

	XrResult xrDestroySpace(XrSpace space)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySpace");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrDestroySpace(space);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrDestroySpace_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrDestroySpace: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySpace_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrDestroySpace failed with %d\n", result);
		}

		return result;
	}

	XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateSwapchain");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrCreateSwapchain(session, createInfo, swapchain);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrCreateSwapchain_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrCreateSwapchain: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrCreateSwapchain_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrCreateSwapchain failed with %d\n", result);
		}

		return result;
	}

	XrResult xrDestroySwapchain(XrSwapchain swapchain)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySwapchain");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrDestroySwapchain(swapchain);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrDestroySwapchain_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrDestroySwapchain: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrDestroySwapchain_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrDestroySwapchain failed with %d\n", result);
		}

		return result;
	}

	XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrAcquireSwapchainImage(swapchain, acquireInfo, index);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrAcquireSwapchainImage: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrAcquireSwapchainImage failed with %d\n", result);
		}

		return result;
	}

	XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrReleaseSwapchainImage");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrReleaseSwapchainImage(swapchain, releaseInfo);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrReleaseSwapchainImage_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrReleaseSwapchainImage: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrReleaseSwapchainImage_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrReleaseSwapchainImage failed with %d\n", result);
		}

		return result;
	}

	XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrBeginFrame");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrBeginFrame(session, frameBeginInfo);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrBeginFrame_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrBeginFrame: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrBeginFrame_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrBeginFrame failed with %d\n", result);
		}

		return result;
	}

	XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo)
	{
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrEndFrame");
#endif

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrEndFrame(session, frameEndInfo);
		}
		catch (std::exception exc)
		{
#if USE_TRACELOGGING
			TraceLoggingWrite(g_traceProvider, "xrEndFrame_Error", TLArg(exc.what(), "Error"));
#endif
			ErrorLog("xrEndFrame: %s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}
#if USE_TRACELOGGING
		TraceLoggingWrite(g_traceProvider, "xrEndFrame_Result", TLArg(xr::ToCString(result), "Result"));
#endif
		if (XR_FAILED(result)) {
			ErrorLog("xrEndFrame failed with %d\n", result);
		}

		return result;
	}


	// Auto-generated dispatcher handler.
	XrResult OpenXrApi::xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
	{
		XrResult result = m_xrGetInstanceProcAddr(instance, name, function);

		const std::string apiName(name);

		if (apiName == "xrDestroyInstance")
		{
			m_xrDestroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroyInstance);
		}
		else if (apiName == "xrGetSystem")
		{
			m_xrGetSystem = reinterpret_cast<PFN_xrGetSystem>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrGetSystem);
		}
		else if (apiName == "xrEnumerateEnvironmentBlendModes")
		{
			m_xrEnumerateEnvironmentBlendModes = reinterpret_cast<PFN_xrEnumerateEnvironmentBlendModes>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrEnumerateEnvironmentBlendModes);
		}
		else if (apiName == "xrCreateSession")
		{
			m_xrCreateSession = reinterpret_cast<PFN_xrCreateSession>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrCreateSession);
		}
		else if (apiName == "xrDestroySession")
		{
			m_xrDestroySession = reinterpret_cast<PFN_xrDestroySession>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroySession);
		}
		else if (apiName == "xrCreateReferenceSpace")
		{
			m_xrCreateReferenceSpace = reinterpret_cast<PFN_xrCreateReferenceSpace>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrCreateReferenceSpace);
		}
		else if (apiName == "xrDestroySpace")
		{
			m_xrDestroySpace = reinterpret_cast<PFN_xrDestroySpace>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroySpace);
		}
		else if (apiName == "xrCreateSwapchain")
		{
			m_xrCreateSwapchain = reinterpret_cast<PFN_xrCreateSwapchain>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrCreateSwapchain);
		}
		else if (apiName == "xrDestroySwapchain")
		{
			m_xrDestroySwapchain = reinterpret_cast<PFN_xrDestroySwapchain>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroySwapchain);
		}
		else if (apiName == "xrAcquireSwapchainImage")
		{
			m_xrAcquireSwapchainImage = reinterpret_cast<PFN_xrAcquireSwapchainImage>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrAcquireSwapchainImage);
		}
		else if (apiName == "xrReleaseSwapchainImage")
		{
			m_xrReleaseSwapchainImage = reinterpret_cast<PFN_xrReleaseSwapchainImage>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrReleaseSwapchainImage);
		}
		else if (apiName == "xrBeginFrame")
		{
			m_xrBeginFrame = reinterpret_cast<PFN_xrBeginFrame>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrBeginFrame);
		}
		else if (apiName == "xrEndFrame")
		{
			m_xrEndFrame = reinterpret_cast<PFN_xrEndFrame>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrEndFrame);
		}


		return result;
	}

	// Auto-generated create instance handler.
	XrResult OpenXrApi::xrCreateInstance(const XrInstanceCreateInfo* createInfo)
    {
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetInstanceProperties))))
		{
			throw new std::runtime_error("Failed to resolve xrGetInstanceProperties");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrGetSystemProperties", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetSystemProperties))))
		{
			throw new std::runtime_error("Failed to resolve xrGetSystemProperties");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateReferenceSpace))))
		{
			throw new std::runtime_error("Failed to resolve xrCreateReferenceSpace");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrLocateSpace", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrLocateSpace))))
		{
			throw new std::runtime_error("Failed to resolve xrLocateSpace");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrDestroySpace", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrDestroySpace))))
		{
			throw new std::runtime_error("Failed to resolve xrDestroySpace");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateViewConfigurationViews", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateViewConfigurationViews))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateViewConfigurationViews");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateSwapchainFormats))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateSwapchainFormats");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateSwapchain))))
		{
			throw new std::runtime_error("Failed to resolve xrCreateSwapchain");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrDestroySwapchain))))
		{
			throw new std::runtime_error("Failed to resolve xrDestroySwapchain");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateSwapchainImages))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateSwapchainImages");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrAcquireSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrAcquireSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrWaitSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrWaitSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrWaitSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrReleaseSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrReleaseSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrBeginFrame", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrBeginFrame))))
		{
			throw new std::runtime_error("Failed to resolve xrBeginFrame");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEndFrame", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEndFrame))))
		{
			throw new std::runtime_error("Failed to resolve xrEndFrame");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrLocateViews", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrLocateViews))))
		{
			throw new std::runtime_error("Failed to resolve xrLocateViews");
		}
		m_xrGetInstanceProcAddr(m_instance, "xrConvertTimeToWin32PerformanceCounterKHR", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrConvertTimeToWin32PerformanceCounterKHR));
		m_applicationName = createInfo->applicationInfo.applicationName;
		return XR_SUCCESS;
	}

} // namespace LAYER_NAMESPACE

