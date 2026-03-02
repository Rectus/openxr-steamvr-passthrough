
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
			g_logger->error("vkEnumerateInstanceExtensionProperties failure: {}", static_cast<int32_t>(result));
			return false;
		}

		for (const char* extension : extensions)
		{
			if (!HasExtension(properties, extension))
			{
				g_logger->error("Required Vulkan extension not found: {}", extension);
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
			g_logger->error("vkCreateInstance failure: {}", static_cast<int32_t>(result));
			return false;
		}


		m_physicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
		if (m_physicalDevice == VK_NULL_HANDLE)
		{
			g_logger->error("Failed to select Vulkan physical device!");
			return false;
		}

		m_queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physicalDevice);
		if (m_queueFamily == (uint32_t)-1)
		{
			g_logger->error("Failed to select Vulkan queue family!");
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
			g_logger->error("vkCreateDevice failure: {}", static_cast<int32_t>(result));
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
			g_logger->error("vkCreateDescriptorPool failure: {}", static_cast<int32_t>(result));
			return false;
		}
	}


	if (!window->CreateVulkanSurface(m_instance, m_surface))
	{
		g_logger->error("Failed to create Vulkan surface!");
		return false;
	}

	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, m_surface, &res);
	if (res != VK_TRUE)
	{
		g_logger->error("Presentation not suppported on Vulkan surface!");
		return false;
	}

	// Dear ImGUI renders in gamma space, and requires (incorrect) linear textures to not autoconvert to gamma on write.
	const VkFormat requestSurfaceImageFormat[] ={ VK_FORMAT_R8G8B8A8_UNORM };

	m_windowData.Surface = m_surface;
	m_windowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(m_physicalDevice, m_surface, requestSurfaceImageFormat, 4, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

	VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };

	m_windowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_physicalDevice, m_surface, &presentModes[0], 1);

	uint32_t width, height = 0;
	window->GetWindowDimensions(width, height);
	ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, &m_windowData, m_queueFamily, nullptr, width, height, 2, 0);

	
	m_textureData.m_pDevice = m_device;
	m_textureData.m_pPhysicalDevice = m_physicalDevice;
	m_textureData.m_pInstance = m_instance;
	m_textureData.m_pQueue = m_queue;
	m_textureData.m_nQueueFamilyIndex = m_queueFamily;
	m_textureData.m_nWidth = width;
	m_textureData.m_nHeight = height;
	m_textureData.m_nFormat = VK_FORMAT_R8G8B8A8_SRGB;
	m_textureData.m_nSampleCount = 1;

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

