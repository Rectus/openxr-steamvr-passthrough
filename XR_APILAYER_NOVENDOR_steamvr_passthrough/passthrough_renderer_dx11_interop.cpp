#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include "comdef.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


DXGI_FORMAT VulkanImageFormatToDXGI(VkFormat in)
{
	switch (in)
	{
	case VK_FORMAT_R8G8B8A8_SRGB:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case VK_FORMAT_B8G8R8A8_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_D32_SFLOAT:
		return DXGI_FORMAT_D32_FLOAT;
	case VK_FORMAT_D16_UNORM:
		return DXGI_FORMAT_D16_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

	case VK_FORMAT_UNDEFINED:
		return DXGI_FORMAT_UNKNOWN;

	default:
		ErrorLog("Unhandled Vulkan image format %d", in);
		return DXGI_FORMAT_UNKNOWN;
	}
}


void VulkanTransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
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
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		depFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = 0;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		depFlags = 0;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		depFlags = 0;
	}
	else
	{
		ErrorLog("Unknown layout transition!\n");
		return;
	}

	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, depFlags, 0, nullptr, 0, nullptr, 1, &barrier);
}


PassthroughRendererDX11Interop::PassthroughRendererDX11Interop(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager)
	: PassthroughRendererDX11(nullptr, dllModule, configManager)
	, m_applicationRenderAPI(DirectX12)
	, m_d3d12Device(device)
	, m_d3d12CommandQueue(commandQueue)
	, m_vulkanInstance(nullptr)
	, m_vulkanPhysDevice(nullptr)
	, m_vulkanDevice(nullptr)
	, m_vulkanQueueFamilyIndex(0)
	, m_vulkanQueueIndex(0)
	, m_vulkanQueue(nullptr)
{
}

PassthroughRendererDX11Interop::PassthroughRendererDX11Interop(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager)
	: PassthroughRendererDX11(nullptr, dllModule, configManager)
	, m_applicationRenderAPI(Vulkan)
	, m_d3d12Device(nullptr)
	, m_d3d12CommandQueue(nullptr)
	, m_vulkanInstance(binding.instance)
	, m_vulkanPhysDevice(binding.physicalDevice)
	, m_vulkanDevice(binding.device)
	, m_vulkanQueueFamilyIndex(binding.queueFamilyIndex)
	, m_vulkanQueueIndex(binding.queueIndex)
	, m_vulkanQueue(nullptr)
{
}

PassthroughRendererDX11Interop::~PassthroughRendererDX11Interop()
{
	if (m_applicationRenderAPI == Vulkan && m_vulkanDevice)
	{
		if (m_vulkanCommandPool) 
		{ 
			vkFreeCommandBuffers(m_vulkanDevice, m_vulkanCommandPool, NUM_SWAPCHAINS * 2, m_vulkanCommandBuffer); 
			vkDestroyCommandPool(m_vulkanDevice, m_vulkanCommandPool, nullptr);
		}
		for (int i = 0; i < NUM_SWAPCHAINS; i++)
		{
			if(m_localRTMemLeft[i]) { vkFreeMemory(m_vulkanDevice, m_localRTMemLeft[i], nullptr); }
			if (m_localRTMemRight[i]) { vkFreeMemory(m_vulkanDevice, m_localRTMemRight[i], nullptr); }
			if (m_localDBMemLeft[i]) { vkFreeMemory(m_vulkanDevice, m_localDBMemLeft[i], nullptr); }
			if (m_localDBMemRight[i]) { vkFreeMemory(m_vulkanDevice, m_localDBMemRight[i], nullptr); }

			if (m_swapchainsLeft[i]) { vkDestroyImage(m_vulkanDevice, m_swapchainsLeft[i], nullptr); }
			if (m_swapchainsRight[i]) { vkDestroyImage(m_vulkanDevice, m_swapchainsRight[i], nullptr); }
			if (m_depthBuffersLeft[i]) { vkDestroyImage(m_vulkanDevice, m_depthBuffersLeft[i], nullptr); }
			if (m_depthBuffersRight[i]) { vkDestroyImage(m_vulkanDevice, m_depthBuffersRight[i], nullptr); }

			if (m_localRendertargetsLeft[i]) { vkDestroyImage(m_vulkanDevice, m_localRendertargetsLeft[i], nullptr); }
			if (m_localRendertargetsRight[i]) { vkDestroyImage(m_vulkanDevice, m_localRendertargetsRight[i], nullptr); }
			if (m_localDepthBuffersLeft[i]) { vkDestroyImage(m_vulkanDevice, m_localDepthBuffersLeft[i], nullptr); }
			if (m_localDepthBuffersRight[i]) { vkDestroyImage(m_vulkanDevice, m_localDepthBuffersRight[i], nullptr); }
		}

		if (m_semaphoreFenceHandle) { CloseHandle(m_semaphoreFenceHandle); }
		if (m_semaphore) { vkDestroySemaphore(m_vulkanDevice, m_semaphore, nullptr); }
	}

}

