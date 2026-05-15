
#include "pch.h"
#include "async_renderer.h"
#include "mathutil.h"
#include "renderdoc_app.h"

#include "passthrough_renderer.h"

#include "shaders\fill_holes_vulkan_cs.spv.h"
#include "shaders\joint_bilateral_vulkan_cs.spv.h"

struct alignas(16) CSAsyncConstantBuffer
{
	int32_t g_disparitySize[2];
	float minDisparity;
	float maxDisparity;
	uint32_t bHoleFillLastPass;

	float bilateralDispCutoff;
	uint32_t bilateralDistance;
	uint32_t bUseInputConfidence;
};

#define MAX_FILTER_DIST 10
#define WEIGHT_ARRAY_SIZE (MAX_FILTER_DIST * 2 + 1)

struct CSFilterKernels
{
	float lumaWeights[256];
	float spaceWeights[WEIGHT_ARRAY_SIZE][WEIGHT_ARRAY_SIZE];
};


static PFN_vkGetMemoryWin32HandleKHR _vkGetMemoryWin32HandleKHR = nullptr;
static PFN_vkGetMemoryWin32HandlePropertiesKHR _vkGetMemoryWin32HandlePropertiesKHR = nullptr;
static PFN_vkCmdInsertDebugUtilsLabelEXT _vkCmdInsertDebugUtilsLabelEXT = nullptr;

static RENDERDOC_API_1_7_0* g_renderDocAPI = nullptr;
static bool g_bRenderDocCaptured = false;


static void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = 0;

	VkPipelineStageFlags srcStageMask = 0;
	VkPipelineStageFlags dstStageMask = 0;
	VkDependencyFlags depFlags = 0;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		//dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		&& newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		//dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		//srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		//dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_GENERAL)
	{
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		depFlags = 0;
	}
	else
	{
		g_logger->error("Unknown layout transition!");
		return;
	}

	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, depFlags, 0, nullptr, 0, nullptr, 1, &barrier);
}



