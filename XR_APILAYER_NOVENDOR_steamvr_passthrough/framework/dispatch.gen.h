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

#pragma once

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

namespace LAYER_NAMESPACE
{

	class OpenXrApi
	{
	private:
		XrInstance m_instance{ XR_NULL_HANDLE };
		std::string m_applicationName;
		std::vector<std::string> m_grantedExtensions;
        std::vector<std::string> m_requestedExtensions;

	protected:
		OpenXrApi() = default;

		PFN_xrGetInstanceProcAddr m_xrGetInstanceProcAddr{ nullptr };

	public:
		virtual ~OpenXrApi() = default;

		XrInstance GetXrInstance() const
		{
			return m_instance;
		}

		const std::string& GetApplicationName() const
		{
			return m_applicationName;
		}

		const std::vector<std::string>& GetGrantedExtensions() const
		{
			return m_grantedExtensions;
		}

        const std::vector<std::string>& GetRequestedExtensions() const
		{
			return m_requestedExtensions;
		}

		void SetGetInstanceProcAddr(PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr, XrInstance instance)
		{
			m_xrGetInstanceProcAddr = pfn_xrGetInstanceProcAddr;
			m_instance = instance;
		}

		void SetGrantedExtensions(std::vector<std::string>& grantedExtensions)
		{
			m_grantedExtensions = grantedExtensions;
		}

        void SetRequestedExtensions(std::vector<std::string>& requestedExtensions)
		{
			m_requestedExtensions = requestedExtensions;
		}

		// Specially-handled by the auto-generated code.
		virtual XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);
		virtual XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo);


		// Auto-generated entries for the requested APIs.

	public:
		virtual XrResult xrDestroyInstance(XrInstance instance)
		{
			return m_xrDestroyInstance(instance);
		}
	private:
		PFN_xrDestroyInstance m_xrDestroyInstance{ nullptr };

	public:
		virtual XrResult xrGetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties)
		{
			return m_xrGetInstanceProperties(instance, instanceProperties);
		}
	private:
		PFN_xrGetInstanceProperties m_xrGetInstanceProperties{ nullptr };

	public:
		virtual XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId)
		{
			return m_xrGetSystem(instance, getInfo, systemId);
		}
	private:
		PFN_xrGetSystem m_xrGetSystem{ nullptr };

	public:
		virtual XrResult xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties)
		{
			return m_xrGetSystemProperties(instance, systemId, properties);
		}
	private:
		PFN_xrGetSystemProperties m_xrGetSystemProperties{ nullptr };

	public:
		virtual XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t environmentBlendModeCapacityInput, uint32_t* environmentBlendModeCountOutput, XrEnvironmentBlendMode* environmentBlendModes)
		{
			return m_xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);
		}
	private:
		PFN_xrEnumerateEnvironmentBlendModes m_xrEnumerateEnvironmentBlendModes{ nullptr };

	public:
		virtual XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session)
		{
			return m_xrCreateSession(instance, createInfo, session);
		}
	private:
		PFN_xrCreateSession m_xrCreateSession{ nullptr };

	public:
		virtual XrResult xrDestroySession(XrSession session)
		{
			return m_xrDestroySession(session);
		}
	private:
		PFN_xrDestroySession m_xrDestroySession{ nullptr };

	public:
		virtual XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space)
		{
			return m_xrCreateReferenceSpace(session, createInfo, space);
		}
	private:
		PFN_xrCreateReferenceSpace m_xrCreateReferenceSpace{ nullptr };

	public:
		virtual XrResult xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location)
		{
			return m_xrLocateSpace(space, baseSpace, time, location);
		}
	private:
		PFN_xrLocateSpace m_xrLocateSpace{ nullptr };

	public:
		virtual XrResult xrDestroySpace(XrSpace space)
		{
			return m_xrDestroySpace(space);
		}
	private:
		PFN_xrDestroySpace m_xrDestroySpace{ nullptr };

	public:
		virtual XrResult xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrViewConfigurationView* views)
		{
			return m_xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, views);
		}
	private:
		PFN_xrEnumerateViewConfigurationViews m_xrEnumerateViewConfigurationViews{ nullptr };

	public:
		virtual XrResult xrEnumerateSwapchainFormats(XrSession session, uint32_t formatCapacityInput, uint32_t* formatCountOutput, int64_t* formats)
		{
			return m_xrEnumerateSwapchainFormats(session, formatCapacityInput, formatCountOutput, formats);
		}
	private:
		PFN_xrEnumerateSwapchainFormats m_xrEnumerateSwapchainFormats{ nullptr };

	public:
		virtual XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain)
		{
			return m_xrCreateSwapchain(session, createInfo, swapchain);
		}
	private:
		PFN_xrCreateSwapchain m_xrCreateSwapchain{ nullptr };

	public:
		virtual XrResult xrDestroySwapchain(XrSwapchain swapchain)
		{
			return m_xrDestroySwapchain(swapchain);
		}
	private:
		PFN_xrDestroySwapchain m_xrDestroySwapchain{ nullptr };

	public:
		virtual XrResult xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images)
		{
			return m_xrEnumerateSwapchainImages(swapchain, imageCapacityInput, imageCountOutput, images);
		}
	private:
		PFN_xrEnumerateSwapchainImages m_xrEnumerateSwapchainImages{ nullptr };

	public:
		virtual XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index)
		{
			return m_xrAcquireSwapchainImage(swapchain, acquireInfo, index);
		}
	private:
		PFN_xrAcquireSwapchainImage m_xrAcquireSwapchainImage{ nullptr };

	public:
		virtual XrResult xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* waitInfo)
		{
			return m_xrWaitSwapchainImage(swapchain, waitInfo);
		}
	private:
		PFN_xrWaitSwapchainImage m_xrWaitSwapchainImage{ nullptr };

	public:
		virtual XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo)
		{
			return m_xrReleaseSwapchainImage(swapchain, releaseInfo);
		}
	private:
		PFN_xrReleaseSwapchainImage m_xrReleaseSwapchainImage{ nullptr };

	public:
		virtual XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
		{
			return m_xrBeginFrame(session, frameBeginInfo);
		}
	private:
		PFN_xrBeginFrame m_xrBeginFrame{ nullptr };

	public:
		virtual XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo)
		{
			return m_xrEndFrame(session, frameEndInfo);
		}
	private:
		PFN_xrEndFrame m_xrEndFrame{ nullptr };

	public:
		virtual XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views)
		{
			return m_xrLocateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
		}
	private:
		PFN_xrLocateViews m_xrLocateViews{ nullptr };

	public:
		virtual XrResult xrConvertTimeToWin32PerformanceCounterKHR(XrInstance instance, XrTime time, LARGE_INTEGER* performanceCounter)
		{
			return m_xrConvertTimeToWin32PerformanceCounterKHR(instance, time, performanceCounter);
		}
	private:
		PFN_xrConvertTimeToWin32PerformanceCounterKHR m_xrConvertTimeToWin32PerformanceCounterKHR{ nullptr };

	public:
		virtual XrResult xrSetEnvironmentDepthEstimationVARJO(XrSession session, XrBool32 enabled)
		{
			return m_xrSetEnvironmentDepthEstimationVARJO(session, enabled);
		}
	private:
		PFN_xrSetEnvironmentDepthEstimationVARJO m_xrSetEnvironmentDepthEstimationVARJO{ nullptr };



	};

} // namespace LAYER_NAMESPACE

