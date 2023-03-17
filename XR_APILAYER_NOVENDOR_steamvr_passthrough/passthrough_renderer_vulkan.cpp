
#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <PathCch.h>
#include <xr_linear.h>
#include "lodepng.h"

#include "shaders\passthrough_vs.spv.h"

#include "shaders\alpha_prepass_ps.spv.h"
#include "shaders\alpha_prepass_masked_ps.spv.h"
#include "shaders\passthrough_ps.spv.h"
#include "shaders\passthrough_masked_ps.spv.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


struct VSConstantBuffer
{
	XrMatrix4x4f cameraProjectionToWorld;
	//XrMatrix4x4f worldToCameraProjection;
	XrMatrix4x4f worldToHMDProjection;
	XrVector4f frameUVBounds;
	XrVector3f hmdViewWorldPos;
	float projectionDistance;
	float floorHeightOffset;
	uint8_t _padding[12];
};


struct PSPassConstantBuffer
{
	XrVector2f depthRange;
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	uint32_t bDoColorAdjustment;
	uint32_t bDebugDepth;
	uint32_t bDebugValidStereo;
	uint32_t bUseFisheyeCorrection;
};

struct PSViewConstantBuffer
{
	XrVector4f frameUVBounds;
	XrVector4f prepassUVBounds;
	uint32_t rtArrayIndex;
};

struct PSMaskedConstantBuffer
{
	float maskedKey[3];
	float maskedFracChroma;
	float maskedFracLuma;
	float maskedSmooth;
	uint32_t bMaskedUseCamera;
	uint32_t bMaskedInvert;
};


bool CreateBuffer(VkDevice device, VkPhysicalDevice physDevice, VkBuffer& buffer, VkDeviceMemory& bufferMem, VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, std::deque<std::function<void()>>* deletionQueue)
{
	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.usage = usageFlags;
	bufferInfo.size = bufferSize;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

	VkMemoryRequirements memReq{};
	vkGetBufferMemoryRequirements(device, buffer, &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryTypeBits & (1 << i))
			&& (memProps.memoryTypes[i].propertyFlags & memFlags))
		{
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = i;
			if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, buffer, nullptr);
				return false;
			}

			break;
		}
	}

	if (!bufferMem)
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return false;
	}

	if (deletionQueue)
	{
		deletionQueue->push_back([=]() { vkDestroyBuffer(device, buffer, nullptr); });
		deletionQueue->push_back([=]() { vkFreeMemory(device, bufferMem, nullptr); });
	}

	vkBindBufferMemory(device, buffer, bufferMem, 0);

	return true;
}



void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
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
	else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		depFlags = 0;
	}
	else
	{
		ErrorLog("Unknown layout transition!\n");
		return;
	}

	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, depFlags, 0, nullptr, 0, nullptr, 1, &barrier);
}