void CopyTextureToGPU(VkCommandBuffer commandBuffer, VulkanTexture texture, VkImageLayout newLayout)
{

	TransitionImage(commandBuffer, texture.Image, texture.Layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	texture.Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { texture.Extent.width, texture.Extent.height, 1 };

	vkCmdCopyBufferToImage(commandBuffer, texture.StagingBuffer, texture.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	TransitionImage(commandBuffer, texture.Image, texture.Layout, newLayout);
	texture.Layout = newLayout;
}

void CopyBufferTextureToGPU(VkCommandBuffer commandBuffer, VulkanBufferTexture texture)
{
	VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	barrier.buffer = texture.Buffer;
	barrier.size = VK_WHOLE_SIZE;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 1, &barrier, 0, nullptr);

	VkBufferCopy region{};
	region.size = texture.Size;

	vkCmdCopyBuffer(commandBuffer, texture.StagingBuffer, texture.Buffer, 1, &region);

	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}


AsyncRenderer::~AsyncRenderer()
{
	if (!m_device || !m_queue)
	{
		return;
	}

	if (!vkQueueWaitIdle(m_queue))
	{
		return;
	}

	DestroyTexture(m_cameraTexture);
	DestroyTexture(m_disparityTexture);
	DestroyTexture(m_confidenceTexture);
	for (int i = 0; i < 3; i++)
	{
		DestroyTexture(m_outputTexture[i]);
	}

	for (std::function<void()> deleteFunc : m_deletionQueue)
	{
		deleteFunc();
	}
}





bool AsyncRenderer::InitRenderer()
{
	if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI =(pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
		int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&g_renderDocAPI);
		if (ret == 1)
		{
			g_logger->info("RenderDoc API enabled for async renderer");
		}
		else
		{
			g_logger->error("Failed to initalize RenderDoc API: {}", ret);
		}
	}

	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_4;

	std::vector<const char*> validationLayers;
	validationLayers.push_back("VK_LAYER_KHRONOS_validation");

	std::vector<const char*> instanceExtensions;
	instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
	if (g_renderDocAPI)
	{
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
	createInfo.ppEnabledExtensionNames = instanceExtensions.data();

	if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
	{
		g_logger->error("vkCreateInstance failure!");
		return false;
	}

	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	if (deviceCount < 1)
	{
		g_logger->error("Failed to enumerate Vulkan devices!");
		return false;
	}

	bool bFoundPhysDevice = false;

	uint64_t gpuLUID = m_baseRenderer->GetRenderDeviceLUID();

	for (uint32_t i = 0; i < deviceCount; i++)
	{
		VkPhysicalDeviceProperties2 deviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceIDProperties deviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		deviceProps.pNext = &deviceIDProps;
		vkGetPhysicalDeviceProperties2(devices[i], &deviceProps);

		if (gpuLUID == *reinterpret_cast<uint64_t*>(deviceIDProps.deviceLUID))
		{
			m_physDevice = devices[i];
			bFoundPhysDevice = true;
			break;
		}
	}

	if (!bFoundPhysDevice)
	{
		g_logger->error("No matching Vulkan physical device found!");
		return false;
	}


	vkGetPhysicalDeviceMemoryProperties(m_physDevice, &m_memProps);

#if 0
	for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++)
	{
		VkMemoryHeap& heap = m_memProps.memoryHeaps[m_memProps.memoryTypes[i].heapIndex];


		g_logger->info("Memory {}:", i);
		g_logger->info("\tHeap {}: {} MB, Flags: 0x{:x}", m_memProps.memoryTypes[i].heapIndex, heap.size / (1024 * 1024), heap.flags);
		g_logger->info("\tFlags 0x{:x}", static_cast<uint32_t>(m_memProps.memoryTypes[i].propertyFlags));
		if(m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		{
			g_logger->info("\tDEVICE_LOCAL");
		}
		if (m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			g_logger->info("\tHOST_VISIBLE");
		}
		if (m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		{
			g_logger->info("\tHOST_COHERENT");
		}
	}
#endif

	uint32_t familyPropsCount = 0;

	vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &familyPropsCount, nullptr);

	std::vector<VkQueueFamilyProperties> familyProps;
	familyProps.resize(familyPropsCount);

	vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &familyPropsCount, familyProps.data());

	bool bFoundQueue = false;

	for (uint32_t i = 0; i < familyPropsCount; i++)
	{
		// Prioritize queues without graphics
		if ((familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			(familyProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			(familyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
		{
			bFoundQueue = true;
			g_logger->warn("Using queue {}", i);
			m_queueFamilyIndex = i;
			break;
		}
		else if (!bFoundQueue && 
			(familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			(familyProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
		{
			bFoundQueue = true;
			m_queueFamilyIndex = i;
		}
	}

	if (!bFoundQueue)
	{
		g_logger->error("No valid Vulkan device queues found!");
		return false;
	}

	float queuePriority = 0.0;

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueCount = 1;
	queueInfo.queueFamilyIndex = m_queueFamilyIndex;
	queueInfo.pQueuePriorities = &queuePriority;

	std::vector<const char*> deviceExtensions;
	//deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);

	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (vkCreateDevice(m_physDevice, &deviceInfo, nullptr, &m_device) != VK_SUCCESS)
	{
		g_logger->error("vkCreateDevice failure!");
		return false;
	}

	_vkGetMemoryWin32HandleKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandleKHR"));
	_vkGetMemoryWin32HandlePropertiesKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandlePropertiesKHR"));
	_vkCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetDeviceProcAddr(m_device, "vkCmdInsertDebugUtilsLabelEXT"));
	
	vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);


	VkCommandPoolCreateInfo poolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolCreateInfo.queueFamilyIndex = m_queueFamilyIndex;

	if (vkCreateCommandPool(m_device, &poolCreateInfo, nullptr, &m_commandPool) != VK_SUCCESS)
	{
		g_logger->error("vkCreateCommandPool failure!");
		return false;
	}

	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS)
	{
		g_logger->error("vkAllocateCommandBuffers failure!");
		vkDestroyCommandPool(m_device, m_commandPool, nullptr);
		return false;
	}

	m_disparityFillHolesCS = CreateShaderModule(g_FillHolesCS, ARRAYSIZE(g_FillHolesCS) * sizeof(g_FillHolesCS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_disparityFillHolesCS, nullptr); });

	m_disparityJointBilateralCS = CreateShaderModule(g_JointBilateralCS, ARRAYSIZE(g_JointBilateralCS) * sizeof(g_JointBilateralCS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_disparityJointBilateralCS, nullptr); });



	if (!CreateBuffer(m_filterKernelBuffer, m_filterKernelBufferMem, sizeof(CSFilterKernels), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &m_deletionQueue))
	{
		g_logger->error("m_filterKernelBuffer creation failure!");
		return false;
	}


	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_renderFence) != VK_SUCCESS)
	{
		g_logger->error("vkCreateFence failure!");
		return false;
	}

	VkDescriptorPoolSize poolSizes[3] =
	{
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}
	};

	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
	{
		g_logger->error("vkCreateDescriptorPool failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); });

	VkDescriptorSetLayoutBinding layoutBindings[5] =
	{
		{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
	};


	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 5;
	layoutInfo.pBindings = layoutBindings;

	if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorLayout) != VK_SUCCESS)
	{
		g_logger->error("vkCreateDescriptorSetLayout failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr); });

	VkDescriptorSetAllocateInfo descAllocInfo{};
	descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descAllocInfo.descriptorPool = m_descriptorPool;
	descAllocInfo.descriptorSetCount = 1;
	descAllocInfo.pSetLayouts = &m_descriptorLayout;

	if (vkAllocateDescriptorSets(m_device, &descAllocInfo, &m_descriptorSet) != VK_SUCCESS)
	{
		g_logger->error("vkAllocateDescriptorSets failure!");
		return false;
	}


	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
	{
		g_logger->error("vkCreateSampler failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroySampler(m_device, m_sampler, nullptr); });



	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(CSAsyncConstantBuffer);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorLayout;

	if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
	{
		g_logger->error("vkCreatePipelineLayout failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); });


	VkPipelineShaderStageCreateInfo shaderInfoFillHolesCS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoFillHolesCS.module = m_disparityFillHolesCS;
	shaderInfoFillHolesCS.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderInfoFillHolesCS.pName = "main";

	VkPipelineShaderStageCreateInfo shaderInfoJointBilateralCS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoJointBilateralCS.module = m_disparityJointBilateralCS;
	shaderInfoJointBilateralCS.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderInfoJointBilateralCS.pName = "main";


	VkComputePipelineCreateInfo pipelineInfoFillHoles{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfoFillHoles.stage = shaderInfoFillHolesCS;
	pipelineInfoFillHoles.flags = 0;
	pipelineInfoFillHoles.layout = m_pipelineLayout;

	VkComputePipelineCreateInfo pipelineInfoJointBilateral{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfoJointBilateral.stage = shaderInfoJointBilateralCS;
	pipelineInfoJointBilateral.flags = 0;
	pipelineInfoJointBilateral.layout = m_pipelineLayout;

	std::vector<VkComputePipelineCreateInfo> pipelineInfos{ pipelineInfoFillHoles, pipelineInfoJointBilateral };

	std::vector<VkPipeline> pipelines;
	pipelines.resize(pipelineInfos.size());

	if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, (uint32_t)pipelineInfos.size(), pipelineInfos.data(), nullptr, pipelines.data()) != VK_SUCCESS)
	{
		g_logger->error("vkCreateComputePipelines failure!");
		return false;
	}

	m_pipelineFillHoles = pipelines[0];
	m_pipelineJointBilateral = pipelines[1];

	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineFillHoles, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineJointBilateral, nullptr); });

	g_logger->info("Asynchronous depth renderer initialized");

	return true;
}


bool AsyncRenderer::CreateBuffer(VkBuffer& buffer, VkDeviceMemory& bufferMem, VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, std::deque<std::function<void()>>* deletionQueue)
{
	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.usage = usageFlags;
	bufferInfo.size = bufferSize;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer);

	VkMemoryRequirements memReq{};
	vkGetBufferMemoryRequirements(m_device, buffer, &memReq);

	for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryTypeBits & (1 << i))
			&& (m_memProps.memoryTypes[i].propertyFlags & memFlags))
		{
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = i;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(m_device, buffer, nullptr);
				return false;
			}

			break;
		}
	}

	if (!bufferMem)
	{
		vkDestroyBuffer(m_device, buffer, nullptr);
		return false;
	}

	if (deletionQueue)
	{
		deletionQueue->push_back([=]() { vkDestroyBuffer(m_device, buffer, nullptr); });
		deletionQueue->push_back([=]() { vkFreeMemory(m_device, bufferMem, nullptr); });
	}

	vkBindBufferMemory(m_device, buffer, bufferMem, 0);

	return true;
}