bool PassthroughRendererDX11Interop::InitRenderer()
{

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,	D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

		if (FAILED(D3D11On12CreateDevice(m_d3d12Device.Get(), 0, featureLevels.data(), (UINT)featureLevels.size(), reinterpret_cast<IUnknown**>(m_d3d12CommandQueue.GetAddressOf()), 1, 0, &m_d3dDevice, nullptr, nullptr)))
		{
			ErrorLog("Failed to create D3D11 to D3D12 interop device!\n");
			return false;
		}

		if (FAILED(m_d3dDevice.As(&m_d3d11On12Device)))
		{
			ErrorLog("Failed to query D3D11 to D3D12 interop device!\n");
			return false;
		}

		break;
	}

	case Vulkan:
	{
		vkGetDeviceQueue(m_vulkanDevice, m_vulkanQueueFamilyIndex, m_vulkanQueueIndex, &m_vulkanQueue);

		VkPhysicalDeviceProperties2 deviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceIDProperties deviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		deviceProps.pNext = &deviceIDProps;
		vkGetPhysicalDeviceProperties2(m_vulkanPhysDevice, &deviceProps);

		IDXGIFactory1* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&factory))))
		{
			ErrorLog("CreateDXGIFactory failure!\n");
			return false;
		}

		UINT i = 0;
		IDXGIAdapter* adapter = nullptr;
		DXGI_ADAPTER_DESC desc;
		bool bFoundAdapter = false;
		while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
		{
			if (adapter->GetDesc(&desc) != S_OK)
			{
				ErrorLog("GetDesc failure!\n");
				return false;
			}
			bFoundAdapter = true;
			for (int j = 0; j < 8; j++)
			{
				if (((uint8_t*)&desc.AdapterLuid)[j] != deviceIDProps.deviceLUID[j])
				{
					bFoundAdapter = false;
					continue;
				}
			}
			if (bFoundAdapter)
			{
				break;
			}
			i++;
		}

		if (!bFoundAdapter)
		{
			adapter = nullptr;
		}

		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,	D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

		HRESULT res = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, featureLevels.data(), featureLevels.size(), D3D11_SDK_VERSION, &m_d3dDevice, NULL, &m_deviceContext);
		if (FAILED(res))
		{
			ErrorLog("D3D11CreateDevice failure: 0x%x\n", res);
			return false;
		}

		if (FAILED(m_d3dDevice->QueryInterface(__uuidof(ID3D11Device5), (void**)m_d3d11Device5.GetAddressOf())))
		{
			ErrorLog("Querying ID3D11Device5 failure!\n");
			return false;
		}

		if (FAILED(m_deviceContext->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)m_d3d11DeviceContext4.GetAddressOf())))
		{
			ErrorLog("Querying ID3D11DeviceContext4 failure!\n");
			return false;
		}

		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = m_vulkanQueueFamilyIndex;

		if (vkCreateCommandPool(m_vulkanDevice, &poolInfo, nullptr, &m_vulkanCommandPool) != VK_SUCCESS)
		{
			ErrorLog("vkCreateCommandPool failure!\n");
			return false;
		}

		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandPool = m_vulkanCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = NUM_SWAPCHAINS * 2;

		if (vkAllocateCommandBuffers(m_vulkanDevice, &allocInfo, m_vulkanCommandBuffer) != VK_SUCCESS)
		{
			ErrorLog("vkAllocateCommandBuffers failure!\n");
			vkDestroyCommandPool(m_vulkanDevice, m_vulkanCommandPool, nullptr);
			return false;
		}


		if (FAILED(m_d3d11Device5->CreateFence(1, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)m_semaphoreFence.GetAddressOf())))
		{
			ErrorLog("CreateFence failure!\n");
			return false;
		}

		VkSemaphoreTypeCreateInfo typeInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		typeInfo.initialValue = 1;

		VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		semaphoreInfo.pNext = &typeInfo;

		if (vkCreateSemaphore(m_vulkanDevice, &semaphoreInfo, nullptr, &m_semaphore) != VK_SUCCESS)
		{
			ErrorLog("vkCreateSemaphore failure!\n");
			return false;
		}

		m_semaphoreFence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &m_semaphoreFenceHandle);

		VkImportSemaphoreWin32HandleInfoKHR importInfo = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
		importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
		importInfo.handle = m_semaphoreFenceHandle;
		importInfo.semaphore = m_semaphore;

		PFN_vkImportSemaphoreWin32HandleKHR importFunc = (PFN_vkImportSemaphoreWin32HandleKHR)vkGetInstanceProcAddr(m_vulkanInstance, "vkImportSemaphoreWin32HandleKHR");

		if (importFunc(m_vulkanDevice, &importInfo) != VK_SUCCESS)
		{
			ErrorLog("vkImportSemaphoreWin32HandleKHR failure!\n");
			return false;
		}

		break;
	}

	case OpenGL:
	{
		break;
	}

	default:
	{
		break;
	}
	}

	if (!PassthroughRendererDX11::InitRenderer())
	{
		return false;
	}

	return true;
}

