
#include "pch.h"
#include "async_frame_decoder.h"
#include "mathutil.h"
#include "vulkan_util.h"

#include "shaders\vulkan_texture_decode.comp.spv.h"



struct alignas(16) ConversionPushConstants
{
	float brightness;
	float contrast;
	float saturation;
	float gammaCorrection;
};



static void SetVulkanDebugName(const VkDevice device, const void* object, const VkObjectType type, const char* name)
{
	if (!vkSetDebugUtilsObjectNameEXT)
	{
		return;
	}
	VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = type;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(object);
	nameInfo.pObjectName = name;

	vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
}


void AsyncFrameDecoder::Deinit()
{
	if (!m_device || !m_queue)
	{
		return;
	}

	if (m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
		m_pipeline = VK_NULL_HANDLE;
	}

	for (std::function<void()> deleteFunc : m_deletionQueue)
	{
		deleteFunc();
	}
}

bool AsyncFrameDecoder::Init(VkDevice device, VkPhysicalDevice physDevice, uint32_t queueFamilyIndex, uint32_t queueIndex, bool bRenderDocEnabled)
{
	m_bIsInitialized = false;
	m_device = device;
	m_physDevice = physDevice;
	m_queueFamilyIndex = queueFamilyIndex;
	m_bRenderDocEnabled = bRenderDocEnabled;

	// Get second queue allocated by main class.
	vkGetDeviceQueue(m_device, m_queueFamilyIndex, queueIndex, &m_queue);


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
		vkDestroyCommandPool(m_device, m_commandPool, nullptr);
		return false;
	}
	m_deletionQueue.push_back([=]() { vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer); });


	m_textureDecodeCS = CreateShaderModule(m_device, g_TextureDecodeCS, ARRAYSIZE(g_TextureDecodeCS) * sizeof(g_TextureDecodeCS[0]));
	if (m_textureDecodeCS == nullptr)
	{
		g_logger->error("Failed to create m_textureDecodeCS");
		return false;
	}
	SetVulkanDebugName(m_device, m_textureDecodeCS, VK_OBJECT_TYPE_SHADER_MODULE, "m_textureDecodeCS");
	m_deletionQueue.push_back([=]() { vkDestroyShaderModule(m_device, m_textureDecodeCS, nullptr); });

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS)
	{
		g_logger->error("vkCreateFence failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyFence(m_device, m_fence, nullptr); });

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

	VkDescriptorPoolSize poolSizes[2] =
	{
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}
	};

	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
	{
		g_logger->error("vkCreateDescriptorPool failure!");
		return false;
	}
	m_deletionQueue.push_back([=]() { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); });



	std::vector<VkDescriptorSetLayoutBinding> layoutBindings{};
	layoutBindings.push_back({ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &m_sampler });
	layoutBindings.push_back({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = (uint32_t)layoutBindings.size();
	layoutInfo.pBindings = layoutBindings.data();

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
	pushRange.size = sizeof(ConversionPushConstants);

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

	g_logger->info("Asynchronous frame decoder initialized");
	m_bIsInitialized = true;

	return true;
}

bool AsyncFrameDecoder::CreatePipeline()
{
	if (!m_bIsInitialized) { return false; }

	if (m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
		m_pipeline = VK_NULL_HANDLE;
	}

	std::vector<VkSpecializationMapEntry> entries;

	VkSpecializationMapEntry entry{};
	uint32_t offset = 0;

	entry.constantID = 0;
	entry.offset = offset;
	entry.size = sizeof(uint32_t);
	entries.push_back(entry);
	offset += (uint32_t)entry.size;

	entry.constantID = 1;
	entry.offset = offset;
	entry.size = sizeof(uint32_t);
	entries.push_back(entry);
	offset += (uint32_t)entry.size;

	entry.constantID = 2;
	entry.offset = offset;
	entry.size = sizeof(uint32_t);
	entries.push_back(entry);
	offset += (uint32_t)entry.size;

	entry.constantID = 3;
	entry.offset = offset;
	entry.size = sizeof(uint32_t);
	entries.push_back(entry);
	offset += (uint32_t)entry.size;

	VkSpecializationInfo specInfo{};
	specInfo.mapEntryCount = (uint32_t)entries.size();
	specInfo.pMapEntries = entries.data();
	specInfo.dataSize = sizeof(m_specConstants);
	specInfo.pData = &m_specConstants;

	VkPipelineShaderStageCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfo.module = m_textureDecodeCS;
	shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderInfo.pName = "main";
	shaderInfo.pSpecializationInfo = &specInfo;

	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.stage = shaderInfo;
	pipelineInfo.flags = 0;
	pipelineInfo.layout = m_pipelineLayout;

	VkResult res = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
	if (res != VK_SUCCESS)
	{
		g_logger->error("vkCreateComputePipelines failure {}", (uint32_t)res);
		return false;
	}

	return true;
}



bool AsyncFrameDecoder::CopyAndDecodeCameraFrame(std::shared_ptr<CameraCPUFrame> inFrame, VulkanTexture& rawTexture, VulkanTexture& sharedTexture)
{
	if (!m_bIsInitialized) { return false; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	bool bDoColorAdjustment = m_configManager->CheckEnableAsyncColorAdjustment() && (fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f || fabsf(mainConf.GammaCorrection - 1.0f) > 0.01f);

	if (inFrame->RawFrameFormat != m_specConstants.frameFormat ||
		bDoColorAdjustment != (m_specConstants.bDoColorAdjustment != 0))
	{
		m_specConstants.frameFormat = inFrame->RawFrameFormat;
		m_specConstants.bDoColorAdjustment = bDoColorAdjustment;
		m_specConstants.bInputIsSRGB = inFrame->RawFrameFormat == FrameFormat_RGBX32 || inFrame->RawFrameFormat == FrameFormat_RGB24;
		m_specConstants.bOutputIsSRGB = true;

		if (!CreatePipeline())
		{
			return false;
		}
	}


	vkResetFences(m_device, 1, &m_fence);

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = 0;

	vkBeginCommandBuffer(m_commandBuffer, &beginInfo);


	if (rawTexture.StagingBuffer == VK_NULL_HANDLE)
	{
		CopyHostImageToGPU(m_device, rawTexture, *inFrame->FrameBuffer.get());
	}
	else
	{
		memcpy(rawTexture.MappedMemory, inFrame->FrameBuffer->data(), inFrame->FrameBuffer->size());
		CopyTextureToGPU(m_commandBuffer, rawTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}


	VkDescriptorImageInfo rawFrameInfo{};
	rawFrameInfo.imageLayout = rawTexture.Layout;
	rawFrameInfo.imageView = rawTexture.View;
	rawFrameInfo.sampler = VK_NULL_HANDLE;

	VkDescriptorImageInfo sharedFrameInfo{};
	sharedFrameInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	sharedFrameInfo.imageView = sharedTexture.View;
	sharedFrameInfo.sampler = VK_NULL_HANDLE;

	std::vector<VkWriteDescriptorSet> descriptorWrite;

	VkWriteDescriptorSet desc{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	desc.dstSet = m_descriptorSet;

	desc.dstArrayElement = 0;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	desc.descriptorCount = 1;
	desc.pImageInfo = &rawFrameInfo;
	descriptorWrite.push_back(desc);

	desc.dstBinding = 1;
	desc.dstArrayElement = 0;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	desc.descriptorCount = 1;
	desc.pImageInfo = &sharedFrameInfo;
	descriptorWrite.push_back(desc);


	vkUpdateDescriptorSets(m_device, (uint32_t)descriptorWrite.size(), descriptorWrite.data(), 0, nullptr);
	vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
	

	ConversionPushConstants constants = {};
	constants.brightness = mainConf.Brightness;
	constants.contrast = mainConf.Contrast;
	constants.saturation = mainConf.Saturation;
	constants.gammaCorrection = 1.0f / mainConf.GammaCorrection;

	if (sharedTexture.Layout != VK_IMAGE_LAYOUT_GENERAL)
	{
		TransitionImage(m_commandBuffer, sharedTexture.Image, sharedTexture.Layout, VK_IMAGE_LAYOUT_GENERAL);
		sharedTexture.Layout = VK_IMAGE_LAYOUT_GENERAL;
	}

	int groupCountX;
	int groupCountY;

	if (inFrame->RawFrameFormat == FrameFormat_YUYV16)
	{
		groupCountX = DivRoundUp(rawTexture.Extent.width, 32);
		groupCountY = DivRoundUp(rawTexture.Extent.height, 32);
	}
	else
	{
		groupCountX = DivRoundUp(sharedTexture.Extent.width, 32);
		groupCountY = DivRoundUp(sharedTexture.Extent.height, 32);
	}

	vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ConversionPushConstants), &constants);

	vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
	vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, 1);


	// Add a RenderDoc frame end marker to allow captures from the UI.
	if (m_bRenderDocEnabled && m_configManager->GetConfig_Main().InsertFrameDecoderRenderDocMarkers)
	{
		VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = "vr-marker,frame_end,type,application";
		vkCmdInsertDebugUtilsLabelEXT(m_commandBuffer, &label);
	}

	vkEndCommandBuffer(m_commandBuffer);

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer;

	vkQueueSubmit(m_queue, 1, &submitInfo, m_fence);

	VkResult res = vkWaitForFences(m_device, 1, &m_fence, true, 1000 * 1000 * 100);
	if (res == VK_TIMEOUT)
	{
		g_logger->warn("vkWaitForFences timeout!");
	}
	if (res != VK_SUCCESS)
	{
		g_logger->error("vkWaitForFences failure: {}", (int32_t)res);
	}


	

	return true;
}