bool AsyncRenderer::CreateBufferTexture(VulkanBufferTexture& texture, VkExtent2D extent, VkDeviceSize bufferSize)
{
	DestroyBufferTexture(texture);

	texture.Size = bufferSize;
	texture.Extent = extent;

	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	bufferInfo.size = bufferSize;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateBuffer(m_device, &bufferInfo, nullptr, &texture.Buffer);

	VkMemoryRequirements memReq{};
	vkGetBufferMemoryRequirements(m_device, texture.Buffer, &memReq);

	bool bFoundHostVisibleMem = false;
	bool bFoundDeviceOnlyMem = false;
	uint32_t memType = 0;

	for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++)
	{
		if (memReq.memoryTypeBits & (1 << i))
		{
			/*if (m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			{
				memType = i;
				bFoundHostVisibleMem = true;
				texture.bSupportsDirectTransfer = true;
				break;
			}
			else */if (m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			{
				memType = i;
				bFoundDeviceOnlyMem = true;
				break;
			}
		}
	}

	if (!bFoundHostVisibleMem && !bFoundDeviceOnlyMem)
	{
		g_logger->error("Failed to find suitable memory type for buffer!");
		return false;
	}

	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = memType;

	if (vkAllocateMemory(m_device, &allocInfo, nullptr, &texture.Memory) != VK_SUCCESS)
	{
		g_logger->error("vkAllocateMemory failed!");
		return false;
	}

	if (vkBindBufferMemory(m_device, texture.Buffer, texture.Memory, 0) != VK_SUCCESS)
	{
		g_logger->error("vkBindBufferMemory failed!");
		return false;
	}


	VkBufferViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
	viewInfo.buffer = texture.Buffer;
	viewInfo.format = VK_FORMAT_R16_SNORM;
	viewInfo.offset = 0;
	viewInfo.range = memReq.size;

	if (vkCreateBufferView(m_device, &viewInfo, nullptr, &texture.View) != VK_SUCCESS)
	{
		g_logger->error("vkCreateBufferView failure!");
		return false;
	}


	// Create staging buffer if the texture will be updated from the CPU.
	if (!bFoundHostVisibleMem)
	{
		texture.bSupportsDirectTransfer = false;

		if (!CreateBuffer(texture.StagingBuffer, texture.StagingBufferMemory, memReq.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, nullptr))
		{
			g_logger->error("Staging buffer creation failure!");
			return false;
		}

		VkResult ret = vkMapMemory(m_device, texture.StagingBufferMemory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&texture.MappedMemory));
		if (ret != VK_SUCCESS)
		{
			g_logger->error("Failed to map staging buffer! {}", (int32_t)ret);
			return false;
		}
	}
	else
	{
		VkResult ret = vkMapMemory(m_device, texture.Memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&texture.MappedMemory));
		if (ret != VK_SUCCESS)
		{
			g_logger->error("Failed to map buffer! {}", (int32_t)ret);
			return false;
		}

		g_logger->info("Created buffer texture with host accessible memory");
	}

	texture.bIsValid = true;

	return true;
}


