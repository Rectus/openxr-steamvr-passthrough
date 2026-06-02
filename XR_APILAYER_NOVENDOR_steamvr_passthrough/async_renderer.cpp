
#include "pch.h"
#include "async_renderer.h"
#include "mathutil.h"
#include "renderdoc_app.h"
#include "pathutil.h"
#include "renderutil.h"
#include "vulkan_util.h"

#include "passthrough_renderer.h"

#include "shaders\fill_holes_vulkan_cs.spv.h"
#include "shaders\joint_bilateral_vulkan_cs.spv.h"



#define RENDERDOC_DLL_PATH_REG_KEY L"SOFTWARE\\Classes\\RenderDoc.RDCCapture.1\\DefaultIcon\\"

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
#define LUMA_WEIGHT_CLAMP 48

// Using the default sparse 16 byte aligned arrays for a bit of performance over packing them.
struct  CSFilterKernels
{
	float lumaWeights[LUMA_WEIGHT_CLAMP][4];
	float spaceWeights[MAX_FILTER_DIST][MAX_FILTER_DIST][4];
};


static PFN_vkGetMemoryWin32HandleKHR _vkGetMemoryWin32HandleKHR = nullptr;
static PFN_vkGetMemoryWin32HandlePropertiesKHR _vkGetMemoryWin32HandlePropertiesKHR = nullptr;
static PFN_vkCmdInsertDebugUtilsLabelEXT _vkCmdInsertDebugUtilsLabelEXT = nullptr;
static PFN_vkSetDebugUtilsObjectNameEXT _vkSetDebugUtilsObjectNameEXT = nullptr;

static RENDERDOC_API_1_7_0* g_renderDocAPI = nullptr;
static bool g_bRenderDocCaptured = false;


inline VkFormat CameraFrameFormatToVulkan(const ECameraFrameFormat in)
{
	switch (in)
	{
	case FrameFormat_RGBX32:
		return VK_FORMAT_R8G8B8A8_SRGB;
	case FrameFormat_RGB24:
		return VK_FORMAT_R8G8B8_SRGB;
	case FrameFormat_YUYV16:
		//return VK_FORMAT_G8B8G8R8_422_UNORM;
		return VK_FORMAT_R8G8B8A8_UNORM;
	case FrameFormat_NV12:
		return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	case FrameFormat_NV12_2:
		return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	case FrameFormat_BAYER16BG:
	case FrameFormat_MJPEG:
	case FrameFormat_RAW10:
	default:
		return VK_FORMAT_UNDEFINED;
	}
}


static void SetVulkanDebugName(const VkDevice device, const void* object, const VkObjectType type, const char* name)
{
	if (!_vkSetDebugUtilsObjectNameEXT)
	{
		return;
	}
	VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = type;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(object);
	nameInfo.pObjectName = name;

	_vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
}





AsyncRenderer::~AsyncRenderer()
{
	std::unique_lock destructLock(m_accessMutex);

	if (!m_device || !m_queue)
	{
		return;
	}

	if (!vkQueueWaitIdle(m_queue))
	{
		return;
	}

	m_frameDecoder.Deinit();

	DestroyTexture(m_bwRectifiedCameraTexture);
	DestroyTexture(m_disparityTexture);
	DestroyTexture(m_confidenceTexture);
	for (int i = 0; i < 3; i++)
	{
		DestroyTexture(m_rawCameraTexture[i]);
		DestroyTexture(m_sharedCameraTexture[i]);
		DestroyTexture(m_outputTexture[i]);
	}

	for (std::function<void()> deleteFunc : m_deletionQueue)
	{
		deleteFunc();
	}

	vkDestroyDevice(m_device, nullptr);
}





