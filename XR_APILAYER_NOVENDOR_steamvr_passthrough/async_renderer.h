
#pragma once

#include "layer_structs.h"
#include "config_manager.h"
#include "async_frame_decoder.h"

class IPassthroughRenderer;


namespace
{
	struct alignas(4) AsyncSpecializationConstants
	{
		float minDisparity;
		float maxDisparity;
		VkBool32 bUseInputConfidence;
		float bilateralDispCutoff;
		uint32_t bilateralDistance;
	};
}


class AsyncRenderer
{
public:
	AsyncRenderer(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<IPassthroughRenderer> baseRenderer)
		: m_configManager(configManager)
		, m_inlineRenderer(baseRenderer)
		, m_frameDecoder(configManager)
		, m_bIsInitialized(false)
	{}
	~AsyncRenderer();
	bool InitRenderer();
	bool CreatePipeline();
	bool CopyAndDecodeCameraFrame(std::shared_ptr<CameraCPUFrame> inFrame, void** nativeTexture);
	bool BeginRender(std::shared_ptr<DepthFrame> depthFrame, const Config_Stereo& stereoConf);
	void CopyDisparityToGPU(std::vector<uint8_t>& buffer);
	void CopyConfidenceToGPU(std::vector<uint8_t>& buffer);
	void CopyBWRectifiedCameraFrameToGPU(std::vector<uint8_t>& buffer);
	void Render(std::shared_ptr<DepthFrame> depthFrame, const Config_Stereo& stereoConf);

private:

	bool CreateBuffer(VkBuffer& buffer, VkDeviceMemory& bufferMem, VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, std::deque<std::function<void()>>* deletionQueue);
	bool CreateTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags);
	bool CreateSharedTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags);
	void DestroyTexture(VulkanTexture& texture);
	void ComputeFilterKernels();

	AsyncFrameDecoder m_frameDecoder;
	std::shared_mutex m_accessMutex;

	std::shared_ptr<ConfigManager> m_configManager;
	std::weak_ptr<IPassthroughRenderer> m_inlineRenderer;

	std::deque<std::function<void()>> m_deletionQueue;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties m_memProps{};
	VkDevice m_device = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = 0;
	uint32_t m_queueIndex = 0;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	bool m_bHostImageCopyEnabled = false;
	bool m_bSamplerYcbcrConversionEnabled = false;
	std::atomic_bool m_bIsInitialized = false;

	VkFence m_renderFence = VK_NULL_HANDLE;

	VkShaderModule m_disparityFillHolesCS = VK_NULL_HANDLE;
	VkShaderModule m_disparityJointBilateralCS = VK_NULL_HANDLE;

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipelineFillHoles = VK_NULL_HANDLE;
	VkPipeline m_pipelineJointBilateral = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;

	VkSampler m_sampler = VK_NULL_HANDLE;

	VulkanTexture m_rawCameraTexture[5] = {};
	VulkanTexture m_sharedCameraTexture[5] = {};
	int m_cameraTextureIndex = -1;

	VulkanTexture m_bwRectifiedCameraTexture;
	VulkanTexture m_disparityTexture;
	VulkanTexture m_confidenceTexture;
	VulkanTexture m_outputTexture[5] = {};

	VkBuffer m_filterKernelBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_filterKernelBufferMem = VK_NULL_HANDLE;

	uint32_t m_bilateralDistance = 0;
	float m_bilateralSigmaSpace = 0.0f;
	float m_bilateralSigmaLuma = 0.0f;

	AsyncSpecializationConstants m_specConstants{};
};