bool AsyncRenderer::CreateTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags, VkImageCreateFlags createFlags, VkMemoryPropertyFlags memFlags)
{
	DestroyTexture(texture);

	texture.Extent = extent;
	texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = extent.width;
	imageInfo.extent.height = extent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usageFlags;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = createFlags;

	if (vkCreateImage(m_device, &imageInfo, nullptr, &texture.Image) != VK_SUCCESS)
	{
		g_logger->error("vkCreateImage failure!");
		return false;
	}

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_device, texture.Image, &memReq);

	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocInfo.allocationSize = memReq.size;

	bool bFoundMemoryType = false;
	for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryTypeBits & (1 << i)) && ((m_memProps.memoryTypes[i].propertyFlags & memFlags) == memFlags))
		{
			allocInfo.memoryTypeIndex = i;
			bFoundMemoryType = true;
			break;
		}
	}

	if (!bFoundMemoryType)
	{
		g_logger->error("Failed to find suitable memory type for image!");
		return false;
	}

	if (vkAllocateMemory(m_device, &allocInfo, nullptr, &texture.Memory) != VK_SUCCESS)
	{
		g_logger->error("vkAllocateMemory failure!");
		return false;
	}

	if (vkBindImageMemory(m_device, texture.Image, texture.Memory, 0) != VK_SUCCESS)
	{
		g_logger->error("vkBindImageMemory failure!");
		return false;
	}

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = texture.Image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture.View) != VK_SUCCESS)
	{
		g_logger->error("vkCreateImageView failure!");
		return false;
	}

	// Create staging buffer if the texture will be updated from the CPU.
	if (usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	{
		if (!CreateBuffer(texture.StagingBuffer, texture.StagingBufferMemory, memReq.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, nullptr))
		{
			g_logger->error("Staging buffer creation failure!");
			return false;
		}

		if (vkMapMemory(m_device, texture.StagingBufferMemory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&texture.MappedMemory)) != VK_SUCCESS)
		{
			g_logger->error("Failed to map staging buffer!");
			return false;
		}
	}

	texture.bIsValid = true;
	return true;
}


