
#pragma once

#include "layer_structs.h"
#include "config_manager.h"

namespace
{
	struct alignas(4) ConversionSpecializationConstants
	{
		uint32_t frameFormat;
		VkBool32 bDoColorAdjustment;
		VkBool32 bInputIsSRGB;
		VkBool32 bOutputIsSRGB;
	};
}

class AsyncFrameDecoder
{
public:
	AsyncFrameDecoder(std::shared_ptr<ConfigManager> configManager)
		: m_configManager(configManager)
	{
	}
	void Deinit();
	bool Init(VkDevice device, VkPhysicalDevice physDevice, uint32_t queueFamilyIndex, uint32_t queueIndex, bool bRenderDocEnabled);
	bool CreatePipeline();
	bool CopyAndDecodeCameraFrame(std::shared_ptr<CameraCPUFrame> inFrame, VulkanTexture& rawTexture, VulkanTexture& sharedTexture);

private:

	std::shared_ptr<ConfigManager> m_configManager;

	std::deque<std::function<void()>> m_deletionQueue;

	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = 0;
	uint32_t m_queueIndex = 0;
	bool m_bRenderDocEnabled = false;
	bool m_bIsInitialized = false;

	VkQueue m_queue = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;

	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

	VkFence m_fence = VK_NULL_HANDLE;

	VkShaderModule m_textureDecodeCS = VK_NULL_HANDLE;

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkRenderPass m_renderpass = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;

	VkSampler m_sampler = VK_NULL_HANDLE;

	ConversionSpecializationConstants m_specConstants{};
};

