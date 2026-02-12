
#include "pch.h"
#include "vulkan_menu_renderer.h"
#include "desktop_window_win32.h"


#define VK_USE_PLATFORM_WIN32_KHR
#include "imgui_impl_vulkan.h"
#include "backends/imgui_impl_win32.h"

#include <vulkan/vulkan_beta.h>


static bool HasExtension(const std::vector<VkExtensionProperties>& properties, const char* extension)
{
	for (const VkExtensionProperties& property : properties)
	{
		if (strncmp(property.extensionName, extension, strlen(extension)) == 0)
		{
			return true;
		}
	}
	return false;
}

bool VulkanMenuRenderer::SetupRenderer(std::shared_ptr<DesktopWindowWin32> window)
{
	{
		std::vector<const char*> extensions;

		extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
		extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

		VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		createInfo.pApplicationInfo = &appInfo;

		uint32_t numProperties;
		std::vector<VkExtensionProperties> properties;
		vkEnumerateInstanceExtensionProperties(nullptr, &numProperties, nullptr);
		properties.resize(numProperties);
		VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &numProperties, properties.data());
		if (result != VK_SUCCESS)
		{
			ErrorLog("vkEnumerateInstanceExtensionProperties failure: %d\n", result);
			return false;
		}

		for (const char* extension : extensions)
		{
			if (!HasExtension(properties, extension))
			{
				ErrorLog("Required Vulkan extension not found: %s\n", extension);
				return false;
			}
		}

		if (HasExtension(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		createInfo.enabledExtensionCount = (uint32_t)extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();
		result = vkCreateInstance(&createInfo, nullptr, &m_instance);
		if (result != VK_SUCCESS)
		{
			ErrorLog("vkCreateInstance failure: %d\n", result);
			return false;
		}


		m_physicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
		if (m_physicalDevice == VK_NULL_HANDLE)
		{
			ErrorLog("Failed to select Vulkan physical device!\n");
			return false;
		}

		m_queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physicalDevice);
		if (m_queueFamily == (uint32_t)-1)
		{
			ErrorLog("Failed to select Vulkan queue family!\n");
			return false;
		}
	}

	{
		std::vector<const char*> deviceExtensions;
		deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		uint32_t numProperties;
		std::vector<VkExtensionProperties> properties;
		vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &numProperties, nullptr);
		properties.resize(numProperties);
		vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &numProperties, properties.data());


		if (HasExtension(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
		{
			deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
		}

		const float queue_priority[] = { 1.0f };
		VkDeviceQueueCreateInfo queue_info[1] = {};
		queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[0].queueFamilyIndex = m_queueFamily;
		queue_info[0].queueCount = 1;
		queue_info[0].pQueuePriorities = queue_priority;

		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
		create_info.pQueueCreateInfos = queue_info;
		create_info.enabledExtensionCount = (uint32_t)deviceExtensions.size();
		create_info.ppEnabledExtensionNames = deviceExtensions.data();
		VkResult result = vkCreateDevice(m_physicalDevice, &create_info, nullptr, &m_device);
		if (result != VK_SUCCESS)
		{
			ErrorLog("vkCreateDevice failure: %d\n", result);
			return false;
		}

		vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
	}

	{
		VkDescriptorPoolSize poolSizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
		};
		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 0;

		for (VkDescriptorPoolSize& poolSize : poolSizes)
			poolInfo.maxSets += poolSize.descriptorCount;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = poolSizes;
		VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
		if (result != VK_SUCCESS)
		{
			ErrorLog("vkCreateDescriptorPool failure: %d\n", result);
			return false;
		}
	}


	if (!window->CreateVulkanSurface(m_instance, m_surface))
	{
		ErrorLog("Failed to create Vulkan surface!\n");
		return false;
	}

	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, m_surface, &res);
	if (res != VK_TRUE)
	{
		ErrorLog("Presentation not suppported on Vulkan surface!\n");
		return false;
	}

	// Dear ImGUI renders in gamma space, and requires (incorrect) linear textures to not autoconvert to gamma on write.
	const VkFormat requestSurfaceImageFormat[] =
	{ VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };

	m_windowData.Surface = m_surface;
	m_windowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(m_physicalDevice, m_surface, requestSurfaceImageFormat, 4, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);


#ifdef APP_USE_UNLIMITED_FRAME_RATE
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
	VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
	m_windowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_physicalDevice, m_surface, &presentModes[0], 1);

	uint32_t width, height = 0;
	window->GetWindowDimensions(width, height);
	ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, &m_windowData, m_queueFamily, nullptr, width, height, 2, 0);

	return true;
}