bool AsyncRenderer::CreateSharedTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format)
{
	DestroyTexture(texture);

	texture.Extent = extent;
	texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	
	if (!m_baseRenderer->CreateSharedDisparityMap(&texture.SharedHandle, &texture.d3dTexture, extent, format))
	{
		return false;
	}

	VkExternalMemoryImageCreateInfo  extInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
	extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.pNext = &extInfo;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = extent.width;
	imageInfo.extent.height = extent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = 0;

	if (vkCreateImage(m_device, &imageInfo, nullptr, &texture.Image) != VK_SUCCESS)
	{
		g_logger->error("Async shared texture: vkCreateImage failure!");
		return false;
	}

	VkImageMemoryRequirementsInfo2 reqInfo{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	reqInfo.image = texture.Image;

	VkMemoryDedicatedRequirements dedReq{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };

	VkMemoryRequirements2 memReq{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	memReq.pNext = &dedReq;
	vkGetImageMemoryRequirements2(m_device, &reqInfo, &memReq);


	VkMemoryWin32HandlePropertiesKHR handleProps{ VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };

	VkResult res2 = _vkGetMemoryWin32HandlePropertiesKHR(m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT, texture.SharedHandle, &handleProps);
	if (res2 != VK_SUCCESS)
	{
		g_logger->error("vkGetMemoryWin32HandlePropertiesKHR failure: {}", (int32_t)res2);
		return false;
	}

	VkMemoryDedicatedAllocateInfo dedicatedInfo{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedInfo.image = texture.Image;

	VkImportMemoryWin32HandleInfoKHR handleInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
	handleInfo.pNext = dedReq.prefersDedicatedAllocation ? &dedicatedInfo : nullptr;
	handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
	handleInfo.handle = texture.SharedHandle;

	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocInfo.allocationSize = memReq.memoryRequirements.size;
	allocInfo.pNext = &handleInfo;

	bool bFoundMemoryType = false;
	for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryRequirements.memoryTypeBits & (1 << i)) &&
			(m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			allocInfo.memoryTypeIndex = i;
			bFoundMemoryType = true;
			break;
		}
	}

	if (!bFoundMemoryType)
	{
		g_logger->error("Async shared texture: Failed to find suitable memory type for image!");
		return false;
	}

	VkResult res = vkAllocateMemory(m_device, &allocInfo, nullptr, &texture.Memory);
	if (res != VK_SUCCESS)
	{
		g_logger->error("Async shared texture: vkAllocateMemory failure! {}", (int32_t)res);

		return false;
	}

	if (vkBindImageMemory(m_device, texture.Image, texture.Memory, 0) != VK_SUCCESS)
	{
		g_logger->error("Async shared texture: vkBindImageMemory failure!");
		return false;
	}

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = texture.Image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture.View) != VK_SUCCESS)
	{
		g_logger->error("Async shared texture: vkCreateImageView failure!");
		return false;
	}

	texture.bIsValid = true;
	return true;
}


void AsyncRenderer::DestroyTexture(VulkanTexture& texture)
{
	texture.bIsValid = false;

	if (texture.SharedHandle != INVALID_HANDLE_VALUE)
	{
		texture.SharedHandle = INVALID_HANDLE_VALUE;
	}
	
	texture.d3dTexture = nullptr;

	if (texture.View != VK_NULL_HANDLE)
	{
		vkDestroyImageView(m_device, texture.View, nullptr);
		texture.View = VK_NULL_HANDLE;
	}
	if (texture.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_device, texture.Memory, nullptr);
		texture.Memory = VK_NULL_HANDLE;
	}
	if (texture.Image != VK_NULL_HANDLE)
	{
		vkDestroyImage(m_device, texture.Image, nullptr);
		texture.Image = VK_NULL_HANDLE;
	}

	if (texture.StagingBufferMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_device, texture.StagingBufferMemory, nullptr);
		texture.MappedMemory = nullptr;
		texture.StagingBufferMemory = VK_NULL_HANDLE;
	}
	if (texture.StagingBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_device, texture.StagingBuffer, nullptr);
		texture.StagingBuffer = VK_NULL_HANDLE;
	}
}