void UploadImage(VkCommandBuffer commandBuffer, VkDevice device, VkBuffer uploadBuffer, VkImage outImage, VkExtent3D extent, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	TransitionImage(commandBuffer, outImage, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = extent;

	vkCmdCopyBufferToImage(commandBuffer, uploadBuffer, outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	TransitionImage(commandBuffer, outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, newLayout);
}





PassthroughRendererVulkan::PassthroughRendererVulkan(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllMoudule, std::shared_ptr<ConfigManager> configManager)
	: m_dllModule(dllMoudule)
	, m_configManager(configManager)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
	, m_vertexBuffer(nullptr)
	, m_vertexBufferMem(nullptr)
	, m_descriptorLayout(nullptr)
	, m_vertexShader(nullptr)
	, m_pixelShader(nullptr)
	, m_prepassShader(nullptr)
	, m_maskedPrepassShader(nullptr)
	, m_maskedPixelShader(nullptr)
	, m_renderpass(nullptr)
	, m_pipelineLayout(nullptr)
	, m_pipelineDefault(nullptr)
	, m_pipelineAlphaPremultiplied(nullptr)
	, m_pipelinePrepassUseAppAlpha(nullptr)
	, m_pipelinePrepassIgnoreAppAlpha(nullptr)
	, m_pipelineMaskedPrepass(nullptr)
	, m_pipelineMaskedRender(nullptr)
	, m_testPattern(nullptr)
	, m_testPatternMem(nullptr)
	, m_testPatternBuffer(nullptr)
	, m_testPatternBufferMem(nullptr)
	, m_uvDistortionMap(nullptr)
	, m_uvDistortionMapView(nullptr)
	, m_uvDistortionMapMem(nullptr)
	, m_uvDistortionMapBuffer(nullptr)
	, m_uvDistortionMapBufferMem(nullptr)
{
	m_instance = binding.instance;
	m_physDevice = binding.physicalDevice;
	m_device = binding.device;
	m_queueFamilyIndex = binding.queueFamilyIndex;
	m_queueIndex = binding.queueIndex;

	memset(m_psPassConstantBuffer, 0, sizeof(m_psPassConstantBuffer));
	memset(m_psPassConstantBufferMem, 0, sizeof(m_psPassConstantBufferMem));
	memset(m_psPassConstantBufferMappings, 0, sizeof(m_psPassConstantBufferMappings));
	memset(m_psMaskedConstantBuffer, 0, sizeof(m_psMaskedConstantBuffer));
	memset(m_psMaskedConstantBufferMem, 0, sizeof(m_psMaskedConstantBufferMem));
	memset(m_psMaskedConstantBufferMappings, 0, sizeof(m_psMaskedConstantBufferMappings));

	memset(m_cameraFrameRes, 0, sizeof(m_cameraFrameRes));
	memset(m_cameraFrameResMem, 0, sizeof(m_cameraFrameResMem));
	memset(m_cameraFrameResExternalHandle, 0, sizeof(m_cameraFrameResExternalHandle));
	memset(m_cameraFrameResView, 0, sizeof(m_cameraFrameResView));
	memset(m_cameraFrameResArrayView, 0, sizeof(m_cameraFrameResArrayView));
}


PassthroughRendererVulkan::~PassthroughRendererVulkan()
{
	if (!m_device || !m_queue)
	{
		return;
	}

	if (!vkQueueWaitIdle(m_queue))
	{
		return;
	}

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		if (m_renderTargets[i])
		{
			vkDestroyImageView(m_device, m_renderTargetViews[i], nullptr);
			vkDestroyFramebuffer(m_device, m_renderTargetFramebuffers[i], nullptr);
		}

		if (m_cameraFrameRes[i])
		{
			vkDestroyImage(m_device, m_cameraFrameRes[i], nullptr);
			vkFreeMemory(m_device, m_cameraFrameResMem[i], nullptr);
			vkDestroyImageView(m_device, m_cameraFrameResView[i], nullptr);
			vkDestroyImageView(m_device, m_cameraFrameResArrayView[i], nullptr);
		}
	}

	for (std::function<void()> deleteFunc : m_deletionQueue)
	{
		deleteFunc();
	}
}


bool PassthroughRendererVulkan::InitRenderer()
{
	vkGetDeviceQueue(m_device, m_queueFamilyIndex, m_queueIndex, &m_queue);
	
	{
		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = m_queueFamilyIndex;

		if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
		{
			ErrorLog("vkCreateCommandPool failure!\n");
			return false;
		}
	
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = NUM_SWAPCHAINS;

		if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffer) != VK_SUCCESS)
		{
			ErrorLog("vkAllocateCommandBuffers failure!\n");
			return false;
		}

		m_deletionQueue.push_back([=]() { vkFreeCommandBuffers(m_device, m_commandPool, NUM_SWAPCHAINS, m_commandBuffer); });
		m_deletionQueue.push_back([=]() { vkDestroyCommandPool(m_device, m_commandPool, nullptr); });
	}


	m_vertexShader = CreateShaderModule(g_PassthroughShaderVS, ARRAYSIZE(g_PassthroughShaderVS) * sizeof(g_PassthroughShaderVS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_vertexShader, nullptr); });

	m_pixelShader = CreateShaderModule(g_PassthroughShaderPS, ARRAYSIZE(g_PassthroughShaderPS) * sizeof(g_PassthroughShaderPS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_pixelShader, nullptr); });

	m_prepassShader = CreateShaderModule(g_AlphaPrepassShaderPS, ARRAYSIZE(g_AlphaPrepassShaderPS) * sizeof(g_AlphaPrepassShaderPS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_prepassShader, nullptr); });

	m_maskedPrepassShader = CreateShaderModule(g_AlphaPrepassMaskedShaderPS, ARRAYSIZE(g_AlphaPrepassMaskedShaderPS) * sizeof(g_AlphaPrepassMaskedShaderPS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_maskedPrepassShader, nullptr); });

	m_maskedPixelShader = CreateShaderModule(g_PassthroughMaskedShaderPS, ARRAYSIZE(g_PassthroughMaskedShaderPS) * sizeof(g_PassthroughMaskedShaderPS[0]));
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_maskedPixelShader, nullptr); });

	if (!m_vertexShader || !m_pixelShader || !m_prepassShader || !m_maskedPrepassShader || !m_maskedPixelShader)
	{
		ErrorLog("Shader module creation failure!\n");
		return false;
	}

	{
		VkDescriptorPoolSize poolSizes[2] =
		{
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NUM_SWAPCHAINS * 4},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_SWAPCHAINS * 6}
		};

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		poolInfo.maxSets = NUM_SWAPCHAINS * 2;

		if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			ErrorLog("vkCreateDescriptorPool failure!\n");
			return false;
		}
		m_deletionQueue.push_back([=]() { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); });

		VkDescriptorSetLayoutBinding layoutBindings[6] =
		{
			{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
		};


		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 6;
		layoutInfo.pBindings = layoutBindings;

		if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorLayout) != VK_SUCCESS)
		{
			ErrorLog("vkCreateDescriptorSetLayout failure!\n");
			return false;
		}
		m_deletionQueue.push_back([=]() { vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr); });
		

		VkDescriptorSetLayout layouts[NUM_SWAPCHAINS * 2];

		for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
		{
			layouts[i] = m_descriptorLayout;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = NUM_SWAPCHAINS * 2;
		allocInfo.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets) != VK_SUCCESS)
		{
			ErrorLog("vkAllocateDescriptorSets failure!\n");
			return false;
		}


		for (int i = 0; i < NUM_SWAPCHAINS; i++)
		{
			if (!CreateBuffer(m_device, m_physDevice, m_psPassConstantBuffer[i], m_psPassConstantBufferMem[i], 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &m_deletionQueue))
			{
				ErrorLog("m_psPassConstantBuffer creation failure!\n");
				return false;
			}

			vkMapMemory(m_device, m_psPassConstantBufferMem[i], 0, sizeof(PSPassConstantBuffer), 0, &m_psPassConstantBufferMappings[i]);

			m_deletionQueue.push_back([=]() { 
				vkFreeMemory(m_device, m_psPassConstantBufferMem[i], nullptr);
				vkDestroyBuffer(m_device, m_psPassConstantBuffer[i], nullptr);
			});


			if (!CreateBuffer(m_device, m_physDevice, m_psMaskedConstantBuffer[i], m_psMaskedConstantBufferMem[i], 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &m_deletionQueue))
			{
				ErrorLog("psMaskedConstantBuffer creation failure!\n");
				return false;
			}

			vkMapMemory(m_device, m_psMaskedConstantBufferMem[i], 0, sizeof(PSMaskedConstantBuffer), 0, &m_psMaskedConstantBufferMappings[i]);

			m_deletionQueue.push_back([=]() {
				vkFreeMemory(m_device, m_psMaskedConstantBufferMem[i], nullptr);
				vkDestroyBuffer(m_device, m_psMaskedConstantBuffer[i], nullptr);
			});
		}
	}

	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	
	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_cameraSampler) != VK_SUCCESS)
	{
		ErrorLog("Camera vkCreateSampler failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroySampler(m_device, m_intermediateSampler, nullptr); });

	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_intermediateSampler) != VK_SUCCESS)
	{
		ErrorLog("Intermediate vkCreateSampler failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroySampler(m_device, m_cameraSampler, nullptr); });

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(m_commandBuffer[NUM_SWAPCHAINS - 1], &beginInfo);

	if(!GenerateMesh(m_commandBuffer[NUM_SWAPCHAINS - 1]))
	{
		return false;
	}

	if (!SetupTestImage(m_commandBuffer[NUM_SWAPCHAINS - 1]))
	{
		return false;
	}

	vkEndCommandBuffer(m_commandBuffer[NUM_SWAPCHAINS - 1]);

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer[NUM_SWAPCHAINS - 1];

	vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);

	return true;
}