bool AsyncRenderer::InitRenderer()
{
	std::unique_lock initLock(m_accessMutex);
	if (m_bIsInitialized) { g_logger->error("m_bIsInitialized"); }

	Config_Main& mainConfig = m_configManager->GetConfig_Main();

	if (mainConfig.EnableRenderDocDebugging)
	{
		HMODULE module = nullptr;

		if (mainConfig.AutostartRenderDocInstance)
		{
			HKEY key;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RENDERDOC_DLL_PATH_REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
			{
				WCHAR path[256] = { 0 };
				DWORD pathSize = 255;

				LSTATUS ret = RegGetValueW(key, NULL, NULL, RRF_RT_REG_SZ, NULL, &path, &pathSize);
				if (ret == ERROR_SUCCESS)
				{
					std::filesystem::path dirPath(ToUTF8String(path));
					dirPath.replace_filename("renderdoc.dll");

					module = LoadLibraryW(ToWideString(dirPath.string()).data());
					if (module)
					{
						g_logger->info("Starting RenderDoc instance...");
					}
				}
				RegCloseKey(key);
			}
		}
		else
		{
			module = GetModuleHandleA("renderdoc.dll");
		}

		if (module)
		{
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(module, "RENDERDOC_GetAPI");
			int result = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&g_renderDocAPI);
			if (result == 1)
			{
				g_logger->info("RenderDoc API enabled for async renderer");
			}
			else
			{
				g_logger->warn("Failed to initalize RenderDoc API: {}", result);
			}
		}
	}
	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_4;

	std::vector<const char*> validationLayers;
	if (mainConfig.EnableAsyncVulkanValidation)
	{
		validationLayers.push_back("VK_LAYER_KHRONOS_validation");
	}

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

	uint64_t gpuLUID = m_inlineRenderer.lock()->GetRenderDeviceLUID();

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
			(familyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
			familyProps[i].queueCount >= 2)
		{
			bFoundQueue = true;
			m_queueFamilyIndex = i;
			break;
		}
		else if (!bFoundQueue &&
			(familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			(familyProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			familyProps[i].queueCount >= 2)
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
	queueInfo.queueCount = 2;
	queueInfo.queueFamilyIndex = m_queueFamilyIndex;
	queueInfo.pQueuePriorities = &queuePriority;

	VkPhysicalDeviceSamplerYcbcrConversionFeatures enabledConversionFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES };
	VkPhysicalDeviceVulkan14Features enabledFeatures14{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
	enabledFeatures14.pNext = &enabledConversionFeatures;

	{
		VkPhysicalDeviceSamplerYcbcrConversionFeatures conversionFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES };
		VkPhysicalDeviceVulkan14Features physFeatures14{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
		physFeatures14.pNext = &conversionFeatures;
		VkPhysicalDeviceFeatures2 physFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		physFeatures.pNext = &physFeatures14;

		vkGetPhysicalDeviceFeatures2(m_physDevice, &physFeatures);

		if (physFeatures14.hostImageCopy)
		{
			enabledFeatures14.hostImageCopy = true;
			m_bHostImageCopyEnabled = true;
		}
		if (conversionFeatures.samplerYcbcrConversion)
		{
			enabledConversionFeatures.samplerYcbcrConversion = true;
		}
	}

	std::vector<const char*> deviceExtensions;
	//deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);

	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.pNext = &enabledFeatures14;
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
	_vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT"));
	
	vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);


	VkCommandPoolCreateInfo poolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolCreateInfo.queueFamilyIndex = m_queueFamilyIndex;

	if (vkCreateCommandPool(m_device, &poolCreateInfo, nullptr, &m_commandPool) != VK_SUCCESS)
	{
		g_logger->error("vkCreateCommandPool failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyCommandPool(m_device, m_commandPool, nullptr); });

	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS)
	{
		g_logger->error("vkAllocateCommandBuffers failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer); });


	m_disparityFillHolesCS = CreateShaderModule(m_device, g_FillHolesCS, ARRAYSIZE(g_FillHolesCS) * sizeof(g_FillHolesCS[0]));
	if (m_disparityFillHolesCS == nullptr)
	{
		g_logger->error("Failed to create m_disparityFillHolesCS");
		return false;
	}
	SetVulkanDebugName(m_device, m_disparityFillHolesCS, VK_OBJECT_TYPE_SHADER_MODULE, "m_disparityFillHolesCS");
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_disparityFillHolesCS, nullptr); });

	m_disparityJointBilateralCS = CreateShaderModule(m_device, g_JointBilateralCS, ARRAYSIZE(g_JointBilateralCS) * sizeof(g_JointBilateralCS[0]));
	if (m_disparityJointBilateralCS == nullptr)
	{
		g_logger->error("Failed to create m_disparityJointBilateralCS");
		return false;
	}
	SetVulkanDebugName(m_device, m_disparityJointBilateralCS, VK_OBJECT_TYPE_SHADER_MODULE, "m_disparityJointBilateralCS");
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
	m_deletionQueue.push_back([=]() { vkDestroyFence(m_device, m_renderFence, nullptr); });
	

	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
	{
		g_logger->error("vkCreateSampler failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroySampler(m_device, m_sampler, nullptr); });


	VkDescriptorPoolSize poolSizes[3] =
	{
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
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


	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
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


	if (!m_frameDecoder.Init(m_device, m_physDevice, m_queueFamilyIndex, 1, g_renderDocAPI != nullptr))
	{
		g_logger->error("Failed to init acync frame decoder!");
		return false;
	}

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
	m_bIsInitialized = true;
	return true;
}

bool AsyncRenderer::CreateTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags)
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
	imageInfo.flags = 0;

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
		if ((memReq.memoryTypeBits & (1 << i)) && 
			(m_memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
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
	if (usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT && 
		(usageFlags & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) == 0)
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

	if (usageFlags & VK_IMAGE_USAGE_HOST_TRANSFER_BIT)
	{
		VkHostImageLayoutTransitionInfo info{ VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO };
		info.image = texture.Image;
		info.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel =  0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;

		if (vkTransitionImageLayout(m_device, 1, &info) != VK_SUCCESS)
		{
			g_logger->error("vkTransitionImageLayout failed!");
			return false;
		}

		texture.Layout = VK_IMAGE_LAYOUT_GENERAL;
	}

	texture.bIsValid = true;
	texture.Format = format;
	return true;
}


bool AsyncRenderer::CreateSharedTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags)
{
	texture.Extent = extent;
	texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

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
	imageInfo.usage = usageFlags;
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
	texture.Format = format;
	return true;
}

void AsyncRenderer::DestroyTexture(VulkanTexture& texture)
{
	texture.bIsValid = false;

	if (texture.SharedHandle != INVALID_HANDLE_VALUE)
	{
		texture.SharedHandle = INVALID_HANDLE_VALUE;
	}
	
	texture.nativeTexture = nullptr;



	if (texture.Framebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(m_device, texture.Framebuffer, nullptr);
		texture.Framebuffer = VK_NULL_HANDLE;
	}
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


// Caller is resposible for aquiring camera frame locks.
bool AsyncRenderer::CopyAndDecodeCameraFrame(std::shared_ptr<CameraCPUFrame> inFrame, void** nativeTexture)
{
	std::shared_lock acessLock(m_accessMutex);
	if (!m_bIsInitialized) { return false; }

	m_cameraTextureIndex = (m_cameraTextureIndex + 1) % 3;
	VulkanTexture& rawTexture = m_rawCameraTexture[m_cameraTextureIndex];
	VulkanTexture& sharedTexture = m_sharedCameraTexture[m_cameraTextureIndex];

	VkFormat rawFormat = CameraFrameFormatToVulkan(inFrame->RawFrameFormat);

	VkExtent2D rawExtent = { (uint32_t)inFrame->RawFrameSize[0], (uint32_t)inFrame->RawFrameSize[1] };
	if (inFrame->RawFrameFormat == FrameFormat_YUYV16) { rawExtent.width /= 2; }

	if (!rawTexture.bIsValid || rawTexture.Extent.width != rawExtent.width || rawTexture.Extent.height != rawExtent.height || rawTexture.Format != rawFormat)
	{
		DestroyTexture(rawTexture);

		VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (m_bHostImageCopyEnabled) { usageFlags |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT; }

		if (!CreateTexture(rawTexture, rawExtent, rawFormat, usageFlags))
		{
			g_logger->error("Failed to create m_rawCameraTexture!");
			return false;
		}
	}

	if (!sharedTexture.bIsValid || sharedTexture.Extent.width != inFrame->FrameSize[0] || sharedTexture.Extent.height != inFrame->FrameSize[1])
	{
		DestroyTexture(sharedTexture);

		if (!m_inlineRenderer.lock()->CreateSharedCameraTexture(&sharedTexture.SharedHandle, &sharedTexture.nativeTexture, { (uint32_t)inFrame->FrameSize[0], (uint32_t)inFrame->FrameSize[1] }, VK_FORMAT_R8G8B8A8_SRGB))
		{
			g_logger->error("Failed to create shared camera texture {}!", m_cameraTextureIndex);
			return false;
		}

		if (!CreateSharedTexture(sharedTexture, { (uint32_t)inFrame->FrameSize[0], (uint32_t)inFrame->FrameSize[1] }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
		{
			g_logger->error("Failed to create m_sharedCameraTexture!");
			return false;
		}
	}

	if (!m_frameDecoder.CopyAndDecodeCameraFrame(inFrame, rawTexture, sharedTexture))
	{
		return false;
	}

	*nativeTexture = sharedTexture.nativeTexture;

	return true;
}




bool AsyncRenderer::BeginRender(std::shared_ptr<DepthFrame> depthFrame)
{
	std::shared_lock accessLock(m_accessMutex);
	int textureIndex = depthFrame->disparityTextureIndex;

	if (!m_bIsInitialized || !depthFrame.get() || !depthFrame->bIsValid)
	{
		return false;
	}

	if (!m_disparityTexture.bIsValid ||
		m_disparityTexture.Extent.width != depthFrame->inputDisparityTextureSize[0] ||
		m_disparityTexture.Extent.height != depthFrame->inputDisparityTextureSize[1])
	{
		VkExtent2D extent = { depthFrame->inputDisparityTextureSize[0], depthFrame->inputDisparityTextureSize[1] };
		VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		if (m_bHostImageCopyEnabled) { usageFlags |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT; }

		if (!CreateTexture(m_disparityTexture, extent, VK_FORMAT_R16_SNORM, usageFlags))
		{
			g_logger->error("Failed to create m_disparityTexture!");
			return false;
		}

		if (!CreateTexture(m_confidenceTexture, extent, VK_FORMAT_R16_SNORM, usageFlags))
		{
			g_logger->error("Failed to create m_confidenceTexture!");
			return false;
		}
	}

	if (!m_outputTexture[textureIndex].bIsValid ||
		m_outputTexture[textureIndex].Extent.width != depthFrame->outputDisparityTextureSize[0] ||
		m_outputTexture[textureIndex].Extent.height != depthFrame->outputDisparityTextureSize[1])
	{
		DestroyTexture(m_outputTexture[textureIndex]);

		if (!m_inlineRenderer.lock()->CreateSharedDisparityMap(&m_outputTexture[textureIndex].SharedHandle, &m_outputTexture[textureIndex].nativeTexture, { depthFrame->outputDisparityTextureSize[0], depthFrame->outputDisparityTextureSize[1] }, VK_FORMAT_R16G16_SNORM))
		{
			g_logger->error("Failed to create shared disparity map {}!", textureIndex);
			return false;
		}

		if (!CreateSharedTexture(m_outputTexture[textureIndex], { depthFrame->outputDisparityTextureSize[0], depthFrame->outputDisparityTextureSize[1] }, VK_FORMAT_R16G16_SNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
		{
			g_logger->error("Failed to create m_outputTexture {}!", textureIndex);
			return false;
		}

		depthFrame->outputDisparityMapNativeTexture = m_outputTexture[textureIndex].nativeTexture;
	}

	if (!m_bwRectifiedCameraTexture.bIsValid || m_bwRectifiedCameraTexture.Extent.width != depthFrame->cameraFrameTextureSize[0] || m_bwRectifiedCameraTexture.Extent.height != depthFrame->cameraFrameTextureSize[1])
	{
		VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (m_bHostImageCopyEnabled) { usageFlags |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT; }

		if (!CreateTexture(m_bwRectifiedCameraTexture, { depthFrame->cameraFrameTextureSize[0] , depthFrame->cameraFrameTextureSize[1] }, VK_FORMAT_R8_SRGB, usageFlags))
		{
			g_logger->error("Failed to create m_bwRectifiedCameraTexture!");
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


void AsyncRenderer::CopyDisparityToGPU(std::vector<uint8_t>& buffer)
{
	std::shared_lock accessLock(m_accessMutex);
	if (m_disparityTexture.StagingBuffer == VK_NULL_HANDLE)
	{
		CopyHostImageToGPU(m_device, m_disparityTexture, buffer);
	}
	else
	{
		memcpy(m_disparityTexture.MappedMemory, buffer.data(), buffer.size());
		CopyTextureToGPU(m_commandBuffer, m_disparityTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}

void AsyncRenderer::CopyConfidenceToGPU(std::vector<uint8_t>& buffer)
{
	std::shared_lock accessLock(m_accessMutex);
	if (m_confidenceTexture.StagingBuffer == VK_NULL_HANDLE)
	{
		CopyHostImageToGPU(m_device, m_confidenceTexture, buffer);
	}
	else
	{
		memcpy(m_confidenceTexture.MappedMemory, buffer.data(), buffer.size());
		CopyTextureToGPU(m_commandBuffer, m_confidenceTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}

void AsyncRenderer::CopyBWRectifiedCameraFrameToGPU(std::vector<uint8_t>& buffer)
{
	std::shared_lock accessLock(m_accessMutex);
	if (m_bwRectifiedCameraTexture.StagingBuffer == VK_NULL_HANDLE)
	{
		CopyHostImageToGPU(m_device, m_bwRectifiedCameraTexture, buffer);
	}
	else
	{
		memcpy(m_bwRectifiedCameraTexture.MappedMemory, buffer.data(), buffer.size());
		CopyTextureToGPU(m_commandBuffer, m_bwRectifiedCameraTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}


void AsyncRenderer::Render(std::shared_ptr<DepthFrame> depthFrame, const Config_Stereo& stereoConf)
{
	std::shared_lock accessLock(m_accessMutex);
	int textureIndex = depthFrame->disparityTextureIndex;

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
		cameraImageInfo.imageView = m_bwRectifiedCameraTexture.View;
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

	constants.g_disparitySize[0] = depthFrame->inputDisparityTextureSize[0];
	constants.g_disparitySize[1] = depthFrame->inputDisparityTextureSize[1];
	constants.minDisparity = depthFrame->minDisparity;
	constants.maxDisparity = depthFrame->maxDisparity;
	constants.bilateralDistance = stereoConf.StereoFilteringBilateral_Distance;
	constants.bilateralDispCutoff = stereoConf.StereoFilteringBilateral_DispCutoff * (m_maxDisparity - m_minDisparity);
	constants.bHoleFillLastPass = false;
	constants.bUseInputConfidence = stereoConf.StereoFilteringWLS_Enable;


	if (stereoConf.StereoFilteringBilateral_Enable &&
		(m_bilateralDistance != stereoConf.StereoFilteringBilateral_Distance ||
		m_bilateralSigmaSpace != stereoConf.StereoFilteringBilateral_SigmaSpace ||
		m_bilateralSigmaLuma != stereoConf.StereoFilteringBilateral_SigmaLuma))
	{
		m_bilateralDistance = stereoConf.StereoFilteringBilateral_Distance;
		m_bilateralSigmaSpace = stereoConf.StereoFilteringBilateral_SigmaSpace;
		m_bilateralSigmaLuma = stereoConf.StereoFilteringBilateral_SigmaLuma;

		ComputeFilterKernels();
	}


	if (m_outputTexture[textureIndex].Layout != VK_IMAGE_LAYOUT_GENERAL)
	{
		TransitionImage(m_commandBuffer, m_outputTexture[textureIndex].Image, m_outputTexture[textureIndex].Layout, VK_IMAGE_LAYOUT_GENERAL);
		m_outputTexture[textureIndex].Layout = VK_IMAGE_LAYOUT_GENERAL;
	}

	if (stereoConf.StereoFillHoles)
	{
		int groupCountX = DivRoundUp(depthFrame->inputDisparityTextureSize[0], 32);
		int groupCountY = DivRoundUp(depthFrame->inputDisparityTextureSize[1], 32);

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

	if (stereoConf.StereoFilteringBilateral_Enable)
	{
		int groupCountX = DivRoundUp(depthFrame->outputDisparityTextureSize[0], 32);
		int groupCountY = DivRoundUp(depthFrame->outputDisparityTextureSize[1], 32);

		vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CSAsyncConstantBuffer), &constants);

		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineJointBilateral);
		vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);
	}
	else
	{
		int groupCountX = DivRoundUp(depthFrame->outputDisparityTextureSize[0], 32);
		int groupCountY = DivRoundUp(depthFrame->outputDisparityTextureSize[1], 32);

		constants.bHoleFillLastPass = true;

		vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CSAsyncConstantBuffer), &constants);

		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineFillHoles);
		vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);
	}

	// Add a RenderDoc frame end marker to allow captures from the UI.
	if (g_renderDocAPI && m_configManager->GetConfig_Main().InsertAsyncRendererRenderDocMarkers)
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


void AsyncRenderer::ComputeFilterKernels()
{
	CSFilterKernels* bufferMemory;
	vkMapMemory(m_device, m_filterKernelBufferMem, 0, sizeof(CSFilterKernels), 0, reinterpret_cast<void**>(&bufferMemory));	

	float gaussLumaCoeff = -0.5f / (m_bilateralSigmaLuma * m_bilateralSigmaLuma);

	for (int i = 0; i < LUMA_WEIGHT_CLAMP; i++)
	{
		float factor = float(i) / 256.0f;
		bufferMemory->lumaWeights[i][0] = exp(factor * factor * gaussLumaCoeff);
	}


	int radius = min(m_bilateralDistance, MAX_FILTER_DIST);

	float gaussSpaceCoeff = -0.5f / (m_bilateralSigmaSpace * m_bilateralSigmaSpace);

	for (int y = 0; y < radius; y++)
	{
		for (int x = 0; x < radius; x++)
		{
			float r2 = (float)(x * x + y * y);
			bufferMemory->spaceWeights[x][y][0] = exp(r2 * gaussSpaceCoeff);
		}
	}

	vkUnmapMemory(m_device, m_filterKernelBufferMem);
}