void AsyncRenderer::DestroyBufferTexture(VulkanBufferTexture& texture)
{
	texture.bIsValid = false;


	if (texture.View != VK_NULL_HANDLE)
	{
		vkDestroyBufferView(m_device, texture.View, nullptr);
		texture.View = VK_NULL_HANDLE;
	}
	if (texture.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_device, texture.Memory, nullptr);
		texture.Memory = VK_NULL_HANDLE;
	}
	if (texture.Buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_device, texture.Buffer, nullptr);
		texture.Buffer = VK_NULL_HANDLE;
	}

	if (texture.StagingBufferMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_device, texture.StagingBufferMemory, nullptr);
		texture.MappedMemory = nullptr;
		texture.StagingBufferMemory = VK_NULL_HANDLE;
	}
	if (texture.StagingBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_device, texture.StagingBuffer, nullptr);
		texture.StagingBuffer = VK_NULL_HANDLE;
	}
}





bool AsyncRenderer::BeginRender(std::shared_ptr<DepthFrame> depthFrame)
{
	int textureIndex = depthFrame->disparityTextureIndex;

	if (!depthFrame.get() || !depthFrame->bIsValid)
	{
		return false;
	}

	if (!m_disparityTexture.bIsValid ||
		m_disparityTexture.Extent.width != depthFrame->disparityTextureSize[0] ||
		m_disparityTexture.Extent.height != depthFrame->disparityTextureSize[1])
	{
		VkExtent2D extent = { depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1] };
		VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

		if (!CreateTexture(m_disparityTexture, extent, VK_FORMAT_R16_SNORM, usageFlags, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			g_logger->error("Failed to create m_disparityTexture!");
			return false;
		}

		if (!CreateTexture(m_confidenceTexture, extent, VK_FORMAT_R16_SNORM, usageFlags, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			g_logger->error("Failed to create m_confidenceTexture!");
			return false;
		}
	}

	// TODO output texture scaling
	if (!m_outputTexture[textureIndex].bIsValid ||
		m_outputTexture[textureIndex].Extent.width != depthFrame->disparityTextureSize[0] ||
		m_outputTexture[textureIndex].Extent.height != depthFrame->disparityTextureSize[1])
	{
		if (!CreateSharedTexture(m_outputTexture[textureIndex], { depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1] }, VK_FORMAT_R16G16_SNORM))
		{
			g_logger->error("Failed to create m_outputTexture {}!", textureIndex);
			return false;
		}

		depthFrame->outputDisparityMapNativeTexture = m_outputTexture[textureIndex].d3dTexture;

		g_logger->info("Created m_outputTexture {}", textureIndex);
	}

	if (!m_cameraTexture.bIsValid || m_cameraTexture.Extent.width != depthFrame->cameraFrameTextureSize[0] || m_cameraTexture.Extent.height != depthFrame->cameraFrameTextureSize[1])
	{
		if (!CreateTexture(m_cameraTexture, { depthFrame->cameraFrameTextureSize[0] , depthFrame->cameraFrameTextureSize[1] }, VK_FORMAT_R8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			g_logger->error("Failed to create m_cameraTexture!");
			return false;
		}
	}

	m_minDisparity = depthFrame->minDisparity;
	m_maxDisparity = depthFrame->maxDisparity;

	/*if (g_renderDocAPI && !g_bRenderDocCaptured)
	{
		g_renderDocAPI->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_instance), NULL);
	}*/
	
	vkResetFences(m_device, 1, &m_renderFence);

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = 0;

	vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

	return true;
}


uint16_t* AsyncRenderer::GetDisparityWriteBuffer()
{
	return reinterpret_cast<uint16_t*>(m_disparityTexture.MappedMemory);
}

uint16_t* AsyncRenderer::GetConfidenceWriteBuffer()
{
	return reinterpret_cast<uint16_t*>(m_confidenceTexture.MappedMemory);
}

uint8_t* AsyncRenderer::GetCameraWriteBuffer()
{
	return m_cameraTexture.MappedMemory;
}


void AsyncRenderer::Render(std::shared_ptr<DepthFrame> depthFrame)
{
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	int textureIndex = depthFrame->disparityTextureIndex;

	CopyTextureToGPU(m_commandBuffer, m_disparityTexture, VK_IMAGE_LAYOUT_GENERAL);
	CopyTextureToGPU(m_commandBuffer, m_confidenceTexture, VK_IMAGE_LAYOUT_GENERAL);
	CopyTextureToGPU(m_commandBuffer, m_cameraTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	

	{
		VkDescriptorBufferInfo filterBufferInfo{};
		filterBufferInfo.buffer = m_filterKernelBuffer;
		filterBufferInfo.offset = 0;
		filterBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorImageInfo dispImageInfo{};
		dispImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		dispImageInfo.imageView = m_disparityTexture.View;
		dispImageInfo.sampler = VK_NULL_HANDLE;

		VkDescriptorImageInfo confImageInfo{};
		confImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		confImageInfo.imageView = m_confidenceTexture.View;
		confImageInfo.sampler = VK_NULL_HANDLE;

		VkDescriptorImageInfo cameraImageInfo{};
		cameraImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		cameraImageInfo.imageView = m_cameraTexture.View;
		cameraImageInfo.sampler = m_sampler;

		VkDescriptorImageInfo outputImageInfo{};
		outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputImageInfo.imageView = m_outputTexture[textureIndex].View;
		outputImageInfo.sampler = VK_NULL_HANDLE;


		std::vector<VkWriteDescriptorSet> descriptorWrite;

		VkWriteDescriptorSet desc{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		desc.dstSet = m_descriptorSet;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc.descriptorCount = 1;
		desc.pBufferInfo = &filterBufferInfo;
		descriptorWrite.push_back(desc);

		desc.dstBinding = 1;
		desc.dstArrayElement = 0;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		desc.descriptorCount = 1;
		desc.pImageInfo = &dispImageInfo;
		descriptorWrite.push_back(desc);

		desc.dstBinding = 2;
		desc.dstArrayElement = 0;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		desc.descriptorCount = 1;
		desc.pImageInfo = &confImageInfo;
		descriptorWrite.push_back(desc);

		desc.dstBinding = 3;
		desc.dstArrayElement = 0;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.descriptorCount = 1;
		desc.pImageInfo = &cameraImageInfo;
		descriptorWrite.push_back(desc);

		desc.dstBinding = 4;
		desc.dstArrayElement = 0;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		desc.descriptorCount = 1;
		desc.pImageInfo = &outputImageInfo;
		descriptorWrite.push_back(desc);

		vkUpdateDescriptorSets(m_device, (uint32_t)descriptorWrite.size(), descriptorWrite.data(), 0, nullptr);

		vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
	}


	CSAsyncConstantBuffer constants = {};

	constants.g_disparitySize[0] = depthFrame->disparityTextureSize[0];
	constants.g_disparitySize[1] = depthFrame->disparityTextureSize[1];
	constants.minDisparity = depthFrame->minDisparity;
	constants.maxDisparity = depthFrame->maxDisparity;
	constants.bilateralDistance = stereoConf.StereoFBS_Iterations;
	constants.bilateralDispCutoff = stereoConf.StereoFBS_Lambda / 255.0f * (m_maxDisparity - m_minDisparity);
	constants.bHoleFillLastPass = false;
	constants.bUseInputConfidence = stereoConf.StereoFiltering != StereoFiltering_None;


	if (stereoConf.StereoDrawBackground && 
		(m_bilateralDistance != stereoConf.StereoFBS_Iterations ||
		m_bilateralSigmaSpace != stereoConf.StereoFBS_Spatial ||
		m_bilateralSigmaLuma != stereoConf.StereoFBS_Luma / 250.0f))
	{
		// TODO Add proper values to config
		m_bilateralDistance = stereoConf.StereoFBS_Iterations;
		m_bilateralSigmaSpace = stereoConf.StereoFBS_Spatial;
		m_bilateralSigmaLuma = stereoConf.StereoFBS_Luma / 250.0f;

		ComputeFilterKernels();
	}


	if (m_outputTexture[textureIndex].Layout != VK_IMAGE_LAYOUT_GENERAL)
	{
		TransitionImage(m_commandBuffer, m_outputTexture[textureIndex].Image, m_outputTexture[textureIndex].Layout, VK_IMAGE_LAYOUT_GENERAL);
		m_outputTexture[textureIndex].Layout = VK_IMAGE_LAYOUT_GENERAL;
	}

	if (stereoConf.StereoFillHoles)
	{
		int groupCountX = DivRoundUp(depthFrame->disparityTextureSize[0], 32);
		int groupCountY = DivRoundUp(depthFrame->disparityTextureSize[1], 32);

		vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CSAsyncConstantBuffer), &constants);

		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineFillHoles);

		for (int i = 0; i < 7; i++)
		{
			vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);

			TransitionImage(m_commandBuffer, m_confidenceTexture.Image, m_confidenceTexture.Layout, VK_IMAGE_LAYOUT_GENERAL);
			m_confidenceTexture.Layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		constants.bHoleFillLastPass = true;
	}

	if (stereoConf.StereoDrawBackground)
	{
		int groupCountX = DivRoundUp(depthFrame->disparityTextureSize[0] * stereoConf.StereoDepthMapScale, 32);
		int groupCountY = DivRoundUp(depthFrame->disparityTextureSize[1] * stereoConf.StereoDepthMapScale, 32);

		vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CSAsyncConstantBuffer), &constants);

		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineJointBilateral);
		vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);
	}
	else
	{
		int groupCountX = DivRoundUp(depthFrame->disparityTextureSize[0] * stereoConf.StereoDepthMapScale, 32);
		int groupCountY = DivRoundUp(depthFrame->disparityTextureSize[1] * stereoConf.StereoDepthMapScale, 32);

		constants.bHoleFillLastPass = true;

		vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CSAsyncConstantBuffer), &constants);

		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineFillHoles);
		vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);
	}

	// Add a RenderDoc frame end marker to allow captures from the UI.
	if (g_renderDocAPI)
	{
		VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = "vr-marker,frame_end,type,application";
		_vkCmdInsertDebugUtilsLabelEXT(m_commandBuffer, &label);
	}


	vkEndCommandBuffer(m_commandBuffer);

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer;

	vkQueueSubmit(m_queue, 1, &submitInfo, m_renderFence);

	VkResult res = vkWaitForFences(m_device, 1, &m_renderFence, true, 1000 * 1000 * 100);
	if (res == VK_TIMEOUT)
	{
		g_logger->warn("vkWaitForFences timeout!");
	}
	if (res != VK_SUCCESS)
	{
		g_logger->error("vkWaitForFences failure: {}", (int32_t)res);
	}


	/*if (g_renderDocAPI && !g_bRenderDocCaptured)
	{
		g_bRenderDocCaptured = g_renderDocAPI->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_instance), NULL);
	}*/
}