void VulkanMenuRenderer::CreateDashboardThumbnails(std::vector<std::vector<uint8_t>>& imageData, uint32_t width, uint32_t height)
{
	VkBuffer uploadBuffers[4] = {};
	VkDeviceMemory uploadMemories[4] = {};

	VkPhysicalDeviceMemoryProperties memProperties = {};
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

	VkResult result;
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	for (int i = 0; i < imageData.size(); i++)
	{
		m_thumbnailData[i].TextureData.m_pDevice = m_device;
		m_thumbnailData[i].TextureData.m_pPhysicalDevice = m_physicalDevice;
		m_thumbnailData[i].TextureData.m_pInstance = m_instance;
		m_thumbnailData[i].TextureData.m_pQueue = m_queue;
		m_thumbnailData[i].TextureData.m_nQueueFamilyIndex = m_queueFamily;
		m_thumbnailData[i].TextureData.m_nWidth = width;
		m_thumbnailData[i].TextureData.m_nHeight = height;
		m_thumbnailData[i].TextureData.m_nFormat = VK_FORMAT_R8G8B8A8_SRGB;
		m_thumbnailData[i].TextureData.m_nSampleCount = 1;

		VkImage* imagePtr = reinterpret_cast<VkImage*>(&m_thumbnailData[i].TextureData.m_nImage);

		result = vkCreateImage(m_device, &imageInfo, nullptr, imagePtr);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkCreateImage failed: {}", static_cast<int32_t>(result));
			return;
		}

		VkMemoryRequirements imageReq = {};
		vkGetImageMemoryRequirements(m_device, *imagePtr, &imageReq);
		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = imageReq.size;

		{
			bool bFoundType = false;
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
			{
				if ((imageReq.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				{
					allocInfo.memoryTypeIndex = i;
					bFoundType = true;
					break;
				}
			}
			if (!bFoundType)
			{
				g_logger->error("Failed to find vulkan memory type for thumbnails!");
				return;
			}
		}
		result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_thumbnailData[i].ImageMemory);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkAllocateMemory failed: {}", static_cast<int32_t>(result));
			return;
		}

		result = vkBindImageMemory(m_device, *imagePtr, m_thumbnailData[i].ImageMemory, 0);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkBindImageMemory failed: {}", static_cast<int32_t>(result));
			return;
		}


		//// Create the Image View
		//{
		//	VkImageViewCreateInfo info = {};
		//	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		//	info.image = *imagePtr;
		//	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		//	info.format = VK_FORMAT_R8G8B8A8_UNORM;
		//	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//	info.subresourceRange.levelCount = 1;
		//	info.subresourceRange.layerCount = 1;
		//	result = vkCreateImageView(m_device, &info, nullptr, &tex_data->ImageView);
		//	check_vk_result(err);
		//}

		//// Create Sampler
		//{
		//	VkSamplerCreateInfo sampler_info{};
		//	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		//	sampler_info.magFilter = VK_FILTER_LINEAR;
		//	sampler_info.minFilter = VK_FILTER_LINEAR;
		//	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		//	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // outside image bounds just use border color
		//	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		//	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		//	sampler_info.minLod = -1000;
		//	sampler_info.maxLod = 1000;
		//	sampler_info.maxAnisotropy = 1.0f;
		//	result = vkCreateSampler(m_device, &sampler_info, nullptr, &tex_data->Sampler);
		//	check_vk_result(err);
		//}

		//// Create Descriptor Set using ImGUI's implementation
		//tex_data->DS = ImGui_ImplVulkan_AddTexture(tex_data->Sampler, tex_data->ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Create Upload Buffer

		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = imageData[i].size();
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &uploadBuffers[i]);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkCreateBuffer failed: {}", static_cast<int32_t>(result));
			return;
		}

		VkMemoryRequirements bufferReq;
		vkGetBufferMemoryRequirements(m_device, uploadBuffers[i], &bufferReq);
		VkMemoryAllocateInfo bufferAllocInfo = {};
		bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		bufferAllocInfo.allocationSize = bufferReq.size;

		{
			bool bFoundType = false;
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
			{
				if ((bufferReq.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
				{
					bufferAllocInfo.memoryTypeIndex = i;
					bFoundType = true;
					break;
				}
			}
			if (!bFoundType)
			{
				g_logger->error("Failed to find vulkan memory type for thumbnails!");
				return;
			}
		}

		result = vkAllocateMemory(m_device, &bufferAllocInfo, nullptr, &uploadMemories[i]);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkAllocateMemory failed: {}", static_cast<int32_t>(result));
			return;
		}

		result = vkBindBufferMemory(m_device, uploadBuffers[i], uploadMemories[i], 0);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkBindBufferMemory failed: {}", static_cast<int32_t>(result));
			return;
		}
		
		void* map = NULL;

		result = vkMapMemory(m_device, uploadMemories[i], 0, imageData[i].size(), 0, &map);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkMapMemory failed: {}", static_cast<int32_t>(result));
			return;
		}

		memcpy(map, imageData[i].data(), imageData[i].size());
		VkMappedMemoryRange range[1] = {};
		range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[0].memory = uploadMemories[i];
		range[0].size = imageData[i].size();

		result = vkFlushMappedMemoryRanges(m_device, 1, range);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkFlushMappedMemoryRanges failed: {}", static_cast<int32_t>(result));
			return;
		}

		vkUnmapMemory(m_device, uploadMemories[i]);

	}

	// Create a command buffer that will perform following steps when hit in the command queue.
	// TODO: this works in the example, but may need input if this is an acceptable way to access the pool/create the command buffer.
	/*VkCommandPool command_pool = g_MainWindowData.Frames[g_MainWindowData.FrameIndex].CommandPool;
	VkCommandBuffer command_buffer;
	{
		VkCommandBufferAllocateInfo alloc_info{};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandPool = command_pool;
		alloc_info.commandBufferCount = 1;

		result = vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkAllocateCommandBuffers failed: {}", static_cast<int32_t>(result));
			return;
		}*/

	VkCommandBuffer commandBuffer = m_windowData.Frames[0].CommandBuffer;

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkBeginCommandBuffer failed: {}", static_cast<int32_t>(result));
		return;
	}
	//}

	
	for (int i = 0; i < imageData.size(); i++)
	{
		VkImage image = *reinterpret_cast<VkImage*>(&m_thumbnailData[i].TextureData.m_nImage);

		VkImageMemoryBarrier copyBarrier = {};
		copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		copyBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarrier.image = image;
		copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyBarrier.subresourceRange.levelCount = 1;
		copyBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &copyBarrier);

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(commandBuffer, uploadBuffers[i], image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier useBarrier = {};
		useBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		useBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		useBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		useBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		useBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		useBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		useBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		useBarrier.image = image;
		useBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		useBarrier.subresourceRange.levelCount = 1;
		useBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &useBarrier);
	}

	result = vkEndCommandBuffer(commandBuffer);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkEndCommandBuffer failed: {}", static_cast<int32_t>(result));
		return;
	}

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	result = vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkQueueSubmit failed: {}", static_cast<int32_t>(result));
		return;
	}
		
	result = vkDeviceWaitIdle(m_device);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkDeviceWaitIdle failed: {}", static_cast<int32_t>(result));
		return;
	}

	for (int i = 0; i < imageData.size(); i++)
	{
		vkFreeMemory(m_device, uploadMemories[i], nullptr);
		vkDestroyBuffer(m_device, uploadBuffers[i], nullptr);
	}
}