bool PassthroughRendererVulkan::SetupPipeline(VkFormat format)
{
	VkAttachmentDescription colorDesc{};
	colorDesc.format = format;
	colorDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	colorDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = &colorDesc;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderpass) != VK_SUCCESS)
	{
		ErrorLog("Default vkCreateRenderPass failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyRenderPass(m_device, m_renderpass, nullptr); });


	VkAttachmentDescription maskedPrepassColorDesc;
	maskedPrepassColorDesc.format = VK_FORMAT_R8_UNORM;
	maskedPrepassColorDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	maskedPrepassColorDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	maskedPrepassColorDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	maskedPrepassColorDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	maskedPrepassColorDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	maskedPrepassColorDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	maskedPrepassColorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkRenderPassCreateInfo maskedRPInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	maskedRPInfo.attachmentCount = 1;
	maskedRPInfo.pAttachments = &maskedPrepassColorDesc;
	maskedRPInfo.subpassCount = 1;
	maskedRPInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(m_device, &maskedRPInfo, nullptr, &m_renderpassMaskedPrepass) != VK_SUCCESS)
	{
		ErrorLog("Masked vkCreateRenderPass failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyRenderPass(m_device, m_renderpassMaskedPrepass, nullptr); });


	VkPushConstantRange pushRanges[2] =
	{
		 {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VSConstantBuffer)}
		,{VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(VSConstantBuffer), sizeof(PSViewConstantBuffer)}
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.pushConstantRangeCount = 2;
	pipelineLayoutInfo.pPushConstantRanges = pushRanges;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorLayout;

	if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
	{
		ErrorLog("vkCreatePipelineLayout failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); });


	std::vector<VkDynamicState> dynamicStates = 
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();

	VkVertexInputBindingDescription bindDesc{};
	bindDesc.binding = 0;
	bindDesc.stride = sizeof(float) * 3;
	bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrDesc{};
	attrDesc.location = 0;
	attrDesc.binding = 0;
	attrDesc.format = VK_FORMAT_R32G32B32_SFLOAT;
	attrDesc.offset = 0;

	VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vi.vertexBindingDescriptionCount = 1;
	vi.pVertexBindingDescriptions = &bindDesc;
	vi.vertexAttributeDescriptionCount = 1;
	vi.pVertexAttributeDescriptions = &attrDesc;

	VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.primitiveRestartEnable = VK_FALSE;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_BACK_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE; // Front faces flipped since we are scaling mesh y axis by -1 in the shader
	rs.depthClampEnable = VK_FALSE;
	rs.rasterizerDiscardEnable = VK_FALSE;
	rs.depthBiasEnable = VK_FALSE;
	rs.depthBiasConstantFactor = 0;
	rs.depthBiasClamp = 0;
	rs.depthBiasSlopeFactor = 0;
	rs.lineWidth = 1.0f;


	VkPipelineColorBlendAttachmentState attachStateNoBlend{};
	attachStateNoBlend.blendEnable = VK_FALSE;
	attachStateNoBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStateNoBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachStateNoBlend.colorBlendOp = VK_BLEND_OP_ADD;
	attachStateNoBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStateNoBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachStateNoBlend.alphaBlendOp = VK_BLEND_OP_ADD;
	attachStateNoBlend.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendStateNoBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blendStateNoBlend.attachmentCount = 1;
	blendStateNoBlend.pAttachments = &attachStateNoBlend;
	blendStateNoBlend.logicOpEnable = VK_FALSE;
	blendStateNoBlend.logicOp = VK_LOGIC_OP_NO_OP;
	blendStateNoBlend.blendConstants[0] = 1.0f;
	blendStateNoBlend.blendConstants[1] = 1.0f;
	blendStateNoBlend.blendConstants[2] = 1.0f;
	blendStateNoBlend.blendConstants[3] = 1.0f;

	VkPipelineColorBlendAttachmentState attachStateBase{};
	attachStateBase.blendEnable = VK_TRUE;
	attachStateBase.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	attachStateBase.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
	attachStateBase.colorBlendOp = VK_BLEND_OP_ADD;
	attachStateBase.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStateBase.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStateBase.alphaBlendOp = VK_BLEND_OP_ADD;
	attachStateBase.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendStateBase{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blendStateBase.attachmentCount = 1;
	blendStateBase.pAttachments = &attachStateBase;
	blendStateBase.logicOpEnable = VK_FALSE;
	blendStateBase.logicOp = VK_LOGIC_OP_NO_OP;
	blendStateBase.blendConstants[0] = 1.0f;
	blendStateBase.blendConstants[1] = 1.0f;
	blendStateBase.blendConstants[2] = 1.0f;
	blendStateBase.blendConstants[3] = 1.0f;

	VkPipelineColorBlendAttachmentState attachStateAlphaPremultiplied = attachStateBase;
	attachStateAlphaPremultiplied.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkPipelineColorBlendStateCreateInfo blendStateAlphaPremultiplied = blendStateBase;
	blendStateAlphaPremultiplied.pAttachments = &attachStateAlphaPremultiplied;

	VkPipelineColorBlendAttachmentState attachStateSrcAlpha = attachStateBase;
	attachStateSrcAlpha.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	attachStateSrcAlpha.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	VkPipelineColorBlendStateCreateInfo blendStateSrcAlpha = blendStateBase;
	blendStateSrcAlpha.pAttachments = &attachStateSrcAlpha;

	VkPipelineColorBlendAttachmentState attachStatePrepassUseAppAlpha = attachStateBase;
	attachStatePrepassUseAppAlpha.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachStatePrepassUseAppAlpha.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStatePrepassUseAppAlpha.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStatePrepassUseAppAlpha.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStatePrepassUseAppAlpha.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blendStatePrepassUseAppAlpha = blendStateBase;
	blendStatePrepassUseAppAlpha.pAttachments = &attachStatePrepassUseAppAlpha;

	VkPipelineColorBlendAttachmentState attachStatePrepassIgnoreAppAlpha = attachStateBase;
	attachStatePrepassIgnoreAppAlpha.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachStatePrepassIgnoreAppAlpha.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStatePrepassIgnoreAppAlpha.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachStatePrepassIgnoreAppAlpha.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachStatePrepassIgnoreAppAlpha.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blendStatePrepassIgnoreAppAlpha = blendStateBase;
	blendStatePrepassIgnoreAppAlpha.pAttachments = &attachStatePrepassIgnoreAppAlpha;


	VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	ds.depthTestEnable = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;
	ds.depthCompareOp = VK_COMPARE_OP_GREATER;
	ds.depthBoundsTestEnable = VK_FALSE;
	ds.stencilTestEnable = VK_FALSE;
	ds.front.failOp = VK_STENCIL_OP_KEEP;
	ds.front.passOp = VK_STENCIL_OP_KEEP;
	ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
	ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
	ds.back = ds.front;
	ds.minDepthBounds = 0.0f;
	ds.maxDepthBounds = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineShaderStageCreateInfo shaderInfoVertex{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoVertex.module = m_vertexShader;
	shaderInfoVertex.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderInfoVertex.pName = "main";

	VkPipelineShaderStageCreateInfo shaderInfoPassthroughFS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoPassthroughFS.module = m_pixelShader;
	shaderInfoPassthroughFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderInfoPassthroughFS.pName = "main";

	VkPipelineShaderStageCreateInfo shaderInfoPrepassFS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoPrepassFS.module = m_prepassShader;
	shaderInfoPrepassFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderInfoPrepassFS.pName = "main";

	VkPipelineShaderStageCreateInfo shaderInfoMaskedPrepassFS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoMaskedPrepassFS.module = m_maskedPrepassShader;
	shaderInfoMaskedPrepassFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderInfoMaskedPrepassFS.pName = "main";

	VkPipelineShaderStageCreateInfo shaderInfoMaskedPassthroughFS{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoMaskedPassthroughFS.module = m_maskedPixelShader;
	shaderInfoMaskedPassthroughFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderInfoMaskedPassthroughFS.pName = "main";

	std::vector<VkPipelineShaderStageCreateInfo> shaderInfoBase{ shaderInfoVertex, shaderInfoPassthroughFS };
	std::vector<VkPipelineShaderStageCreateInfo> shaderInfoPrepass{ shaderInfoVertex, shaderInfoPrepassFS };
	std::vector<VkPipelineShaderStageCreateInfo> shaderInfoMaskedPrepass{ shaderInfoVertex, shaderInfoMaskedPrepassFS };
	std::vector<VkPipelineShaderStageCreateInfo> shaderInfoMasked{ shaderInfoVertex, shaderInfoMaskedPassthroughFS };

	VkGraphicsPipelineCreateInfo pipelineInfoBase{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfoBase.stageCount = (uint32_t)shaderInfoBase.size();
	pipelineInfoBase.pStages = shaderInfoBase.data();
	pipelineInfoBase.pVertexInputState = &vi;
	pipelineInfoBase.pInputAssemblyState = &ia;
	pipelineInfoBase.pTessellationState = nullptr;
	pipelineInfoBase.pViewportState = &vp;
	pipelineInfoBase.pRasterizationState = &rs;
	pipelineInfoBase.pMultisampleState = &ms;
	pipelineInfoBase.pDepthStencilState = &ds;
	pipelineInfoBase.pColorBlendState = &blendStateBase;
	pipelineInfoBase.pDynamicState = &dynamicState;
	pipelineInfoBase.layout = m_pipelineLayout;
	pipelineInfoBase.renderPass = m_renderpass;
	pipelineInfoBase.subpass = 0;

	VkGraphicsPipelineCreateInfo piAlphaPremultiplied{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	piAlphaPremultiplied = pipelineInfoBase;
	piAlphaPremultiplied.stageCount = (uint32_t)shaderInfoBase.size();
	piAlphaPremultiplied.pStages = shaderInfoBase.data();
	piAlphaPremultiplied.pColorBlendState = &blendStateAlphaPremultiplied;

	VkGraphicsPipelineCreateInfo piPrepassUseAppAlpha{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	piPrepassUseAppAlpha = pipelineInfoBase;
	piPrepassUseAppAlpha.stageCount = (uint32_t)shaderInfoPrepass.size();
	piPrepassUseAppAlpha.pStages = shaderInfoPrepass.data();
	piPrepassUseAppAlpha.pColorBlendState = &blendStatePrepassUseAppAlpha;

	VkGraphicsPipelineCreateInfo piPrepassIgnoreAppAlpha{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	piPrepassIgnoreAppAlpha = pipelineInfoBase;
	piPrepassIgnoreAppAlpha.stageCount = (uint32_t)shaderInfoPrepass.size();
	piPrepassIgnoreAppAlpha.pStages = shaderInfoPrepass.data();
	piPrepassIgnoreAppAlpha.pColorBlendState = &blendStatePrepassIgnoreAppAlpha;

	VkGraphicsPipelineCreateInfo piMaskedPrepass{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	piMaskedPrepass = pipelineInfoBase;
	piMaskedPrepass.stageCount = (uint32_t)shaderInfoMaskedPrepass.size();
	piMaskedPrepass.pStages = shaderInfoMaskedPrepass.data();
	piMaskedPrepass.pColorBlendState = &blendStateNoBlend;
	piMaskedPrepass.renderPass = m_renderpassMaskedPrepass;

	VkGraphicsPipelineCreateInfo piMaskedRender{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	piMaskedRender = pipelineInfoBase;
	piMaskedRender.stageCount = (uint32_t)shaderInfoMasked.size();
	piMaskedRender.pStages = shaderInfoMasked.data();
	piMaskedRender.pColorBlendState = &blendStateSrcAlpha;
	
	std::vector<VkGraphicsPipelineCreateInfo> pipelineInfos{ pipelineInfoBase, piAlphaPremultiplied, piPrepassUseAppAlpha, piPrepassIgnoreAppAlpha, piMaskedPrepass, piMaskedRender };

	std::vector<VkPipeline> pipelines;
	pipelines.resize(pipelineInfos.size());

	if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, (uint32_t)pipelineInfos.size(), pipelineInfos.data(), nullptr, pipelines.data()) != VK_SUCCESS)
	{
		ErrorLog("vkCreateGraphicsPipelines failure!\n");
		return false;
	}

	m_pipelineDefault = pipelines[0];
	m_pipelineAlphaPremultiplied = pipelines[1];
	m_pipelinePrepassUseAppAlpha = pipelines[2];
	m_pipelinePrepassIgnoreAppAlpha = pipelines[3];
	m_pipelineMaskedPrepass = pipelines[4];
	m_pipelineMaskedRender = pipelines[5];

	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineDefault, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineAlphaPremultiplied, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelinePrepassUseAppAlpha, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelinePrepassIgnoreAppAlpha, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineMaskedPrepass, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyPipeline(m_device, m_pipelineMaskedRender, nullptr); });

	return true;
}


VkShaderModule PassthroughRendererVulkan::CreateShaderModule(const uint32_t* bytecode, size_t codeSize)
{
	VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	createInfo.codeSize = codeSize;
	createInfo.pCode = bytecode;

	VkShaderModule module;

	if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS)
	{
		ErrorLog("vkCreateShaderModule failure!\n");
		return nullptr;
	}

	return module;
}


bool PassthroughRendererVulkan::SetupTestImage(VkCommandBuffer commandBuffer)
{
	char path[MAX_PATH];

	if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	{
		ErrorLog("Error opening test pattern.\n");
		return false;
	}

	std::string pathStr = path;
	std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\testpattern.png";

	std::vector<unsigned char> image;
	uint32_t width, height;

	unsigned error = lodepng::decode(image, width, height, imgPath.c_str());
	if (error)
	{
		ErrorLog("Error decoding test pattern.\n");
		return false;
	}

	if (!CreateBuffer(m_device, m_physDevice, m_testPatternBuffer, m_testPatternBufferMem, image.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &m_deletionQueue))
	{
		ErrorLog("Test image buffer creation failure!\n");
		return false;
	}

	void* mappedData;
	vkMapMemory(m_device, m_testPatternBufferMem, 0, image.size(), 0, &mappedData);
	memcpy(mappedData, image.data(), image.size());
	vkUnmapMemory(m_device, m_testPatternBufferMem);

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	if (vkCreateImage(m_device, &imageInfo, nullptr, &m_testPattern) != VK_SUCCESS)
	{
		ErrorLog("Test image vkCreateImage failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyImage(m_device, m_testPattern, nullptr); });
	

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_device, m_testPattern, &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryTypeBits & (1 << i))
			&& (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = i;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_testPatternMem) != VK_SUCCESS)
			{
				ErrorLog("Test image vkAllocateMemory failure!\n");
				return false;
			}
			break;
		}
	}

	if (!m_testPatternMem)
	{
		ErrorLog("Test image memory allocation failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkFreeMemory(m_device, m_testPatternMem, nullptr); });

	vkBindImageMemory(m_device, m_testPattern, m_testPatternMem, 0);

	UploadImage(commandBuffer, m_device, m_testPatternBuffer, m_testPattern, { width, height, 1 }, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_testPattern;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_testPatternView) != VK_SUCCESS)
	{
		ErrorLog("Test image vkCreateImageView failure!\n");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyImageView(m_device, m_testPatternView, nullptr); });

	return true;
}


void PassthroughRendererVulkan::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	if (m_renderTargets[bufferIndex] == rendertarget)
	{
		return;
	}

	if (!m_pipelineDefault)
	{
		if (!SetupPipeline((VkFormat)swapchainInfo.format))
		{
			return;
		}
	}

	if (m_renderTargets[bufferIndex])
	{
		vkDestroyImageView(m_device, m_renderTargetViews[bufferIndex], nullptr);
		vkDestroyFramebuffer(m_device, m_renderTargetFramebuffers[bufferIndex], nullptr);

		m_renderTargetViews[bufferIndex] = nullptr;
		m_renderTargetFramebuffers[bufferIndex] = nullptr;
	}

	m_renderTargets[bufferIndex] = (VkImage)rendertarget;

	// The view and framebuffer are set to use size 1 arrays to support both single and array for passed targets.
	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_renderTargets[bufferIndex];
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	viewInfo.format = (VkFormat)swapchainInfo.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = swapchainInfo.arraySize > 1 ? viewIndex : 0;
	viewInfo.subresourceRange.layerCount = swapchainInfo.arraySize;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_renderTargetViews[bufferIndex]) != VK_SUCCESS)
	{
		m_renderTargets[bufferIndex] = nullptr;
		ErrorLog("Render target vkCreateImageView failure!\n");
		return;
	}

	VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fbInfo.renderPass = m_renderpass;
	fbInfo.attachmentCount = 1;
	fbInfo.pAttachments = &m_renderTargetViews[bufferIndex];
	fbInfo.width = swapchainInfo.width;
	fbInfo.height = swapchainInfo.height;
	fbInfo.layers = 1;
	if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_renderTargetFramebuffers[bufferIndex]) != VK_SUCCESS)
	{
		m_renderTargets[bufferIndex] = nullptr;
		vkDestroyImageView(m_device, m_renderTargetViews[bufferIndex], nullptr);
		ErrorLog("Render target vkCreateFramebuffer failure!\n");
		return;
	}
}


void PassthroughRendererVulkan::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}


