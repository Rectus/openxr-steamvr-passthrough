
#pragma once

#include <imgui_impl_vulkan.h>

class DesktopWindowWin32;

struct ThumbnailData
{
	vr::VRVulkanTextureData_t TextureData;
	VkDeviceMemory  ImageMemory;
};

class VulkanMenuRenderer
{
public:
	bool SetupRenderer(std::shared_ptr<DesktopWindowWin32> window);
	void InitImGui();
	void CreateDashboardThumbnails(std::vector<std::vector<uint8_t>>& imageData, uint32_t width, uint32_t height);

	void ResizeWindow(uint32_t width, uint32_t height);
	bool RenderMenu(bool bRenderOffscreen);
	vr::VRVulkanTextureData_t* GetOverlayTextureData() { return &m_textureData; };
	vr::VRVulkanTextureData_t* GetThumbnailTextureData(EWindowIcon icon) { return &m_thumbnailData[icon].TextureData; };

	void WaitDeinitImGui();
	void CleanupRenderer();

private:

	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	uint32_t m_queueFamily = (uint32_t)-1;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	ImGui_ImplVulkanH_Window m_windowData = {};
	vr::VRVulkanTextureData_t m_textureData = {};
	ThumbnailData m_thumbnailData[4] = {};
	bool m_swapChainRebuild = false;

};