void PassthroughRendererDX11Interop::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		ID3D11Resource* res;
		D3D11_RESOURCE_FLAGS flags = {};
		flags.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		if (FAILED(m_d3d11On12Device->CreateWrappedResource((ID3D12Resource*)rendertarget, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, IID_PPV_ARGS(&res))))
		{
			ErrorLog("Failed to create wrapped rendertarget!\n");
			return;
		}

		PassthroughRendererDX11::InitRenderTarget(eye, res, imageIndex, swapchainInfo);

		break;
	}

	case Vulkan:
	{
		if (eye == LEFT_EYE)
		{
			m_swapchainsLeft[imageIndex] = (VkImage)rendertarget;
		}
		else
		{
			m_swapchainsRight[imageIndex] = (VkImage)rendertarget;
		}

		ID3D11Texture2D* d3dtexture;

		VkImage& localRenderTarget = (eye == LEFT_EYE) ? m_localRendertargetsLeft[imageIndex] : m_localRendertargetsRight[imageIndex];
		VkDeviceMemory& localRTMem = (eye == LEFT_EYE) ? m_localRTMemLeft[imageIndex] : m_localRTMemRight[imageIndex];

		HANDLE& handle = (eye == LEFT_EYE) ? m_sharedTextureLeft[imageIndex] : m_sharedTextureRight[imageIndex];

		if (CreateLocalTextureVulkan(localRenderTarget, localRTMem, &d3dtexture, handle, swapchainInfo, false))
		{
			XrSwapchainCreateInfo dxInfo = swapchainInfo;
			dxInfo.format = VulkanImageFormatToDXGI((VkFormat)swapchainInfo.format);
			PassthroughRendererDX11::InitRenderTarget(eye, d3dtexture, imageIndex, dxInfo);
		}

		break;
	}

	case OpenGL:
	{
		break;
	}

	default:
	{
		PassthroughRendererDX11::InitRenderTarget(eye, rendertarget, imageIndex, swapchainInfo);
		break;
	}
	}

	
}