bool PassthroughRendererVulkan::GenerateMesh(VkCommandBuffer commandBuffer)
{
	m_vertices.reserve(NUM_MESH_BOUNDARY_VERTICES * 4 * 6);

	// Grenerate a triangle strip cylinder with radius and height 1.

	float radianStep = -2.0f * MATH_PI / (float)NUM_MESH_BOUNDARY_VERTICES;

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(0.0f);
		m_vertices.push_back(1.0f);
		m_vertices.push_back(0.0f);

		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(1.0f);
		m_vertices.push_back(sinf(radianStep * i));
	}

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(1.0f);
		m_vertices.push_back(sinf(radianStep * i));

		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(0.0f);
		m_vertices.push_back(sinf(radianStep * i));
	}

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(0.0f);
		m_vertices.push_back(sinf(radianStep * i));

		m_vertices.push_back(0.0f);
		m_vertices.push_back(0.0f);
		m_vertices.push_back(0.0f);
	}

	uint32_t bufferSize = (uint32_t)(m_vertices.size() * sizeof(float));

	if (!CreateBuffer(m_device, m_physDevice, m_vertexBuffer, m_vertexBufferMem, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &m_deletionQueue))
	{
		ErrorLog("Mesh vertex buffer creation failure!\n");
		return false;
	}

	void* mappedData;
	vkMapMemory(m_device, m_vertexBufferMem, 0, bufferSize, 0, &mappedData);
	memcpy(mappedData, m_vertices.data(), bufferSize);
	vkUnmapMemory(m_device, m_vertexBufferMem);

	return true;
}