void VulkanMenuRenderer::ResizeWindow(uint32_t width, uint32_t height)
{

	if (width > 0 && height > 0 && (m_swapChainRebuild || m_windowData.Width != width || m_windowData.Height != height))
	{
		ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physicalDevice, m_device, &m_windowData, m_queueFamily, nullptr, width, height, 2, 0);
		m_windowData.FrameIndex = 0;
		m_swapChainRebuild = false;

		m_textureData.m_nWidth = width;
		m_textureData.m_nHeight = height;
	}
}

bool VulkanMenuRenderer::RenderMenu(bool bRenderOffscreen)
{
	ImGui::Render();

	VkSemaphore imageSemaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].ImageAcquiredSemaphore;
	VkSemaphore renderSemaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].RenderCompleteSemaphore;

	VkResult result;

	if (bRenderOffscreen)
	{
		m_windowData.FrameIndex = (m_windowData.FrameIndex + 1) % m_windowData.Frames.size();
	}
	else
	{
		result = vkAcquireNextImageKHR(m_device, m_windowData.Swapchain, UINT64_MAX, imageSemaphore, VK_NULL_HANDLE, &m_windowData.FrameIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_logger->error("VK_ERROR_OUT_OF_DATE_KHR: {}", m_windowData.FrameIndex);
			m_swapChainRebuild = true;
			return false;
		}
		else if (result == VK_SUBOPTIMAL_KHR)
		{
			g_logger->error("VK_SUBOPTIMAL_KHR: {}", m_windowData.FrameIndex);
			m_swapChainRebuild = true;
		}
		else if (result == VK_NOT_READY)
		{
			g_logger->error("Semaphore not ready: {}", m_windowData.SemaphoreIndex);
		}
		else if (result != VK_SUCCESS)
		{
			g_logger->error("vkAcquireNextImageKHR failed: {}", static_cast<int32_t>(result));
			return false;
		}
	}

	ImGui_ImplVulkanH_Frame* frameData = &m_windowData.Frames[m_windowData.FrameIndex];

	result = vkWaitForFences(m_device, 1, &frameData->Fence, VK_TRUE, UINT64_MAX);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkWaitForFences failed: {}", static_cast<int32_t>(result));
		return false;
	}

	result = vkResetFences(m_device, 1, &frameData->Fence);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkResetFences failed: {}", static_cast<int32_t>(result));
		return false;
	}

	result = vkResetCommandPool(m_device, frameData->CommandPool, 0);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkResetCommandPool failed: {}", static_cast<int32_t>(result));
		return false;
	}

	VkCommandBufferBeginInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer(frameData->CommandBuffer, &bufferInfo);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkBeginCommandBuffer failed: {}", static_cast<int32_t>(result));
		return false;
	}

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = m_windowData.RenderPass;
	passInfo.framebuffer = frameData->Framebuffer;
	passInfo.renderArea.extent.width = m_windowData.Width;
	passInfo.renderArea.extent.height = m_windowData.Height;
	passInfo.clearValueCount = 1;
	passInfo.pClearValues = &m_windowData.ClearValue;

	vkCmdBeginRenderPass(frameData->CommandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);


	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameData->CommandBuffer);


	vkCmdEndRenderPass(frameData->CommandBuffer);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	submitInfo.pWaitDstStageMask = &wait_stage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frameData->CommandBuffer;

	if (!bRenderOffscreen)
	{
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &imageSemaphore;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderSemaphore;
	}

	result = vkEndCommandBuffer(frameData->CommandBuffer);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkEndCommandBuffer failed: {}", static_cast<int32_t>(result));
		return false;
	}

	result = vkQueueSubmit(m_queue, 1, &submitInfo, frameData->Fence);
	if (result != VK_SUCCESS)
	{
		g_logger->error("vkQueueSubmit failed: {}", static_cast<int32_t>(result));
		return false;
	}

	if (m_swapChainRebuild)
	{
		return false;
	}

	if (!bRenderOffscreen)
	{
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
			return false;
		}
		else if (result == VK_SUBOPTIMAL_KHR)
		{
			m_swapChainRebuild = true;
		}
		else if (result != VK_SUCCESS)
		{
			g_logger->error("vkQueuePresentKHR failed: {}", static_cast<int32_t>(result));
			return false;
		}
	}
	else
	{
		result = vkWaitForFences(m_device, 1, &frameData->Fence, VK_TRUE, 1000 * 1000 * 8);
		if (result != VK_SUCCESS)
		{
			g_logger->error("vkWaitForFences failed: {}", static_cast<int32_t>(result));
			return false;
		}
	}

	m_windowData.SemaphoreIndex = (m_windowData.SemaphoreIndex + 1) % m_windowData.SemaphoreCount;

	m_textureData.m_nImage = reinterpret_cast<uint64_t>(frameData->Backbuffer);

	return true;
}

void VulkanMenuRenderer::WaitDeinitImGui()
{
	vkDeviceWaitIdle(m_device);

	ImGui_ImplVulkan_Shutdown();
}

void VulkanMenuRenderer::CleanupRenderer()
{
	for (int i = 0; i < 4; i++)
	{
		vkDestroyImage(m_device, *reinterpret_cast<VkImage*>(&m_thumbnailData[i].TextureData.m_nImage), nullptr);
		vkFreeMemory(m_device, m_thumbnailData[i].ImageMemory, nullptr);
	}
	ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_windowData, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_instance, nullptr);
}

