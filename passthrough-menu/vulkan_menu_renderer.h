
#pragma once

#include <imgui_impl_vulkan.h>

class DesktopWindowWin32;

class VulkanMenuRenderer
{
public:
	bool SetupRenderer(std::shared_ptr<DesktopWindowWin32> window);
	void InitImGui();

	void ResizeWindow(uint32_t width, uint32_t height);
	bool RenderMenu(bool bRenderOffscreen);
	vr::VRVulkanTextureData_t* GetOverlayTextureData() { return &m_textureData; };

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
	bool m_swapChainRebuild = false;

};