void VulkanMenuRenderer::InitImGui()
{
	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.ApiVersion = VK_API_VERSION_1_3;
	initInfo.Instance = m_instance;
	initInfo.PhysicalDevice = m_physicalDevice;
	initInfo.Device = m_device;
	initInfo.QueueFamily = m_queueFamily;
	initInfo.Queue = m_queue;
	initInfo.PipelineCache = m_pipelineCache;
	initInfo.DescriptorPool = m_descriptorPool;
	initInfo.MinImageCount = 2;
	initInfo.ImageCount = m_windowData.ImageCount;
	initInfo.Allocator = nullptr;
	initInfo.PipelineInfoMain.RenderPass = m_windowData.RenderPass;
	initInfo.PipelineInfoMain.Subpass = 0;
	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	initInfo.CheckVkResultFn = nullptr;
	ImGui_ImplVulkan_Init(&initInfo);
}

void VulkanMenuRenderer::ResizeWindow(uint32_t width, uint32_t height)
{

	if (width > 0 && height > 0 && (m_swapChainRebuild || m_windowData.Width != width || m_windowData.Height != height))
	{
		ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, &m_windowData, m_queueFamily, nullptr, width, height, 2, 0);
		m_windowData.FrameIndex = 0;
		m_swapChainRebuild = false;
	}
}

void VulkanMenuRenderer::RenderMenu()
{
	ImGui::Render();

	VkSemaphore imageSemaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].ImageAcquiredSemaphore;
	VkSemaphore renderSemaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].RenderCompleteSemaphore;

	VkResult result = vkAcquireNextImageKHR(m_device, m_windowData.Swapchain, UINT64_MAX, imageSemaphore, VK_NULL_HANDLE, &m_windowData.FrameIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_swapChainRebuild = true;
		return;
	}
	else if (result == VK_SUBOPTIMAL_KHR)
	{
		m_swapChainRebuild = true;
	}
	else if (result != VK_SUCCESS)
	{
		ErrorLog("vkAcquireNextImageKHR failed: %d\n", result);
		return;
	}

	ImGui_ImplVulkanH_Frame* fd = &m_windowData.Frames[m_windowData.FrameIndex];


	result = vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkWaitForFences failed: %d\n", result);
		return;
	}

	result = vkResetFences(m_device, 1, &fd->Fence);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkResetFences failed: %d\n", result);
		return;
	}

	result = vkResetCommandPool(m_device, fd->CommandPool, 0);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkResetCommandPool failed: %d\n", result);
		return;
	}

	VkCommandBufferBeginInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer(fd->CommandBuffer, &bufferInfo);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkBeginCommandBuffer failed: %d\n", result);
		return;
	}

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = m_windowData.RenderPass;
	passInfo.framebuffer = fd->Framebuffer;
	passInfo.renderArea.extent.width = m_windowData.Width;
	passInfo.renderArea.extent.height = m_windowData.Height;
	passInfo.clearValueCount = 1;
	passInfo.pClearValues = &m_windowData.ClearValue;

	vkCmdBeginRenderPass(fd->CommandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);


	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);


	vkCmdEndRenderPass(fd->CommandBuffer);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &imageSemaphore;
	submitInfo.pWaitDstStageMask = &wait_stage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &fd->CommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderSemaphore;

	result = vkEndCommandBuffer(fd->CommandBuffer);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkEndCommandBuffer failed: %d\n", result);
		return;
	}

	result = vkQueueSubmit(m_queue, 1, &submitInfo, fd->Fence);
	if (result != VK_SUCCESS)
	{
		ErrorLog("vkQueueSubmit failed: %d\n", result);
		return;
	}

	if (m_swapChainRebuild)
	{
		return;
	}


	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_windowData.Swapchain;
	presentInfo.pImageIndices = &m_windowData.FrameIndex;

	result = vkQueuePresentKHR(m_queue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_swapChainRebuild = true;
		return;
	}
	else if (result == VK_SUBOPTIMAL_KHR)
	{
		m_swapChainRebuild = true;
	}
	else if (result != VK_SUCCESS)
	{
		ErrorLog("vkQueuePresentKHR failed: %d\n", result);
		return;
	}

	m_windowData.SemaphoreIndex = (m_windowData.SemaphoreIndex + 1) % m_windowData.SemaphoreCount;
}

void VulkanMenuRenderer::WaitDeinitImGui()
{
	vkDeviceWaitIdle(m_device);

	ImGui_ImplVulkan_Shutdown();
}

void VulkanMenuRenderer::CleanupRenderer()
{
	ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_windowData, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_instance, nullptr);
}