VkShaderModule AsyncRenderer::CreateShaderModule(const uint32_t* bytecode, size_t codeSize)
{
	VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = codeSize;
	createInfo.pCode = bytecode;

	VkShaderModule module;

	if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS)
	{
		g_logger->error("vkCreateShaderModule failure!");
		return nullptr;
	}

	return module;
}

bool AsyncRenderer::TransferTextureToCPU(void* textureSRV, const uint32_t width, const uint32_t height, const uint32_t bufferSize, uint8_t* buffer)
{
	return false;
}


void AsyncRenderer::ComputeFilterKernels()
{
	CSFilterKernels* bufferMemory;
	vkMapMemory(m_device, m_filterKernelBufferMem, 0, sizeof(CSFilterKernels), 0, reinterpret_cast<void**>(&bufferMemory));	

	float gaussLumaCoeff = -0.5f / (m_bilateralSigmaLuma * m_bilateralSigmaLuma);

	for (int i = 0; i < 256; i++)
	{
		float factor = float(i) / 256.0f;
		bufferMemory->lumaWeights[i] = exp(factor * factor * gaussLumaCoeff);
	}


	uint32_t radius = min(m_bilateralDistance, MAX_FILTER_DIST);
	int filterSize = radius * 2 + 1;

	float gaussSpaceCoeff = -0.5f / (m_bilateralSigmaSpace * m_bilateralSigmaSpace);

	for (int y = 0; y < filterSize; y++)
	{
		for (int x = 0; x < filterSize; x++)
		{
			int i = x - radius;
			int j = y - radius;

			float r2 = (float)(i * i + j * j);
			bufferMemory->spaceWeights[x][y] = exp(r2 * gaussSpaceCoeff);
		}
	}

	vkUnmapMemory(m_device, m_filterKernelBufferMem);
}