void PassthroughRendererVulkan::SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap)
{
	if (m_uvDistortionMap)
	{
		vkDestroyImage(m_device, m_uvDistortionMap, nullptr);
		vkDestroyImageView(m_device, m_uvDistortionMapView, nullptr);
		vkDestroyBuffer(m_device, m_uvDistortionMapBuffer, nullptr);
		vkFreeMemory(m_device, m_uvDistortionMapMem, nullptr);

		m_uvDistortionMap = nullptr;
		m_uvDistortionMapView = nullptr;
		m_uvDistortionMapBuffer = nullptr;
		m_uvDistortionMapMem = nullptr;
	}

	if (!CreateBuffer(m_device, m_physDevice, m_uvDistortionMapBuffer, m_uvDistortionMapBufferMem, uvDistortionMap->size() * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &m_deletionQueue))
	{
		ErrorLog("UV distortion map buffer creation failure!\n");
		return;
	}

	void* mappedData;
	vkMapMemory(m_device, m_uvDistortionMapBufferMem, 0, uvDistortionMap->size() * sizeof(float), 0, &mappedData);
	memcpy(mappedData, uvDistortionMap->data(), uvDistortionMap->size() * sizeof(float));
	vkUnmapMemory(m_device, m_uvDistortionMapBufferMem);


	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = m_cameraTextureWidth;
	imageInfo.extent.height = m_cameraTextureHeight;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R32G32_SFLOAT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;


	if (vkCreateImage(m_device, &imageInfo, nullptr, &m_uvDistortionMap) != VK_SUCCESS)
	{
		ErrorLog("UV distortion map vkCreateImage failure!\n");
		return;
	}

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_device, m_uvDistortionMap, &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);

	for (uint32_t j = 0; j < memProps.memoryTypeCount; j++)
	{
		if ((memReq.memoryTypeBits & (1 << j))
			&& (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = j;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uvDistortionMapMem) != VK_SUCCESS)
			{
				ErrorLog("UV distortion map vkAllocateMemory failure!\n");
				vkDestroyImage(m_device, m_uvDistortionMap, nullptr);
				return;
			}
			break;
		}
	}

	if (!m_uvDistortionMapMem)
	{
		ErrorLog("UV distortion map memory prop not found!\n");
		vkDestroyImage(m_device, m_uvDistortionMap, nullptr);
		m_uvDistortionMap = nullptr;
		return;
	}

	vkBindImageMemory(m_device, m_uvDistortionMap, m_uvDistortionMapMem, 0);

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_uvDistortionMap;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R32G32_SFLOAT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_uvDistortionMapView) != VK_SUCCESS)
	{
		vkDestroyImage(m_device, m_uvDistortionMap, nullptr);
		vkFreeMemory(m_device, m_uvDistortionMapMem, nullptr);
		m_uvDistortionMap = nullptr;
		ErrorLog("UV distortion map vkCreateImageView failure!\n");
		return;
	}

	UploadImage(m_commandBuffer[m_frameIndex], m_device, m_uvDistortionMapBuffer, m_uvDistortionMap, { m_cameraTextureWidth, m_cameraTextureHeight, 1 }, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_deletionQueue.push_back([=]() { vkDestroyImage(m_device, m_uvDistortionMap, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyBuffer(m_device, m_uvDistortionMapBuffer, nullptr); });
	m_deletionQueue.push_back([=]() { vkDestroyImageView(m_device, m_uvDistortionMapView, nullptr); });
	m_deletionQueue.push_back([=]() { vkFreeMemory(m_device, m_uvDistortionMapMem, nullptr); });
}


void PassthroughRendererVulkan::SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height)
{

	if (m_renderTargets[index])
	{
		vkDestroyImage(m_device, m_intermediateRenderTargets[index], nullptr);
		vkDestroyImageView(m_device, m_intermediateRenderTargetViews[index], nullptr);
		vkDestroyFramebuffer(m_device, m_intermediateRenderTargetFramebuffers[index], nullptr);

		m_intermediateRenderTargets[index] = nullptr;
		m_intermediateRenderTargetViews[index] = nullptr;
		m_intermediateRenderTargetFramebuffers[index] = nullptr;
	}

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8_UNORM;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;


	if (vkCreateImage(m_device, &imageInfo, nullptr, &m_intermediateRenderTargets[index]) != VK_SUCCESS)
	{
		ErrorLog("Intermediate rendertarget vkCreateImage failure!\n");
		return;
	}

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_device, m_intermediateRenderTargets[index], &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);

	for (uint32_t j = 0; j < memProps.memoryTypeCount; j++)
	{
		if ((memReq.memoryTypeBits & (1 << j))
			&& (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = j;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_intermediateRenderTargetMem[index]) != VK_SUCCESS)
			{
				ErrorLog("Intermediate rendertarget vkAllocateMemory failure!\n");
				vkDestroyImage(m_device, m_intermediateRenderTargets[index], nullptr);
				return;
			}
			break;
		}
	}

	if (!m_intermediateRenderTargetMem[index])
	{
		ErrorLog("Intermediate rendertarget memory prop not found!\n");
		vkDestroyImage(m_device, m_intermediateRenderTargets[index], nullptr);
		return;
	}

	vkBindImageMemory(m_device, m_intermediateRenderTargets[index], m_intermediateRenderTargetMem[index], 0);

	TransitionImage(m_commandBuffer[m_frameIndex], m_intermediateRenderTargets[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_intermediateRenderTargets[index];
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_intermediateRenderTargetViews[index]) != VK_SUCCESS)
	{
		vkDestroyImage(m_device, m_intermediateRenderTargets[index], nullptr);
		vkFreeMemory(m_device, m_intermediateRenderTargetMem[index], nullptr);
		m_intermediateRenderTargets[index] = nullptr;
		ErrorLog("Intermediate rendertarget vkCreateImageView failure!\n");
		return;
	}

	VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fbInfo.renderPass = m_renderpassMaskedPrepass;
	fbInfo.attachmentCount = 1;
	fbInfo.pAttachments = &m_intermediateRenderTargetViews[index];
	fbInfo.width = width;
	fbInfo.height = height;
	fbInfo.layers = 1;
	if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_intermediateRenderTargetFramebuffers[index]) != VK_SUCCESS)
	{	
		vkDestroyImageView(m_device, m_intermediateRenderTargetViews[index], nullptr);
		vkDestroyImage(m_device, m_intermediateRenderTargets[index], nullptr);
		vkFreeMemory(m_device, m_intermediateRenderTargetMem[index], nullptr);
		m_intermediateRenderTargets[index] = nullptr;
		ErrorLog("Intermediate rendertarget vkCreateFramebuffer failure!\n");
		return;
	}
}


