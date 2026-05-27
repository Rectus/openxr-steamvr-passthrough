
#pragma once

#include "layer_structs.h"
#include "config_manager.h"


 class IPassthroughRenderer;

struct VulkanTexture
{
	VkImage Image = VK_NULL_HANDLE;
	VkDeviceMemory 	Memory = VK_NULL_HANDLE;
	VkImageView View = VK_NULL_HANDLE;
	VkBuffer StagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory 	StagingBufferMemory = VK_NULL_HANDLE;
	uint8_t* MappedMemory = nullptr;

	HANDLE SharedHandle = INVALID_HANDLE_VALUE;
	void* nativeTexture = NULL;

	VkExtent2D Extent = { 0, 0 };
	bool bIsValid = false;
	VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

class AsyncRenderer
{
public:
	AsyncRenderer(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<IPassthroughRenderer> baseRenderer)
		: m_configManager(configManager)
		, m_baseRenderer(baseRenderer)
	{}
	~AsyncRenderer();
	bool InitRenderer();
	bool BeginRender(std::shared_ptr<DepthFrame> depthFrame);
	void CopyDisparityToGPU(std::vector<uint8_t>& buffer);
	void CopyConfidenceToGPU(std::vector<uint8_t>& buffer);
	bool CopyCameraFrameToGPU(std::vector<uint8_t>& buffer, VkExtent2D extent, void** nativeTexture);
	void CopyBWRectifiedCameraFrameToGPU(std::vector<uint8_t>& buffer);
	void Render(std::shared_ptr<DepthFrame> depthFrame, const Config_Stereo& stereoConf);

private:

	VkShaderModule CreateShaderModule(const uint32_t* bytecode, size_t codeSize);
	bool CreateBuffer(VkBuffer& buffer, VkDeviceMemory& bufferMem, VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, std::deque<std::function<void()>>* deletionQueue);
	bool CreateTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags);
	bool CreateSharedTexture(VulkanTexture& texture, VkExtent2D extent, VkFormat format, VkImageUsageFlags usageFlags, bool bCPUTransfer);
	void DestroyTexture(VulkanTexture& texture);
	void ComputeFilterKernels();


	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<IPassthroughRenderer> m_baseRenderer;

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

	VkFence m_renderFence = VK_NULL_HANDLE;
	VkFence m_transferFence = VK_NULL_HANDLE;

	VkShaderModule m_disparityFillHolesCS = VK_NULL_HANDLE;
	VkShaderModule m_disparityJointBilateralCS = VK_NULL_HANDLE;

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipelineFillHoles = VK_NULL_HANDLE;
	VkPipeline m_pipelineJointBilateral = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
	
	VkSampler m_sampler = VK_NULL_HANDLE;

	VulkanTexture m_cameraTexture[3] = {};
	int m_cameraTextureIndex = -1;
	VulkanTexture m_bwRectifiedCameraTexture;
	VulkanTexture m_disparityTexture;
	VulkanTexture m_confidenceTexture;
	VulkanTexture m_outputTexture[3] = {};

	VkBuffer m_filterKernelBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_filterKernelBufferMem = VK_NULL_HANDLE;
	
	float m_minDisparity = 0.0f;
	float m_maxDisparity = 0.0f;

	uint32_t m_bilateralDistance = 0;
	float m_bilateralSigmaSpace = 0.0f;
	float m_bilateralSigmaLuma = 0.0f;
};