void PassthroughRendererDX11Interop::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		ID3D11Resource* res;
		D3D11_RESOURCE_FLAGS flags = {};
		flags.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(m_d3d11On12Device->CreateWrappedResource((ID3D12Resource*)depthBuffer, &flags, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_WRITE, IID_PPV_ARGS(&res))))
		{
			ErrorLog("Failed to create wrapped depth stencil!\n");
			return;
		}


		PassthroughRendererDX11::InitDepthBuffer(eye, res, imageIndex, swapchainInfo);

		break;
	}

	case Vulkan:
	{
		if (eye == LEFT_EYE)
		{
			m_depthBuffersLeft[imageIndex] = (VkImage)depthBuffer;
		}
		else
		{
			m_depthBuffersRight[imageIndex] = (VkImage)depthBuffer;
		}

		ID3D11Texture2D* d3dtexture;

		VkImage& localDepthBuffer = (eye == LEFT_EYE) ? m_localDepthBuffersLeft[imageIndex] : m_localDepthBuffersRight[imageIndex];
		VkDeviceMemory& localDBMem = (eye == LEFT_EYE) ? m_localDBMemLeft[imageIndex] : m_localDBMemRight[imageIndex];

		HANDLE& handle = (eye == LEFT_EYE) ? m_sharedTextureLeft[imageIndex] : m_sharedTextureRight[imageIndex];

		if (CreateLocalTextureVulkan(localDepthBuffer, localDBMem, &d3dtexture, handle, swapchainInfo, true))
		{
			XrSwapchainCreateInfo dxInfo = swapchainInfo;
			dxInfo.format = VulkanImageFormatToDXGI((VkFormat)swapchainInfo.format);
			PassthroughRendererDX11::InitDepthBuffer(eye, d3dtexture, imageIndex, dxInfo);
		}

		
		break;
	}

	case OpenGL:
	{
		break;
	}

	default:
	{
		PassthroughRendererDX11::InitDepthBuffer(eye, depthBuffer, imageIndex, swapchainInfo);
		break;
	}
	}
}


bool PassthroughRendererDX11Interop::CreateLocalTextureVulkan(VkImage& localVulkanTexture, VkDeviceMemory& localVulkanTextureMemory, ID3D11Texture2D** localD3DTexture, HANDLE& sharedTextureHandle, const XrSwapchainCreateInfo& swapchainInfo, bool bIsDepthMap)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = VulkanImageFormatToDXGI((VkFormat)swapchainInfo.format);
	textureDesc.Width = swapchainInfo.width;
	textureDesc.Height = swapchainInfo.height;
	textureDesc.ArraySize = swapchainInfo.arraySize;
	textureDesc.MipLevels = swapchainInfo.mipCount;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = (bIsDepthMap ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	HRESULT res = m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, localD3DTexture);
	if (res != S_OK)
	{
		ErrorLog("Shared texture CreateTexture2D failure: 0x%x\n", res);
		return false;
	}


	{
		IDXGIResource* tempResource = NULL;
		(*localD3DTexture)->QueryInterface(__uuidof(IDXGIResource), (void**)&tempResource);
		tempResource->GetSharedHandle(&sharedTextureHandle);
		tempResource->Release();
	}

	if (localVulkanTexture)
	{
		vkDestroyImage(m_vulkanDevice, localVulkanTexture, nullptr);
		vkFreeMemory(m_vulkanDevice, localVulkanTextureMemory, nullptr);
	}

	VkExternalMemoryImageCreateInfo  extInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
	extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.pNext = &extInfo;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = swapchainInfo.width;
	imageInfo.extent.height = swapchainInfo.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = swapchainInfo.mipCount;
	imageInfo.arrayLayers = swapchainInfo.arraySize;
	imageInfo.format = (VkFormat)swapchainInfo.format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |(bIsDepthMap ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	if (vkCreateImage(m_vulkanDevice, &imageInfo, nullptr, &localVulkanTexture) != VK_SUCCESS)
	{
		ErrorLog("Shared texture vkCreateImage failure!\n");
		return false;
	}

	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_vulkanDevice, localVulkanTexture, &memReq);

	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_vulkanPhysDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
	{
		if ((memReq.memoryTypeBits & (1 << i))
			&& (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			VkImportMemoryWin32HandleInfoKHR handleInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
			handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
			handleInfo.handle = sharedTextureHandle;

			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocInfo.allocationSize = memReq.size;
			allocInfo.pNext = &handleInfo;
			allocInfo.memoryTypeIndex = i;
			if (vkAllocateMemory(m_vulkanDevice, &allocInfo, nullptr, &localVulkanTextureMemory) != VK_SUCCESS)
			{
				ErrorLog("Shared texture vkAllocateMemory failure!\n");
				vkDestroyImage(m_vulkanDevice, localVulkanTexture, nullptr);
				localVulkanTexture = nullptr;
				return false;
			}
			break;
		}
	}

	if (vkBindImageMemory(m_vulkanDevice, localVulkanTexture, localVulkanTextureMemory, 0) != VK_SUCCESS)
	{
		ErrorLog("Failed to bind shared texture memory!\n");
		vkDestroyImage(m_vulkanDevice, localVulkanTexture, nullptr);
		vkFreeMemory(m_vulkanDevice, localVulkanTextureMemory, nullptr);
		localVulkanTexture = nullptr;
		return false;
	}
	return true;
}