bool PassthroughRendererVulkan::UpdateCameraFrameResource(VkCommandBuffer commandBuffer, int frameIndex, void* frameResource)
{
	if (m_cameraFrameResExternalHandle[frameIndex] == frameResource)
	{
		return true;
	}

	if (m_cameraFrameRes[frameIndex])
	{
		vkDestroyImage(m_device, m_cameraFrameRes[frameIndex], nullptr);
		vkFreeMemory(m_device, m_cameraFrameResMem[frameIndex], nullptr);
		vkDestroyImageView(m_device, m_cameraFrameResView[frameIndex], nullptr);
		vkDestroyImageView(m_device, m_cameraFrameResArrayView[frameIndex], nullptr);
	}

	VkExternalMemoryImageCreateInfo  extInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
	extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.pNext = &extInfo;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = m_cameraTextureWidth;
	imageInfo.extent.height = m_cameraTextureHeight;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;


	if (vkCreateImage(m_device, &imageInfo, nullptr, &m_cameraFrameRes[frameIndex]) != VK_SUCCESS)
	{
		ErrorLog("Shared texture vkCreateImage failure!\n");
		return false;
	}

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_device, m_cameraFrameRes[frameIndex], &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);

	for (uint32_t j = 0; j < memProps.memoryTypeCount; j++)
	{
		if ((memReq.memoryTypeBits & (1 << j))
			&& (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			VkImportMemoryWin32HandleInfoKHR handleInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
			handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
			handleInfo.handle = frameResource;

			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.pNext = &handleInfo;
			allocInfo.memoryTypeIndex = j;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_cameraFrameResMem[frameIndex]) != VK_SUCCESS)
			{
				ErrorLog("Shared texture vkAllocateMemory failure!\n");
				vkDestroyImage(m_device, m_cameraFrameRes[frameIndex], nullptr);
				return false;
			}
			break;
		}
	}

	if (!m_cameraFrameResMem[frameIndex])
	{
		ErrorLog("Shared texture memory prop not found!\n");
		vkDestroyImage(m_device, m_cameraFrameRes[frameIndex], nullptr);
		return false;
	}

	vkBindImageMemory(m_device, m_cameraFrameRes[frameIndex], m_cameraFrameResMem[frameIndex], 0);

	TransitionImage(m_commandBuffer[m_frameIndex], m_cameraFrameRes[frameIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_cameraFrameRes[frameIndex];
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_cameraFrameResView[frameIndex]) != VK_SUCCESS)
	{
		ErrorLog("Shared texture vkCreateImageView failure!\n");
		vkDestroyImage(m_device, m_cameraFrameRes[frameIndex], nullptr);
		vkFreeMemory(m_device, m_cameraFrameResMem[frameIndex], nullptr);
		return false;
	}

	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

	if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_cameraFrameResArrayView[frameIndex]) != VK_SUCCESS)
	{
		ErrorLog("Shared texture vkCreateImageView failure!\n");
		vkDestroyImageView(m_device, m_cameraFrameResView[frameIndex], nullptr);
		vkDestroyImage(m_device, m_cameraFrameRes[frameIndex], nullptr);
		vkFreeMemory(m_device, m_cameraFrameResMem[frameIndex], nullptr);
		return false;
	}

	m_cameraFrameResExternalHandle[frameIndex] = frameResource;

	return true;
}

void PassthroughRendererVulkan::UpdateDescriptorSets(VkCommandBuffer commandBuffer, int swapchainIndex, const XrCompositionLayerProjection* layer, EPassthroughBlendMode blendMode)
{
	int viewIndex = swapchainIndex >= (NUM_SWAPCHAINS - 1) ? 1 : 0;

	VkDescriptorBufferInfo psPassBufferInfo{};
	psPassBufferInfo.buffer = m_psPassConstantBuffer[m_frameIndex];
	psPassBufferInfo.offset = 0;
	psPassBufferInfo.range = sizeof(PSPassConstantBuffer);

	VkDescriptorBufferInfo psMaskedBufferInfo{};
	psMaskedBufferInfo.buffer = m_psMaskedConstantBuffer[m_frameIndex];
	psMaskedBufferInfo.offset = 0;
	psMaskedBufferInfo.range = sizeof(PSMaskedConstantBuffer);

	VkDescriptorImageInfo cameraImageInfo{};
	cameraImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	if (m_configManager->GetConfig_Main().ShowTestImage)
	{
		cameraImageInfo.imageView = m_testPatternView;
		cameraImageInfo.sampler = m_cameraSampler;
	}
	else
	{
		cameraImageInfo.imageView = m_cameraFrameResView[m_frameIndex];
		cameraImageInfo.sampler = m_cameraSampler;
	}

	VkDescriptorImageInfo cameraImageArrayInfo{};
	cameraImageArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	cameraImageArrayInfo.imageView = m_cameraFrameResArrayView[m_frameIndex];
	cameraImageArrayInfo.sampler = m_cameraSampler;

	VkWriteDescriptorSet descriptorWrite[6]{};
	descriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite[0].dstSet = m_descriptorSets[swapchainIndex];
	descriptorWrite[0].dstBinding = 0;
	descriptorWrite[0].dstArrayElement = 0;
	descriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite[0].descriptorCount = 1;
	descriptorWrite[0].pBufferInfo = &psPassBufferInfo;

	descriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite[1].dstSet = m_descriptorSets[swapchainIndex];
	descriptorWrite[1].dstBinding = 1;
	descriptorWrite[1].dstArrayElement = 0;
	descriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite[1].descriptorCount = 1;
	descriptorWrite[1].pBufferInfo = &psMaskedBufferInfo;

	descriptorWrite[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite[2].dstSet = m_descriptorSets[swapchainIndex];
	descriptorWrite[2].dstBinding = 2;
	descriptorWrite[2].dstArrayElement = 0;
	descriptorWrite[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite[2].descriptorCount = 1;
	descriptorWrite[2].pImageInfo = &cameraImageInfo;

	int numdescriptors = 3;

	VkDescriptorImageInfo intermediateImageInfo{};
	VkDescriptorImageInfo originalRTImageInfo{};
	VkDescriptorImageInfo uvDistortionImageInfo{};

	if (blendMode == Masked)
	{
		XrRect2Di rect = layer->views[0].subImage.imageRect;

		// Recreate the intermediate rendertarget if it can't hold the entire viewport.
		if (!m_intermediateRenderTargets[swapchainIndex]
			|| m_intermediateRenderTargetWidth[viewIndex] < (uint32_t)rect.extent.width
			|| m_intermediateRenderTargetHeight[viewIndex] < (uint32_t)rect.extent.height)
		{
			SetupIntermediateRenderTarget(swapchainIndex, rect.extent.width, rect.extent.height);

			m_intermediateRenderTargetWidth[viewIndex] = (uint32_t)rect.extent.width;
			m_intermediateRenderTargetHeight[viewIndex] = (uint32_t)rect.extent.height;
		}
		else
		{
			TransitionImage(m_commandBuffer[m_frameIndex], m_intermediateRenderTargets[swapchainIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}

		intermediateImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		intermediateImageInfo.imageView = m_intermediateRenderTargetViews[swapchainIndex];
		intermediateImageInfo.sampler = m_intermediateSampler;

		originalRTImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		originalRTImageInfo.imageView = m_renderTargetViews[swapchainIndex];
		originalRTImageInfo.sampler = m_cameraSampler;

		descriptorWrite[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite[3].dstSet = m_descriptorSets[swapchainIndex];
		descriptorWrite[3].dstBinding = 3;
		descriptorWrite[3].dstArrayElement = 0;
		descriptorWrite[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite[3].descriptorCount = 1;
		descriptorWrite[3].pImageInfo = &intermediateImageInfo;

		descriptorWrite[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite[4].dstSet = m_descriptorSets[swapchainIndex];
		descriptorWrite[4].dstBinding = 4;
		descriptorWrite[4].dstArrayElement = 0;
		descriptorWrite[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite[4].descriptorCount = 1;
		descriptorWrite[4].pImageInfo = m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage ? &cameraImageArrayInfo : &originalRTImageInfo;

		numdescriptors = 5;

		if (m_configManager->GetConfig_Main().ProjectionMode != ProjectionRoomView2D)
		{
			uvDistortionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			uvDistortionImageInfo.imageView = m_uvDistortionMapView;
			uvDistortionImageInfo.sampler = m_cameraSampler;

			descriptorWrite[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite[5].dstSet = m_descriptorSets[swapchainIndex];
			descriptorWrite[5].dstBinding = 5;
			descriptorWrite[5].dstArrayElement = 0;
			descriptorWrite[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite[5].descriptorCount = 1;
			descriptorWrite[5].pImageInfo = &uvDistortionImageInfo;

			numdescriptors = 6;
		}
	}
	else if (m_configManager->GetConfig_Main().ProjectionMode != ProjectionRoomView2D)
	{
		uvDistortionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		uvDistortionImageInfo.imageView = m_uvDistortionMapView;
		uvDistortionImageInfo.sampler = m_cameraSampler;

		descriptorWrite[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite[3].dstSet = m_descriptorSets[swapchainIndex];
		descriptorWrite[3].dstBinding = 3;
		descriptorWrite[3].dstArrayElement = 0;
		descriptorWrite[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite[3].descriptorCount = 1;
		descriptorWrite[3].pImageInfo = &uvDistortionImageInfo;

		numdescriptors = 4;
	}
	else
	{
		descriptorWrite[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite[3].dstSet = m_descriptorSets[swapchainIndex];
		descriptorWrite[3].dstBinding = 3;
		descriptorWrite[3].dstArrayElement = 0;
		descriptorWrite[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite[3].descriptorCount = 1;
		descriptorWrite[3].pImageInfo = &cameraImageInfo;

		numdescriptors = 4;
	}

	vkUpdateDescriptorSets(m_device, numdescriptors, descriptorWrite, 0, nullptr);

	vkCmdBindDescriptorSets(m_commandBuffer[m_frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[swapchainIndex], 0, nullptr);
}



static bool g_bVulkanStereoErrorShown = false;

void PassthroughRendererVulkan::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams)
{

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Core& coreConf = m_configManager->GetConfig_Core();

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = 0;

	// TODO: Can't support stereo in Vulkan as long as SteamVR hangs when reading the camera frame buffer under it.
	if (mainConf.ProjectionMode == ProjectionStereoReconstruction)
	{
		if (!g_bVulkanStereoErrorShown)
		{
			g_bVulkanStereoErrorShown = true;
			ErrorLog("Stereo reconstruction is not currently supported under Vulkan!\n");
		}
		return;
	}

	vkBeginCommandBuffer(m_commandBuffer[m_frameIndex], &beginInfo);


	if (!mainConf.ShowTestImage && frame->frameTextureResource != nullptr)
	{
		if (!UpdateCameraFrameResource(m_commandBuffer[m_frameIndex], m_frameIndex, frame->frameTextureResource))
		{
			m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
			return;
		}
	}
	else if(!mainConf.ShowTestImage)
	{
		m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
		return;
	}

	{
		std::shared_lock readLock(distortionParams.readWriteMutex);

		if (mainConf.ProjectionMode != ProjectionRoomView2D &&
			(!m_uvDistortionMap || m_fovScale != distortionParams.fovScale))
		{
			m_fovScale = distortionParams.fovScale;
			SetupUVDistortionMap(distortionParams.uvDistortionMap);
		}
	}

	{
		PSPassConstantBuffer buffer = {};
		buffer.depthRange = XrVector2f(NEAR_PROJECTION_DISTANCE, mainConf.ProjectionDistanceFar);
		buffer.opacity = mainConf.PassthroughOpacity;
		buffer.brightness = mainConf.Brightness;
		buffer.contrast = mainConf.Contrast;
		buffer.saturation = mainConf.Saturation;
		buffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;
		buffer.bDebugDepth = mainConf.DebugDepth;
		buffer.bDebugValidStereo = mainConf.DebugStereoValid;
		buffer.bUseFisheyeCorrection = mainConf.ProjectionMode != ProjectionRoomView2D;

		memcpy(m_psPassConstantBufferMappings[m_frameIndex], &buffer, sizeof(PSPassConstantBuffer));
	}

	if (blendMode == Masked)
	{
		PSMaskedConstantBuffer maskedBuffer = {};
		maskedBuffer.maskedKey[0] = powf(coreConf.CoreForceMaskedKeyColor[0], 2.2f);
		maskedBuffer.maskedKey[1] = powf(coreConf.CoreForceMaskedKeyColor[1], 2.2f);
		maskedBuffer.maskedKey[2] = powf(coreConf.CoreForceMaskedKeyColor[2], 2.2f);
		maskedBuffer.maskedFracChroma = coreConf.CoreForceMaskedFractionChroma * 100.0f;
		maskedBuffer.maskedFracLuma = coreConf.CoreForceMaskedFractionLuma * 100.0f;
		maskedBuffer.maskedSmooth = coreConf.CoreForceMaskedSmoothing * 100.0f;
		maskedBuffer.bMaskedUseCamera = coreConf.CoreForceMaskedUseCameraImage;
		maskedBuffer.bMaskedInvert = coreConf.CoreForceMaskedInvertMask;

		memcpy(m_psMaskedConstantBufferMappings[m_frameIndex], &maskedBuffer, sizeof(PSMaskedConstantBuffer));

		RenderPassthroughViewMasked(LEFT_EYE, leftSwapchainIndex, layer, frame);
		RenderPassthroughViewMasked(RIGHT_EYE, rightSwapchainIndex, layer, frame);
	}
	else
	{
		// Descriptor sets are identical for both views except in masked mode.
		UpdateDescriptorSets(m_commandBuffer[m_frameIndex], leftSwapchainIndex, layer, blendMode);

		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode);
	}


	vkEndCommandBuffer(m_commandBuffer[m_frameIndex]);

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer[m_frameIndex];

	vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);

	m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
}


void PassthroughRendererVulkan::RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	VkCommandBuffer commandBuffer = m_commandBuffer[m_frameIndex];
	VkFramebuffer rendertarget = m_renderTargetFramebuffers[bufferIndex];

	if (!rendertarget) { return; }

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	VkViewport viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	VkRect2D scissor = { {rect.offset.x, rect.offset.y}, {(uint32_t)rect.offset.x + rect.extent.width, (uint32_t)rect.offset.y + rect.extent.height} };

	VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	renderPassInfo.renderPass = m_renderpass;
	renderPassInfo.framebuffer = rendertarget;
	renderPassInfo.renderArea = scissor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkDeviceSize vertOffset = 0;
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, &vertOffset);

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	VSConstantBuffer buffer = {};
	buffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	//buffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	buffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	buffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	buffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	buffer.projectionDistance = mainConf.ProjectionDistanceFar;
	buffer.floorHeightOffset = mainConf.FloorHeightOffset;

	vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VSConstantBuffer), &buffer);

	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(VSConstantBuffer), sizeof(PSViewConstantBuffer), &viewBuffer);


	// Extra draw if we need to preadjust the alpha.
	if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f)
	{
		VkPipeline prepassPipeline;

		if (blendMode == AlphaBlendPremultiplied || blendMode == AlphaBlendUnpremultiplied)
		{
			prepassPipeline = m_pipelinePrepassUseAppAlpha;
		}
		else
		{
			prepassPipeline = m_pipelinePrepassIgnoreAppAlpha;
		}

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, prepassPipeline);

		vkCmdDraw(commandBuffer, (uint32_t)m_vertices.size() / 3, 1, 0, 0);
	}


	VkPipeline mainpassPipeline;

	if (blendMode == AlphaBlendPremultiplied)
	{
		mainpassPipeline = m_pipelineAlphaPremultiplied;
	}
	else if (blendMode == AlphaBlendUnpremultiplied)
	{
		mainpassPipeline = m_pipelineDefault;
	}
	else if (blendMode == Additive)
	{
		mainpassPipeline = m_pipelineAlphaPremultiplied;
	}
	else
	{
		mainpassPipeline = m_pipelineDefault;
	}

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mainpassPipeline);

	vkCmdDraw(commandBuffer, (uint32_t)m_vertices.size() / 3, 1, 0, 0);


	vkCmdEndRenderPass(commandBuffer);
}


void PassthroughRendererVulkan::RenderPassthroughViewMasked(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	VkCommandBuffer commandBuffer = m_commandBuffer[m_frameIndex];
	VkFramebuffer rendertarget = m_renderTargetFramebuffers[bufferIndex];

	if (!rendertarget) { return; }

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	VkViewport viewport = { 0.0f, 0.0f, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	VkRect2D scissor = { {0, 0}, {(uint32_t)rect.extent.width, (uint32_t)rect.extent.height} };

	UpdateDescriptorSets(commandBuffer, bufferIndex, layer, Masked);

	TransitionImage(commandBuffer, m_renderTargets[bufferIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	renderPassInfo.renderPass = m_renderpassMaskedPrepass;
	renderPassInfo.framebuffer = m_intermediateRenderTargetFramebuffers[bufferIndex];
	renderPassInfo.renderArea = scissor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkDeviceSize vertOffset = 0;
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, &vertOffset);

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	VSConstantBuffer buffer = {};
	buffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	//buffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	buffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	buffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	buffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	buffer.projectionDistance = mainConf.ProjectionDistanceFar;
	buffer.floorHeightOffset = mainConf.FloorHeightOffset;

	vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VSConstantBuffer), &buffer);

	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;


	// Draw the correct half for single framebuffer views.
	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	{
		viewBuffer.prepassUVBounds = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f, 
			(eye == LEFT_EYE) ? 0.5f : 1.0f, 1.0f };
	}
	else
	{
		viewBuffer.prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	}

	vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(VSConstantBuffer), sizeof(PSViewConstantBuffer), &viewBuffer);


	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineMaskedPrepass);

	vkCmdDraw(commandBuffer, (uint32_t)m_vertices.size() / 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);

	TransitionImage(commandBuffer, m_renderTargets[bufferIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	TransitionImage(commandBuffer, m_intermediateRenderTargets[bufferIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	scissor = { rect.offset.x, rect.offset.y, (uint32_t)rect.offset.x + (uint32_t)rect.extent.width, (uint32_t)rect.offset.y + (uint32_t)rect.extent.height };

	renderPassInfo.renderPass = m_renderpass;
	renderPassInfo.framebuffer = rendertarget;
	renderPassInfo.renderArea = scissor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineMaskedRender);

	vkCmdDraw(commandBuffer, (uint32_t)m_vertices.size() / 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}


void* PassthroughRendererVulkan::GetRenderDevice()
{
	return m_device;
}