void PassthroughRendererDX11Interop::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams)
{
	DX11ViewData& viewDataLeft = m_viewData[0][leftSwapchainIndex];
	DX11ViewData& viewDataRight = m_viewData[1][rightSwapchainIndex];

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		{
			ID3D11Resource* rts[2] = { viewDataLeft.renderTarget.Get(), viewDataRight.renderTarget.Get() };
			m_d3d11On12Device->AcquireWrappedResources(rts, 2);

			ID3D11Resource* dts[2] = { 0 };

			if (m_viewDepthData[0].size() > leftDepthSwapchainIndex 
				&& m_viewDepthData[1].size() > rightDepthSwapchainIndex 
				&& m_viewDepthData[0][leftDepthSwapchainIndex].depthStencil 
				&& m_viewDepthData[1][rightDepthSwapchainIndex].depthStencil)
			{
				dts[0] = m_viewDepthData[0][leftDepthSwapchainIndex].depthStencil.Get();
				dts[1] = m_viewDepthData[1][rightDepthSwapchainIndex].depthStencil.Get();

				m_d3d11On12Device->AcquireWrappedResources(dts, 2);
			}

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, leftDepthSwapchainIndex, rightDepthSwapchainIndex, depthFrame, distortionParams, renderParams);

			m_d3d11On12Device->ReleaseWrappedResources(rts, 2);

			if (dts[0] != nullptr)
			{
				m_d3d11On12Device->ReleaseWrappedResources(dts, 2);
			}

			m_deviceContext->Flush();
		}

		break;
	}

	case Vulkan:
	{
		VkImageCopy copyLeft = {};
		copyLeft.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyLeft.srcSubresource.baseArrayLayer = layer->views[0].subImage.imageArrayIndex;
		copyLeft.srcSubresource.layerCount = 1;
		copyLeft.srcOffset.x = layer->views[0].subImage.imageRect.offset.x;
		copyLeft.srcOffset.y = layer->views[0].subImage.imageRect.offset.y;
		copyLeft.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyLeft.dstSubresource.baseArrayLayer = layer->views[0].subImage.imageArrayIndex;
		copyLeft.dstSubresource.layerCount = 1;
		copyLeft.dstOffset.x = layer->views[0].subImage.imageRect.offset.x;
		copyLeft.dstOffset.y = layer->views[0].subImage.imageRect.offset.y;
		copyLeft.extent.width = layer->views[0].subImage.imageRect.extent.width;
		copyLeft.extent.height = layer->views[0].subImage.imageRect.extent.height;
		copyLeft.extent.depth = 1;

		VkImageCopy copyRight = {};
		copyRight.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRight.srcSubresource.baseArrayLayer = layer->views[1].subImage.imageArrayIndex;
		copyRight.srcSubresource.layerCount = 1;
		copyRight.srcOffset.x = layer->views[1].subImage.imageRect.offset.x;
		copyRight.srcOffset.y = layer->views[1].subImage.imageRect.offset.y;
		copyRight.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRight.dstSubresource.baseArrayLayer = layer->views[1].subImage.imageArrayIndex;
		copyRight.dstSubresource.layerCount = 1;
		copyRight.dstOffset.x = layer->views[1].subImage.imageRect.offset.x;
		copyRight.dstOffset.y = layer->views[1].subImage.imageRect.offset.y;
		copyRight.extent.width = layer->views[1].subImage.imageRect.extent.width;
		copyRight.extent.height = layer->views[1].subImage.imageRect.extent.height;
		copyRight.extent.depth = 1;

		int frameIndex = leftSwapchainIndex;
		VkCommandBuffer& commandBuffer = m_vulkanCommandBuffer[frameIndex];

		{
			VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			beginInfo.flags = 0;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			VulkanTransitionImage(commandBuffer, m_swapchainsLeft[frameIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VulkanTransitionImage(commandBuffer, m_localRendertargetsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			vkCmdCopyImage(commandBuffer, m_swapchainsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_localRendertargetsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyLeft);

			//VulkanTransitionImage(commandBuffer, m_localRendertargetsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

			VulkanTransitionImage(commandBuffer, m_swapchainsRight[frameIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VulkanTransitionImage(commandBuffer, m_localRendertargetsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			vkCmdCopyImage(commandBuffer, m_swapchainsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_localRendertargetsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRight);

			//VulkanTransitionImage(commandBuffer, m_localRendertargetsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

			vkEndCommandBuffer(commandBuffer);

			
			VkTimelineSemaphoreSubmitInfo semaphoreInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			m_semaphoreValue++;
			semaphoreInfo.signalSemaphoreValueCount = 1;
			semaphoreInfo.pSignalSemaphoreValues = &m_semaphoreValue;

			VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submitInfo.pNext = &semaphoreInfo;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &m_semaphore;
			vkQueueSubmit(m_vulkanQueue, 1, &submitInfo, VK_NULL_HANDLE);
		}

		{
			m_d3d11DeviceContext4->Wait(m_semaphoreFence.Get(), m_semaphoreValue);

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, leftDepthSwapchainIndex, rightDepthSwapchainIndex, depthFrame, distortionParams, renderParams);

			m_semaphoreValue++;
			m_d3d11DeviceContext4->Signal(m_semaphoreFence.Get(), m_semaphoreValue);
		}

		{
			VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			beginInfo.flags = 0;

			VkCommandBuffer& commandBuffer2 = m_vulkanCommandBuffer[frameIndex + NUM_SWAPCHAINS];

			vkBeginCommandBuffer(commandBuffer2, &beginInfo);

			VulkanTransitionImage(commandBuffer2, m_localRendertargetsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VulkanTransitionImage(commandBuffer2, m_swapchainsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			vkCmdCopyImage(commandBuffer2, m_localRendertargetsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchainsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyLeft);

			VulkanTransitionImage(commandBuffer2, m_swapchainsLeft[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);



			VulkanTransitionImage(commandBuffer2, m_localRendertargetsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VulkanTransitionImage(commandBuffer2, m_swapchainsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			vkCmdCopyImage(commandBuffer2, m_localRendertargetsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchainsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRight);

			VulkanTransitionImage(commandBuffer2, m_swapchainsRight[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			vkEndCommandBuffer(commandBuffer2);

			VkTimelineSemaphoreSubmitInfo semaphoreInfo2 = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			semaphoreInfo2.waitSemaphoreValueCount = 1;
			semaphoreInfo2.pWaitSemaphoreValues = &m_semaphoreValue;

			VkSubmitInfo submitInfo2{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submitInfo2.pNext = &semaphoreInfo2;
			submitInfo2.commandBufferCount = 1;
			submitInfo2.pCommandBuffers = &commandBuffer2;
			submitInfo2.waitSemaphoreCount = 1;
			submitInfo2.pWaitSemaphores = &m_semaphore;
			VkPipelineStageFlags bits = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			submitInfo2.pWaitDstStageMask = &bits;

			vkQueueSubmit(m_vulkanQueue, 1, &submitInfo2, VK_NULL_HANDLE);
		}

		break;
	}

	case OpenGL:
	{
		break;
	}

	default:
	{
		PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, rightSwapchainIndex, leftDepthSwapchainIndex, depthFrame, distortionParams, renderParams);
		break;
	}
	}

	
}