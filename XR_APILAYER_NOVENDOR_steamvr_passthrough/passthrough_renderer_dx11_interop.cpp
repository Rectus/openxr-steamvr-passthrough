#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include "comdef.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


#define HANDLE_TYPE_D3D11_IMAGE_EXT 0x958B
#define HANDLE_TYPE_D3D11_IMAGE_KMT_EXT 0x958C
#define HANDLE_TYPE_OPAQUE_WIN32_EXT 0x9587
#define HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT 0x9588
#define HANDLE_TYPE_D3D12_FENCE_EXT 0x9594
#define D3D12_FENCE_VALUE_EXT 0x9595

#define TEXTURE_TILING_EXT 0x9580
#define OPTIMAL_TILING_EXT 0x9584
#define LINEAR_TILING_EXT 0x9585

#define GL_LAYOUT_GENERAL_EXT 38285
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT 38286
#define GL_LAYOUT_DEPTH_STENCIL_ATTACHMENT_EXT 38287

#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA8_SNORM 0x8F97
#define GL_RGBA32F 0x8814
#define GL_RGB32F 0x8815
#define GL_RGBA16F 0x881A
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH_COMPONENT32 0x81A7
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH32F_STENCIL8 0x8CAD
#define GL_DEPTH32F_STENCIL8_NV 0x8DAC



typedef void (*PFN_glGenSemaphoresEXT)(GLsizei n, GLuint* semaphores);
typedef void (*PFN_glDeleteSemaphoresEXT)(GLsizei n, GLuint* semaphores);
typedef void (*PFN_glIsSemaphoreEXT)(GLuint semaphore);
typedef void (*PFN_glSemaphoreParameterui64vEXT)(GLuint semaphore, GLenum pname, const uint64_t* params);
typedef void (*PFN_glGetSemaphoreParameterui64vEXT)(GLuint semaphore, GLenum pname, uint64_t* params);
typedef void (*PFN_glWaitSemaphoreEXT)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* srcLayouts);
typedef void (*PFN_glSignalSemaphoreEXT)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* dstLayouts);

typedef void (*PFN_glCreateMemoryObjectsEXT)(GLsizei n, GLuint* memoryObjects);
typedef void (*PFN_glDeleteMemoryObjectsEXT)(GLsizei n, GLuint* memoryObjects);
typedef void (*PFN_glImportMemoryWin32HandleEXT)(GLuint memory, uint64_t size, GLenum handleType, void* handle);
typedef void (*PFN_glImportSemaphoreWin32HandleEXT)(GLuint semaphore, GLenum handleType, void* handle);
typedef void (*PFN_glTexStorageMem2DEXT)(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, uint64_t offset);
typedef void (*PFN_glCopyImageSubData)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);


#define CAST_TEXTURE_TO_OPENGL_NAME(input) (static_cast<uint32_t>(reinterpret_cast<uint64_t>(input)))


bool IsDepthFormat(DXGI_FORMAT in)
{
	switch (in)
	{
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_D16_UNORM:
		return true;

	default:
		return false;
	}
}

DXGI_FORMAT DXGI_DepthFormatToTypeless(DXGI_FORMAT in)
{
	switch (in)
	{
	case DXGI_FORMAT_D32_FLOAT:
		return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_D16_UNORM:
		return DXGI_FORMAT_R16_TYPELESS;

	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

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
	case VK_FORMAT_R32G32B32_SFLOAT:
		return DXGI_FORMAT_R32G32B32_FLOAT;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_D32_SFLOAT:
		return DXGI_FORMAT_D32_FLOAT;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_D16_UNORM:
		return DXGI_FORMAT_D16_UNORM;

	case VK_FORMAT_UNDEFINED:
		return DXGI_FORMAT_UNKNOWN;

	default:
		ErrorLog("Unhandled Vulkan image format %d", in);
		return DXGI_FORMAT_UNKNOWN;
	}
}

DXGI_FORMAT OpenGLImageFormatToDXGI(int64_t in)
{
	switch (in)
	{
	case GL_SRGB8_ALPHA8:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	//case ?: There seems to be no way of specifying a BRGA foramt
	//	return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case GL_RGBA8:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case GL_RGBA8_SNORM:
		return DXGI_FORMAT_R8G8B8A8_SNORM;
	case GL_RGBA32F:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case GL_RGB32F:
		return DXGI_FORMAT_R32G32B32_FLOAT;
	case GL_RGBA16F:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case GL_RGB10_A2:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case GL_DEPTH_COMPONENT32F:
	case GL_DEPTH_COMPONENT32: // Assuming float for 32 bit depth buffers
		return DXGI_FORMAT_D32_FLOAT;
	case GL_DEPTH32F_STENCIL8:
	case GL_DEPTH32F_STENCIL8_NV:
		return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	case GL_DEPTH24_STENCIL8:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case GL_DEPTH_COMPONENT16:
		return DXGI_FORMAT_D16_UNORM;

	case GL_NONE:
		return DXGI_FORMAT_UNKNOWN;

	default:
		ErrorLog("Unhandled OpenGL image format 0x%x", in);
		return DXGI_FORMAT_UNKNOWN;
	}
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

PassthroughRendererDX11Interop::PassthroughRendererDX11Interop(const XrGraphicsBindingOpenGLWin32KHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager)
	: PassthroughRendererDX11(nullptr, dllModule, configManager)
	, m_applicationRenderAPI(OpenGL)
	, m_d3d12Device(nullptr)
	, m_d3d12CommandQueue(nullptr)
	, m_openglDeviceContext(binding.hDC)
	, m_openglRenderContext(binding.hGLRC)
{
}

PassthroughRendererDX11Interop::~PassthroughRendererDX11Interop()
{
	if (m_rendererInitialized)
	{
		ResetRenderer();
	}
}

void PassthroughRendererDX11Interop::ResetRenderer()
{
	std::unique_lock deleteLock(m_accessRendererMutex, std::chrono::milliseconds(50));

	m_viewData[0].clear();
	m_viewData[1].clear();
	m_viewDepthData[0].clear();
	m_viewDepthData[1].clear();
	m_frameData.clear();

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{

		break;
	}
	case Vulkan:
	{
		// Wait for queue to complete before freeing objects.
		if (m_vulkanRenderCompleteFence && vkGetFenceStatus(m_vulkanDevice, m_vulkanRenderCompleteFence) == VK_NOT_READY)
		{
			vkWaitForFences(m_vulkanDevice, 1, &m_vulkanRenderCompleteFence, true, 10000000);
			std::this_thread::yield();
		}

		for (int i = 0; i < NUM_SWAPCHAINS; i++)
		{
			if (m_localRTMemLeft[i]) { vkFreeMemory(m_vulkanDevice, m_localRTMemLeft[i], nullptr); }
			if (m_localRTMemRight[i]) { vkFreeMemory(m_vulkanDevice, m_localRTMemRight[i], nullptr); }
			if (m_localDBMemLeft[i]) { vkFreeMemory(m_vulkanDevice, m_localDBMemLeft[i], nullptr); }
			if (m_localDBMemRight[i]) { vkFreeMemory(m_vulkanDevice, m_localDBMemRight[i], nullptr); }

			if (m_localRendertargetsLeft[i]) { vkDestroyImage(m_vulkanDevice, m_localRendertargetsLeft[i], nullptr); }
			if (m_localRendertargetsRight[i]) { vkDestroyImage(m_vulkanDevice, m_localRendertargetsRight[i], nullptr); }
			if (m_localDepthBuffersLeft[i]) { vkDestroyImage(m_vulkanDevice, m_localDepthBuffersLeft[i], nullptr); }
			if (m_localDepthBuffersRight[i]) { vkDestroyImage(m_vulkanDevice, m_localDepthBuffersRight[i], nullptr); }

			m_localRTMemLeft[i] = VK_NULL_HANDLE;
			m_localRTMemRight[i] = VK_NULL_HANDLE;
			m_localDBMemLeft[i] = VK_NULL_HANDLE;
			m_localDBMemRight[i] = VK_NULL_HANDLE;
			m_localRendertargetsLeft[i] = VK_NULL_HANDLE;
			m_localRendertargetsRight[i] = VK_NULL_HANDLE;
			m_localDepthBuffersLeft[i] = VK_NULL_HANDLE;
			m_localDepthBuffersRight[i] = VK_NULL_HANDLE;
		}

		if (m_semaphoreFenceHandle) { CloseHandle(m_semaphoreFenceHandle); }
		if (m_semaphoreVulkan) { vkDestroySemaphore(m_vulkanDevice, m_semaphoreVulkan, nullptr); }
		if (m_vulkanRenderCompleteFence) { vkDestroyFence(m_vulkanDevice, m_vulkanRenderCompleteFence, nullptr); }

		m_semaphoreFenceHandle = VK_NULL_HANDLE;
		m_semaphoreVulkan = VK_NULL_HANDLE;
		m_vulkanRenderCompleteFence = VK_NULL_HANDLE;

		if (m_vulkanCommandPool)
		{
			vkFreeCommandBuffers(m_vulkanDevice, m_vulkanCommandPool, NUM_SWAPCHAINS * 2, m_vulkanCommandBuffer);
			vkDestroyCommandPool(m_vulkanDevice, m_vulkanCommandPool, nullptr);
			m_vulkanCommandPool = VK_NULL_HANDLE;
		}

		if (m_vulkanDownloadDevice) 
		{
			if (m_vulkanDownloadFence && vkGetFenceStatus(m_vulkanDownloadDevice, m_vulkanDownloadFence) == VK_NOT_READY)
			{
				vkWaitForFences(m_vulkanDownloadDevice, 1, &m_vulkanDownloadFence, true, 10000000);
				std::this_thread::yield();
			}

			if (m_vulkanDownloadBufferMemory) { vkFreeMemory(m_vulkanDownloadDevice, m_vulkanDownloadBufferMemory, nullptr); }
			if (m_vulkanDownloadBuffer) { vkDestroyBuffer(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, nullptr); }
			if (m_vulkanDownloadFence) { vkDestroyFence(m_vulkanDownloadDevice, m_vulkanDownloadFence, nullptr); }
			if (m_vulkanDownloadCommandBuffer) { vkFreeCommandBuffers(m_vulkanDownloadDevice, m_vulkanDownloadCommandPool, 1, &m_vulkanDownloadCommandBuffer); }
			if (m_vulkanDownloadCommandPool) { vkDestroyCommandPool(m_vulkanDownloadDevice, m_vulkanDownloadCommandPool, nullptr); }
			vkDestroyDevice(m_vulkanDownloadDevice, nullptr);

			m_vulkanDownloadBufferMemory = VK_NULL_HANDLE;
			m_vulkanDownloadBuffer = VK_NULL_HANDLE;
			m_vulkanDownloadFence = VK_NULL_HANDLE;
			m_vulkanDownloadCommandBuffer = VK_NULL_HANDLE;
			m_vulkanDownloadCommandPool = VK_NULL_HANDLE;
			m_vulkanDownloadDevice = VK_NULL_HANDLE;
		}

		break;
	}
	case OpenGL:
	{
		for (auto const& [key, val] : m_chainedSwapchains)
		{
			DestroyChainedSwapchain(key);
		}

		if (m_semaphoreFenceHandle) { CloseHandle(m_semaphoreFenceHandle); }
		
		break;
	}

	default:
	{
		break;
	}
	}
}

bool PassthroughRendererDX11Interop::InitRenderer()
{
	if (m_rendererInitialized)
	{
		ResetRenderer();
	}
	m_rendererInitialized = true;

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1 };

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
				factory->Release();
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
			adapter->Release();
			adapter = nullptr;
			i++;
		}

		factory->Release();

		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1 };

		HRESULT res = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, featureLevels.data(), (uint32_t)featureLevels.size(), D3D11_SDK_VERSION, &m_d3dDevice, NULL, &m_deviceContext);

		adapter->Release();

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

		if (vkCreateSemaphore(m_vulkanDevice, &semaphoreInfo, nullptr, &m_semaphoreVulkan) != VK_SUCCESS)
		{
			ErrorLog("vkCreateSemaphore failure!\n");
			return false;
		}

		m_semaphoreFence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &m_semaphoreFenceHandle);

		VkImportSemaphoreWin32HandleInfoKHR importInfo = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
		importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
		importInfo.handle = m_semaphoreFenceHandle;
		importInfo.semaphore = m_semaphoreVulkan;

		PFN_vkImportSemaphoreWin32HandleKHR importFunc = (PFN_vkImportSemaphoreWin32HandleKHR)vkGetInstanceProcAddr(m_vulkanInstance, "vkImportSemaphoreWin32HandleKHR");

		if (importFunc(m_vulkanDevice, &importInfo) != VK_SUCCESS)
		{
			ErrorLog("vkImportSemaphoreWin32HandleKHR failure!\n");
			return false;
		}

		VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

		if (vkCreateFence(m_vulkanDevice, &fenceInfo, nullptr, &m_vulkanRenderCompleteFence) != VK_SUCCESS)
		{
			ErrorLog("vkCreateFence failure!\n");
			return false;
		}



		{
			uint32_t familyPropsCount = 0;

			vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysDevice, &familyPropsCount, nullptr);

			std::vector<VkQueueFamilyProperties> familyProps;
			familyProps.resize(familyPropsCount);

			vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysDevice, &familyPropsCount, familyProps.data());

			bool bFoundQueue = false;

			for (uint32_t i = 0; i < familyPropsCount; i++)
			{
				// Prioritize queues without graphics or compute
				if (familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT && (familyProps[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
				{
					bFoundQueue = true;
					m_vulkanDownloadQueueFamilyIndex = i;
					break;
				}
				else if (familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT && (familyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					bFoundQueue = true;
					m_vulkanDownloadQueueFamilyIndex = i;
				}
				else if (!bFoundQueue && familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
				{
					bFoundQueue = true;
					m_vulkanDownloadQueueFamilyIndex = i;
				}			 
			}

			float queuePriority = 0.0;

			VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queueInfo.queueCount = 1;
			queueInfo.queueFamilyIndex = m_vulkanDownloadQueueFamilyIndex;
			queueInfo.pQueuePriorities = &queuePriority;

			VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			deviceInfo.queueCreateInfoCount = 1;
			deviceInfo.pQueueCreateInfos = &queueInfo;

			if (vkCreateDevice(m_vulkanPhysDevice, &deviceInfo, nullptr, &m_vulkanDownloadDevice) != VK_SUCCESS)
			{
				ErrorLog("vkCreateDevice failure!\n");
				return false;
			}

			vkGetDeviceQueue(m_vulkanDownloadDevice, m_vulkanDownloadQueueFamilyIndex, 0, &m_vulkanDownloadQueue);

			VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = m_vulkanDownloadQueueFamilyIndex;

			if (vkCreateCommandPool(m_vulkanDownloadDevice, &poolInfo, nullptr, &m_vulkanDownloadCommandPool) != VK_SUCCESS)
			{
				ErrorLog("vkCreateCommandPool failure!\n");
				return false;
			}

			VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocInfo.commandPool = m_vulkanDownloadCommandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(m_vulkanDownloadDevice, &allocInfo, &m_vulkanDownloadCommandBuffer) != VK_SUCCESS)
			{
				ErrorLog("vkAllocateCommandBuffers failure!\n");
				vkDestroyCommandPool(m_vulkanDownloadDevice, m_vulkanDownloadCommandPool, nullptr);
				return false;
			}

			VkFenceCreateInfo fenceInfo2 = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

			if (vkCreateFence(m_vulkanDownloadDevice, &fenceInfo2, nullptr, &m_vulkanDownloadFence) != VK_SUCCESS)
			{
				ErrorLog("vkCreateFence failure!\n");
				return false;
			}
		}
		

		break;
	}

	case OpenGL:
	{

		IDXGIFactory1* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&factory))))
		{
			ErrorLog("CreateDXGIFactory failure!\n");
			return false;
		}

		IDXGIAdapter* adapter = nullptr;

		// Assuming the default adapter is used, per the OpenXR spec for XrGraphicsBindingOpenGLWin32KHR.
		HRESULT res = factory->EnumAdapters(0, &adapter);

		factory->Release();

		if (res != S_OK)
		{
			ErrorLog("EnumAdapters failure: 0x%x\n", res);
			return false;	
		}

		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1 };

		res = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, featureLevels.data(), (uint32_t)featureLevels.size(), D3D11_SDK_VERSION, &m_d3dDevice, NULL, &m_deviceContext);

		adapter->Release();

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

		if (FAILED(m_d3d11Device5->CreateFence(1, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)m_semaphoreFence.GetAddressOf())))
		{
			ErrorLog("CreateFence failure!\n");
			return false;
		}

		HRESULT result = m_semaphoreFence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &m_semaphoreFenceHandle);
		/*IDXGIResource1* resource;
		m_semaphoreFence->QueryInterface(__uuidof(IDXGIResource1), (void**)&resource);
		result = resource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m_chainedSwapchains[swapchain][i].SharedHandle);
		HRESULT result = resource->GetSharedHandle(&m_semaphoreFenceHandle);
		resource->Release();*/

		if (FAILED(result))
		{
			ErrorLog("CreateSharedHandle failure: 0x%x\n", result);
			return false;
		}

		static PFN_glGenSemaphoresEXT glGenSemaphoresEXT = reinterpret_cast<PFN_glGenSemaphoresEXT>(wglGetProcAddress("glGenSemaphoresEXT"));
		static PFN_glImportSemaphoreWin32HandleEXT glImportSemaphoreWin32HandleEXT = reinterpret_cast<PFN_glImportSemaphoreWin32HandleEXT>(wglGetProcAddress("glImportSemaphoreWin32HandleEXT"));

		wglMakeCurrent(m_openglDeviceContext, m_openglRenderContext);

		glGenSemaphoresEXT(1, &m_semaphoreOpenGL);

		GLenum error = glGetError();
		if (GLenum error = glGetError() != GL_NO_ERROR)
		{
			ErrorLog("glGenSemaphoresEXT error: 0x%x\n", error);
		}

		glImportSemaphoreWin32HandleEXT(m_semaphoreOpenGL, HANDLE_TYPE_D3D12_FENCE_EXT, m_semaphoreFenceHandle);

		error = glGetError();
		if (GLenum error = glGetError() != GL_NO_ERROR)
		{
			ErrorLog("glImportSemaphoreWin32HandleEXT error: 0x%x\n", error);
		}


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

void PassthroughRendererDX11Interop::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain)
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

		PassthroughRendererDX11::InitRenderTarget(eye, res, imageIndex, swapchainInfo, swapchain);

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
			PassthroughRendererDX11::InitRenderTarget(eye, d3dtexture, imageIndex, dxInfo, swapchain);
		}

		break;
	}

	case OpenGL:
	{
		if (eye == LEFT_EYE)
		{
			m_rendertargetsOpenGLLeft[imageIndex] = CAST_TEXTURE_TO_OPENGL_NAME(rendertarget);
		}
		else
		{
			m_rendertargetsOpenGLRight[imageIndex] = CAST_TEXTURE_TO_OPENGL_NAME(rendertarget);
		}

		if (!m_chainedSwapchains.contains(swapchain))
		{
			ErrorLog("No chained swapchain found!\n");
			return;
		}

		XrSwapchainCreateInfo dxInfo = swapchainInfo;
		dxInfo.format = OpenGLImageFormatToDXGI(swapchainInfo.format);
		PassthroughRendererDX11::InitRenderTarget(eye, m_chainedSwapchains[swapchain][imageIndex].D3D11Texture.Get(), imageIndex, dxInfo, swapchain);

		break;
	}

	default:
	{
		PassthroughRendererDX11::InitRenderTarget(eye, rendertarget, imageIndex, swapchainInfo, swapchain);
		break;
	}
	}	
}

void PassthroughRendererDX11Interop::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo, const XrSwapchain swapchain)
{
	((eye == LEFT_EYE) ? m_depthSwapchainLeft : m_depthSwapchainRight) = swapchain;

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


		PassthroughRendererDX11::InitDepthBuffer(eye, res, imageIndex, swapchainInfo, swapchain);

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

		HANDLE& handle = (eye == LEFT_EYE) ? m_sharedDepthTextureLeft[imageIndex] : m_sharedDepthTextureRight[imageIndex];

		if (CreateLocalTextureVulkan(localDepthBuffer, localDBMem, &d3dtexture, handle, swapchainInfo, true))
		{
			XrSwapchainCreateInfo dxInfo = swapchainInfo;
			dxInfo.format = VulkanImageFormatToDXGI((VkFormat)swapchainInfo.format);
			PassthroughRendererDX11::InitDepthBuffer(eye, d3dtexture, imageIndex, dxInfo, swapchain);
		}

		
		break;
	}

	case OpenGL:
	{
		if (eye == LEFT_EYE)
		{
			m_depthBuffersOpenGLLeft[imageIndex] = CAST_TEXTURE_TO_OPENGL_NAME(depthBuffer);
		}
		else
		{
			m_depthBuffersOpenGLRight[imageIndex] = CAST_TEXTURE_TO_OPENGL_NAME(depthBuffer);
		}

		if (!m_chainedSwapchains.contains(swapchain))
		{
			ErrorLog("No chained swapchain found!\n");
			return;
		}
		
		XrSwapchainCreateInfo dxInfo = swapchainInfo;
		dxInfo.format = OpenGLImageFormatToDXGI(swapchainInfo.format);
		PassthroughRendererDX11::InitDepthBuffer(eye, m_chainedSwapchains[swapchain][imageIndex].D3D11Texture.Get(), imageIndex, dxInfo, swapchain);

		break;
	}

	default:
	{
		PassthroughRendererDX11::InitDepthBuffer(eye, depthBuffer, imageIndex, swapchainInfo, swapchain);
		break;
	}
	}
}


bool PassthroughRendererDX11Interop::CreateLocalTextureVulkan(VkImage& localVulkanTexture, VkDeviceMemory& localVulkanTextureMemory, ID3D11Texture2D** localD3DTexture, HANDLE& sharedTextureHandle, const XrSwapchainCreateInfo& swapchainInfo, bool bIsDepthMap)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Format = VulkanImageFormatToDXGI((VkFormat)swapchainInfo.format);
	textureDesc.Width = swapchainInfo.width;
	textureDesc.Height = swapchainInfo.height;
	textureDesc.ArraySize = swapchainInfo.arraySize;
	textureDesc.MipLevels = swapchainInfo.mipCount;
	textureDesc.SampleDesc.Count = swapchainInfo.sampleCount;
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


bool PassthroughRendererDX11Interop::CreateLocalTextureOpenGL(uint32_t& localOpenGLTexture, ID3D11Texture2D** localD3DTexture, HANDLE& sharedTextureHandle, const XrSwapchainCreateInfo& swapchainInfo, bool bIsDepthMap)
{
	/*D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Format = OpenGLImageFormatToDXGI(swapchainInfo.format);
	textureDesc.Width = swapchainInfo.width;
	textureDesc.Height = swapchainInfo.height;
	textureDesc.ArraySize = swapchainInfo.arraySize;
	textureDesc.MipLevels = swapchainInfo.mipCount;
	textureDesc.SampleDesc.Count = swapchainInfo.sampleCount;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = (bIsDepthMap ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	{
		IDXGIResource* tempResource = NULL;
		(*localD3DTexture)->QueryInterface(__uuidof(IDXGIResource), (void**)&tempResource);
		tempResource->GetSharedHandle(&sharedTextureHandle);
		tempResource->Release();
	}


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

	if (localOpenGLTexture != 0)
	{
		glDeleteTextures(1, &localOpenGLTexture);
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
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (bIsDepthMap ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	if (vkCreateImage(m_vulkanDevice, &imageInfo, nullptr, &localVulkanTexture) != VK_SUCCESS)
	{
		ErrorLog("Shared texture vkCreateImage failure!\n");
		return false;
	}*/

	return true;
}

struct ImageCopyData {
	VkImage sourceImage;
	VkImage destImage;
	VkImageLayout sourcePreLayout;
	VkImageLayout sourcePostLayout;
	VkImageLayout destPreLayout;
	VkImageLayout destPostLayout;
	XrSwapchainSubImage imageData;
	bool bIsDepth;
};


void VulkanCopyImages(VkCommandBuffer commandBuffer, std::vector<ImageCopyData>& dataVec, bool bCopyIn, uint32_t queueFamilyIndex)
{
	std::vector<VkImageMemoryBarrier> preBarriers;

	for (const ImageCopyData& data : dataVec)
	{
		{
			VkImageMemoryBarrier& barrier = preBarriers.emplace_back();
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = data.sourcePreLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcQueueFamilyIndex = bCopyIn ? VK_QUEUE_FAMILY_IGNORED : VK_QUEUE_FAMILY_EXTERNAL;
			barrier.dstQueueFamilyIndex = bCopyIn ? VK_QUEUE_FAMILY_IGNORED : queueFamilyIndex;
			barrier.image = data.sourceImage;
			barrier.subresourceRange.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = data.imageData.imageArrayIndex;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = bCopyIn ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		}

		{
			VkImageMemoryBarrier& barrier = preBarriers.emplace_back();
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = data.destPreLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = data.destImage;
			barrier.subresourceRange.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = data.imageData.imageArrayIndex;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = bCopyIn ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
	}

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, (uint32_t)preBarriers.size(), preBarriers.data());

	for (const ImageCopyData& data : dataVec)
	{
		VkImageCopy copy = {};
		copy.srcSubresource.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		copy.srcSubresource.baseArrayLayer = data.imageData.imageArrayIndex;
		copy.srcSubresource.layerCount = 1;
		copy.srcOffset.x = data.imageData.imageRect.offset.x;
		copy.srcOffset.y = data.imageData.imageRect.offset.y;
		copy.dstSubresource.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		copy.dstSubresource.baseArrayLayer = data.imageData.imageArrayIndex;
		copy.dstSubresource.layerCount = 1;
		copy.dstOffset.x = data.imageData.imageRect.offset.x;
		copy.dstOffset.y = data.imageData.imageRect.offset.y;
		copy.extent.width = data.imageData.imageRect.extent.width;
		copy.extent.height = data.imageData.imageRect.extent.height;
		copy.extent.depth = 1;

		vkCmdCopyImage(commandBuffer, data.sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}

	if (bCopyIn) { return; }

	std::vector<VkImageMemoryBarrier> postBarriers;

	for (const ImageCopyData& data : dataVec)
	{
		{
			VkImageMemoryBarrier& barrier = postBarriers.emplace_back();
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = data.sourcePostLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = data.sourceImage;
			barrier.subresourceRange.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = data.imageData.imageArrayIndex;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = 0;
		}

		{
			VkImageMemoryBarrier& barrier = postBarriers.emplace_back();
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = data.destPostLayout;
			barrier.srcQueueFamilyIndex = bCopyIn ? queueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = bCopyIn ? VK_QUEUE_FAMILY_EXTERNAL : VK_QUEUE_FAMILY_IGNORED;
			barrier.image = data.destImage;
			barrier.subresourceRange.aspectMask = data.bIsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = data.imageData.imageArrayIndex;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = 0;
		}
	}

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, (uint32_t)postBarriers.size(), postBarriers.data());
}



void PassthroughRendererDX11Interop::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, std::shared_ptr<CameraFrame> frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams)
{
	DX11ViewData& viewDataLeft = m_viewData[0][renderParams.LeftFrameIndex];
	DX11ViewData& viewDataRight = m_viewData[1][renderParams.RightFrameIndex];

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		{
			if (viewDataLeft.renderTarget.Texture.Get() == nullptr || viewDataRight.renderTarget.Texture.Get() == nullptr)
			{
				ErrorLog("Tried to render without initialized rendertarget texture!\n");
				return;
			}

			ID3D11Resource* rts[2] = { viewDataLeft.renderTarget.Texture.Get(), viewDataRight.renderTarget.Texture.Get() };
			m_d3d11On12Device->AcquireWrappedResources(rts, 2);

			ID3D11Resource* dts[2] = { 0 };

			if (m_viewDepthData[0].size() > renderParams.LeftDepthIndex
				&& m_viewDepthData[1].size() > renderParams.RightDepthIndex
				&& m_viewDepthData[0][renderParams.LeftDepthIndex].depthStencil
				&& m_viewDepthData[1][renderParams.RightDepthIndex].depthStencil)
			{
				dts[0] = m_viewDepthData[0][renderParams.LeftDepthIndex].depthStencil.Get();
				dts[1] = m_viewDepthData[1][renderParams.RightDepthIndex].depthStencil.Get();

				m_d3d11On12Device->AcquireWrappedResources(dts, 2);
			}

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, renderParams, depthFrame, distortionParams);

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
		int frameIndex = renderParams.LeftFrameIndex;

		{
			vkResetFences(m_vulkanDevice, 1, &m_vulkanRenderCompleteFence);

			VkCommandBuffer& commandBuffer = m_vulkanCommandBuffer[frameIndex];

			VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			beginInfo.flags = 0;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			std::vector<ImageCopyData> copyData;

			ImageCopyData& colorDataL = copyData.emplace_back();
			colorDataL.bIsDepth = false;
			colorDataL.sourceImage = m_swapchainsLeft[frameIndex];
			colorDataL.sourcePreLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorDataL.destImage = m_localRendertargetsLeft[frameIndex];
			colorDataL.destPreLayout = m_bFirstRender ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			colorDataL.imageData = layer->views[0].subImage;

			if (m_swapchainsRight[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& colorDataR = copyData.emplace_back();
				colorDataR.bIsDepth = false;
				colorDataR.sourceImage = m_swapchainsRight[frameIndex];
				colorDataR.sourcePreLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorDataR.destImage = m_localRendertargetsRight[frameIndex];
				colorDataR.destPreLayout = m_bFirstRender ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				colorDataR.imageData = layer->views[1].subImage;
			}
			m_bFirstRender = false;

			if (m_depthBuffersLeft[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& depthDataL = copyData.emplace_back();
				depthDataL.bIsDepth = true;
				depthDataL.sourceImage = m_depthBuffersLeft[frameIndex];
				depthDataL.sourcePreLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthDataL.destImage = m_localDepthBuffersLeft[frameIndex];
				depthDataL.destPreLayout = m_bFirstRender ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataL.imageData = layer->views[0].subImage;
			}

			if (m_depthBuffersLeft[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& depthDataR = copyData.emplace_back();
				depthDataR.bIsDepth = true;
				depthDataR.sourceImage = m_depthBuffersRight[frameIndex];
				depthDataR.sourcePreLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthDataR.destImage = m_localDepthBuffersRight[frameIndex];
				depthDataR.destPreLayout = m_bFirstRender ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataR.imageData = layer->views[1].subImage;
			}

			VulkanCopyImages(commandBuffer, copyData, true, m_vulkanQueueFamilyIndex);

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
			submitInfo.pSignalSemaphores = &m_semaphoreVulkan;
			vkQueueSubmit(m_vulkanQueue, 1, &submitInfo, VK_NULL_HANDLE);
		}

		{
			m_d3d11DeviceContext4->Wait(m_semaphoreFence.Get(), m_semaphoreValue);

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, renderParams, depthFrame, distortionParams);

			m_semaphoreValue++;
			m_d3d11DeviceContext4->Signal(m_semaphoreFence.Get(), m_semaphoreValue);
		}

		{
			VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			beginInfo.flags = 0;

			VkCommandBuffer& commandBuffer2 = m_vulkanCommandBuffer[frameIndex + NUM_SWAPCHAINS];

			vkBeginCommandBuffer(commandBuffer2, &beginInfo);

			std::vector<ImageCopyData> copyData;

			ImageCopyData& colorDataL = copyData.emplace_back();
			colorDataL.bIsDepth = false;
			colorDataL.sourceImage = m_localRendertargetsLeft[frameIndex];
			colorDataL.sourcePreLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			colorDataL.sourcePostLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			colorDataL.destImage = m_swapchainsLeft[frameIndex];
			colorDataL.destPreLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			colorDataL.destPostLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorDataL.imageData = layer->views[0].subImage;

			if (m_swapchainsRight[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& colorDataR = copyData.emplace_back();
				colorDataR.bIsDepth = false;
				colorDataR.sourceImage = m_localRendertargetsRight[frameIndex];
				colorDataR.sourcePreLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				colorDataR.sourcePostLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				colorDataR.destImage = m_swapchainsRight[frameIndex];
				colorDataR.destPreLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				colorDataR.destPostLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorDataR.imageData = layer->views[1].subImage;
			}

			if (m_depthBuffersLeft[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& depthDataL = copyData.emplace_back();
				depthDataL.bIsDepth = true;
				depthDataL.sourceImage = m_localDepthBuffersLeft[frameIndex];
				depthDataL.sourcePreLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				depthDataL.sourcePostLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataL.destImage = m_depthBuffersLeft[frameIndex];
				depthDataL.destPreLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataL.destPostLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthDataL.imageData = layer->views[0].subImage;
			}

			if (m_depthBuffersLeft[frameIndex] != VK_NULL_HANDLE)
			{
				ImageCopyData& depthDataR = copyData.emplace_back();
				depthDataR.bIsDepth = true;
				depthDataR.sourceImage = m_localDepthBuffersRight[frameIndex];
				depthDataR.sourcePreLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				depthDataR.sourcePostLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataR.destImage = m_depthBuffersRight[frameIndex];
				depthDataR.destPostLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				depthDataR.destPostLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthDataR.imageData = layer->views[1].subImage;
			}

			VulkanCopyImages(commandBuffer2, copyData, false, m_vulkanQueueFamilyIndex);

			vkEndCommandBuffer(commandBuffer2);

			VkTimelineSemaphoreSubmitInfo semaphoreInfo2 = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			semaphoreInfo2.waitSemaphoreValueCount = 1;
			semaphoreInfo2.pWaitSemaphoreValues = &m_semaphoreValue;

			VkSubmitInfo submitInfo2{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submitInfo2.pNext = &semaphoreInfo2;
			submitInfo2.commandBufferCount = 1;
			submitInfo2.pCommandBuffers = &commandBuffer2;
			submitInfo2.waitSemaphoreCount = 1;
			submitInfo2.pWaitSemaphores = &m_semaphoreVulkan;
			VkPipelineStageFlags bits = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			submitInfo2.pWaitDstStageMask = &bits;

			vkQueueSubmit(m_vulkanQueue, 1, &submitInfo2, m_vulkanRenderCompleteFence);
		}

		break;
	}

	case OpenGL:
	{
		{
			static PFN_glSemaphoreParameterui64vEXT glSemaphoreParameterui64vEXT = reinterpret_cast<PFN_glSemaphoreParameterui64vEXT>(wglGetProcAddress("glSemaphoreParameterui64vEXT"));
			static PFN_glWaitSemaphoreEXT glWaitSemaphoreEXT = reinterpret_cast<PFN_glWaitSemaphoreEXT>(wglGetProcAddress("glWaitSemaphoreEXT"));
			static PFN_glSignalSemaphoreEXT glSignalSemaphoreEXT = reinterpret_cast<PFN_glSignalSemaphoreEXT>(wglGetProcAddress("glSignalSemaphoreEXT"));

			wglMakeCurrent(m_openglDeviceContext, m_openglRenderContext);

			
			/*const XrSwapchainSubImage& subImageLeft = layer->views[0].subImage;
			const XrSwapchainSubImage& subImageRight = layer->views[1].subImage;
			std::vector<GLuint> inTextures;
			std::vector<GLuint> outTextures;
			std::vector<GLenum> inLayouts;
			std::vector<GLenum> outLayouts;

			if (subImageLeft.swapchain != XR_NULL_HANDLE && m_rendertargetsOpenGLLeft[renderParams.LeftFrameIndex] > 0)
			{
				inTextures.push_back(m_chainedSwapchains[subImageLeft.swapchain][renderParams.LeftFrameIndex].OpenGLTexture);
				inTextures.push_back(m_rendertargetsOpenGLLeft[renderParams.LeftFrameIndex]);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
			}
			if (subImageRight.swapchain != XR_NULL_HANDLE && m_rendertargetsOpenGLRight[renderParams.RightFrameIndex] > 0)
			{
				inTextures.push_back(m_chainedSwapchains[subImageRight.swapchain][renderParams.RightFrameIndex].OpenGLTexture);
				inTextures.push_back(m_rendertargetsOpenGLRight[renderParams.RightFrameIndex]);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
			}
			if (m_depthSwapchainLeft != XR_NULL_HANDLE && m_depthBuffersOpenGLLeft[renderParams.LeftDepthIndex] > 0)
			{
				inTextures.push_back(m_chainedSwapchains[m_depthSwapchainLeft][renderParams.LeftDepthIndex].OpenGLTexture);
				inTextures.push_back(m_depthBuffersOpenGLLeft[renderParams.LeftDepthIndex]);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
			}
			if (m_depthSwapchainLeft != XR_NULL_HANDLE && m_depthBuffersOpenGLRight[renderParams.RightDepthIndex] > 0)
			{
				inTextures.push_back(m_chainedSwapchains[m_depthSwapchainRight][renderParams.RightDepthIndex].OpenGLTexture);
				inTextures.push_back(m_depthBuffersOpenGLRight[renderParams.RightDepthIndex]);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
				inLayouts.push_back(GL_LAYOUT_GENERAL_EXT);
			}*/
			
			m_semaphoreValue++;
			glSemaphoreParameterui64vEXT(m_semaphoreOpenGL, D3D12_FENCE_VALUE_EXT, &m_semaphoreValue);

			//glSignalSemaphoreEXT(m_semaphoreOpenGL, 0, nullptr, static_cast<GLuint>(inTextures.size()), inTextures.data(), inLayouts.data());
			glSignalSemaphoreEXT(m_semaphoreOpenGL, 0, nullptr, 0, nullptr, nullptr);

			GLenum error = glGetError();
			if (GLenum error = glGetError() != GL_NO_ERROR)
			{
				ErrorLog("glSignalSemaphoreEXT error: 0x%x\n", error);
			}

			m_d3d11DeviceContext4->Wait(m_semaphoreFence.Get(), m_semaphoreValue);

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, renderParams, depthFrame, distortionParams);

			m_semaphoreValue++;
			m_d3d11DeviceContext4->Signal(m_semaphoreFence.Get(), m_semaphoreValue);

			wglMakeCurrent(m_openglDeviceContext, m_openglRenderContext);

			glSemaphoreParameterui64vEXT(m_semaphoreOpenGL, D3D12_FENCE_VALUE_EXT, &m_semaphoreValue);
			//glWaitSemaphoreEXT(m_semaphoreOpenGL, 0, nullptr, static_cast<GLuint>(inTextures.size()), inTextures.data(), inLayouts.data());
			glWaitSemaphoreEXT(m_semaphoreOpenGL, 0, nullptr, 0, nullptr, nullptr);

			error = glGetError();
			if (GLenum error = glGetError() != GL_NO_ERROR)
			{
				ErrorLog("glWaitSemaphoreEXT error: 0x%x\n", error);
			}
		}


		static PFN_glCopyImageSubData glCopyImageSubData = reinterpret_cast<PFN_glCopyImageSubData>(wglGetProcAddress("glCopyImageSubData"));

		if(layer->views[0].subImage.swapchain != XR_NULL_HANDLE && m_rendertargetsOpenGLLeft[renderParams.LeftFrameIndex] > 0)
		{
			const XrRect2Di& imageRect = layer->views[0].subImage.imageRect;
			uint32_t sourceTexture = m_chainedSwapchains[layer->views[0].subImage.swapchain][renderParams.LeftFrameIndex].OpenGLTexture;
			uint32_t destTexture = m_rendertargetsOpenGLLeft[renderParams.LeftFrameIndex];

			glCopyImageSubData(sourceTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, destTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, imageRect.extent.width, imageRect.extent.height, 1);
		}

		if (layer->views[1].subImage.swapchain != XR_NULL_HANDLE && m_rendertargetsOpenGLRight[renderParams.RightFrameIndex] > 0)
		{
			const XrRect2Di& imageRect = layer->views[1].subImage.imageRect;
			uint32_t sourceTexture = m_chainedSwapchains[layer->views[1].subImage.swapchain][renderParams.RightFrameIndex].OpenGLTexture;
			uint32_t destTexture = m_rendertargetsOpenGLRight[renderParams.RightFrameIndex];

			glCopyImageSubData(sourceTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, destTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, imageRect.extent.width, imageRect.extent.height, 1);
		}

		if (m_depthSwapchainLeft != XR_NULL_HANDLE && m_depthBuffersOpenGLLeft[renderParams.LeftDepthIndex] > 0)
		{
			const XrRect2Di& imageRect = layer->views[0].subImage.imageRect;
			uint32_t sourceTexture = m_chainedSwapchains[m_depthSwapchainLeft][renderParams.LeftDepthIndex].OpenGLTexture;
			uint32_t destTexture = m_depthBuffersOpenGLLeft[renderParams.LeftDepthIndex];

			glCopyImageSubData(sourceTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, destTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, imageRect.extent.width, imageRect.extent.height, 1);
		}

		if (m_depthSwapchainRight != XR_NULL_HANDLE && m_depthBuffersOpenGLRight[renderParams.RightDepthIndex] > 0)
		{
			const XrRect2Di& imageRect = layer->views[1].subImage.imageRect;
			uint32_t sourceTexture = m_chainedSwapchains[m_depthSwapchainRight][renderParams.RightDepthIndex].OpenGLTexture;
			uint32_t destTexture = m_depthBuffersOpenGLRight[renderParams.RightDepthIndex];

			glCopyImageSubData(sourceTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, destTexture, GL_TEXTURE_2D, 0, imageRect.offset.x, imageRect.offset.y, 0, imageRect.extent.width, imageRect.extent.height, 1);
		}

		break;
	}

	default:
	{
		PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, renderParams, depthFrame, distortionParams);
		break;
	}
	}
}


bool PassthroughRendererDX11Interop::DownloadTextureToCPU(const void* textureSRV, const uint32_t width, const uint32_t height, const uint32_t bufferSize, uint8_t* buffer)
{
	if (m_applicationRenderAPI != Vulkan)
	{
		return false;
	}

	vkResetFences(m_vulkanDownloadDevice, 1, &m_vulkanDownloadFence);

	ComPtr<IDXGIResource> dxgiRes;
	ID3D11Resource* res;
	((ID3D11ShaderResourceView*)textureSRV)->GetResource(&res);
	res->QueryInterface(IID_PPV_ARGS(&dxgiRes));
	HANDLE sharedHandle;
	dxgiRes->GetSharedHandle(&sharedHandle);

	VkImage gpuTexture = VK_NULL_HANDLE;
	VkDeviceMemory gpuTextureMemory = VK_NULL_HANDLE;

	{
		VkExternalMemoryImageCreateInfo  extInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.pNext = &extInfo;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(m_vulkanDownloadDevice, &imageInfo, nullptr, &gpuTexture) != VK_SUCCESS)
		{
			ErrorLog("Shared texture vkCreateImage failure!\n");
			return false;
		}

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(m_vulkanDownloadDevice, gpuTexture, &memReq);

		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(m_vulkanPhysDevice, &memProps);

		for (uint32_t j = 0; j < memProps.memoryTypeCount; j++)
		{
			if ((memReq.memoryTypeBits & (1 << j))
				&& (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
			{
				VkImportMemoryWin32HandleInfoKHR handleInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
				handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
				handleInfo.handle = sharedHandle;

				VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
				allocInfo.allocationSize = memReq.size;
				allocInfo.pNext = &handleInfo;
				allocInfo.memoryTypeIndex = j;
				if (vkAllocateMemory(m_vulkanDownloadDevice, &allocInfo, nullptr, &gpuTextureMemory) != VK_SUCCESS)
				{
					ErrorLog("Shared texture vkAllocateMemory failure!\n");
					vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
					return false;
				}
				break;
			}
		}

		if (gpuTextureMemory == VK_NULL_HANDLE)
		{
			ErrorLog("Shared texture memory prop not found!\n");
			vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
			return false;
		}

		vkBindImageMemory(m_vulkanDownloadDevice, gpuTexture, gpuTextureMemory, 0);
	}
	
	if (m_vulkanDownloadBuffer == VK_NULL_HANDLE || m_downloadBufferWidth != width || m_downloadBufferHeight != height)
	{
		m_downloadBufferWidth = width;
		m_downloadBufferHeight = height;

		if (m_vulkanDownloadBuffer != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_vulkanDownloadDevice, m_vulkanDownloadBufferMemory, nullptr);
			vkDestroyBuffer(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, nullptr);
			m_vulkanDownloadBufferMemory = VK_NULL_HANDLE;
			m_vulkanDownloadBuffer = VK_NULL_HANDLE;
		}

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.size = bufferSize;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		vkCreateBuffer(m_vulkanDownloadDevice, &bufferInfo, nullptr, &m_vulkanDownloadBuffer);

		VkMemoryRequirements memReq2{};
		vkGetBufferMemoryRequirements(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, &memReq2);

		VkPhysicalDeviceMemoryProperties memProps2{};
		vkGetPhysicalDeviceMemoryProperties(m_vulkanPhysDevice, &memProps2);

		for (uint32_t i = 0; i < memProps2.memoryTypeCount; i++)
		{
			if ((memReq2.memoryTypeBits & (1 << i))
				&& (memProps2.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))
			{
				VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
				allocInfo.allocationSize = memReq2.size;
				allocInfo.memoryTypeIndex = i;
				if (vkAllocateMemory(m_vulkanDownloadDevice, &allocInfo, nullptr, &m_vulkanDownloadBufferMemory) != VK_SUCCESS)
				{
					ErrorLog("Download buffer vkAllocateMemory failure!\n");
					vkDestroyBuffer(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, nullptr);
					m_vulkanDownloadBuffer = VK_NULL_HANDLE;
					vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
					vkFreeMemory(m_vulkanDownloadDevice, gpuTextureMemory, nullptr);
					return false;
				}

				break;
			}
		}

		if (!m_vulkanDownloadBufferMemory)
		{
			ErrorLog("Download buffer failure!\n");
			vkDestroyBuffer(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, nullptr);
			m_vulkanDownloadBuffer = VK_NULL_HANDLE;
			vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
			vkFreeMemory(m_vulkanDownloadDevice, gpuTextureMemory, nullptr);
			return false;
		}

		vkBindBufferMemory(m_vulkanDownloadDevice, m_vulkanDownloadBuffer, m_vulkanDownloadBufferMemory, 0);
	}

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = 0;

	vkBeginCommandBuffer(m_vulkanDownloadCommandBuffer, &beginInfo);

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
	barrier.dstQueueFamilyIndex = m_vulkanDownloadQueueFamilyIndex;
	barrier.image = gpuTexture;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	vkCmdPipelineBarrier(m_vulkanDownloadCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent.height = height;
	region.imageExtent.width = width;
	region.imageExtent.depth = 1;

	vkCmdCopyImageToBuffer(m_vulkanDownloadCommandBuffer, gpuTexture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_vulkanDownloadBuffer, 1, &region);

	vkEndCommandBuffer(m_vulkanDownloadCommandBuffer);

	VkSubmitInfo submitInfo2{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo2.commandBufferCount = 1;
	submitInfo2.pCommandBuffers = &m_vulkanDownloadCommandBuffer;

	vkQueueSubmit(m_vulkanDownloadQueue, 1, &submitInfo2, m_vulkanDownloadFence);

	if (vkWaitForFences(m_vulkanDownloadDevice, 1, &m_vulkanDownloadFence, true, 10000000) == VK_SUCCESS)
	{

		void* mappedData;
		if (vkMapMemory(m_vulkanDownloadDevice, m_vulkanDownloadBufferMemory, 0, bufferSize, 0, &mappedData) == VK_SUCCESS)
		{
			memcpy(buffer, mappedData, bufferSize);
			vkUnmapMemory(m_vulkanDownloadDevice, m_vulkanDownloadBufferMemory);

			vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
			vkFreeMemory(m_vulkanDownloadDevice, gpuTextureMemory, nullptr);

			return true;
		}
	}

	vkDestroyImage(m_vulkanDownloadDevice, gpuTexture, nullptr);
	vkFreeMemory(m_vulkanDownloadDevice, gpuTextureMemory, nullptr);

	return false;

}



bool PassthroughRendererDX11Interop::CreateChainedSwapchain(const XrSwapchain swapchain, const XrSwapchainCreateInfo& swapchainInfo, const int numImages, XrSwapchainImageBaseHeader* chainedImages)
{
	if (m_chainedSwapchains.contains(swapchain))
	{
		DestroyChainedSwapchain(swapchain);
	}

	switch (m_applicationRenderAPI)
	{
	case Vulkan:
	{
		


		return false;
	}

	case OpenGL:
	{
		XrSwapchainImageOpenGLKHR* images = reinterpret_cast<XrSwapchainImageOpenGLKHR*>(chainedImages);

		wglMakeCurrent(m_openglDeviceContext, m_openglRenderContext);

		m_chainedSwapchains[swapchain] = std::vector<DX11ChainedSwapchainTexture>(numImages);

		for (int i = 0; i < numImages; i++)
		{
			DXGI_FORMAT texFormat = OpenGLImageFormatToDXGI(swapchainInfo.format);

			D3D11_TEXTURE2D_DESC textureDesc = {};
			textureDesc.Format = IsDepthFormat(texFormat) ? DXGI_DepthFormatToTypeless(texFormat) : texFormat;
			textureDesc.Width = swapchainInfo.width;
			textureDesc.Height = swapchainInfo.height;
			textureDesc.ArraySize = swapchainInfo.arraySize;
			textureDesc.MipLevels = swapchainInfo.mipCount;
			textureDesc.SampleDesc.Count = swapchainInfo.sampleCount;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.BindFlags = IsDepthFormat(texFormat) ?
				(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL) :
				(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.CPUAccessFlags = 0;
			//textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
			textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

			HRESULT result = m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_chainedSwapchains[swapchain][i].D3D11Texture);

			if (FAILED(result))
			{
				ErrorLog("Shared texture CreateTexture2D failure: 0x%x\n", result);
				return false;
			}

			IDXGIResource1* resource;
			m_chainedSwapchains[swapchain][i].D3D11Texture->QueryInterface(__uuidof(IDXGIResource1), (void**)&resource);

			//result = resource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m_chainedSwapchains[swapchain][i].SharedHandle);
			result = resource->GetSharedHandle(&m_chainedSwapchains[swapchain][i].SharedHandle);
			resource->Release();

			if (FAILED(result))
			{
				ErrorLog("Shared texture GetSharedHandle failure: 0x%x\n", result);
				return false;
			}
			

			glGenTextures(1, &m_chainedSwapchains[swapchain][i].OpenGLTexture);
			glBindTexture(GL_TEXTURE_2D, m_chainedSwapchains[swapchain][i].OpenGLTexture);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glTexParameteri(GL_TEXTURE_2D, TEXTURE_TILING_EXT, OPTIMAL_TILING_EXT);
			
			
			static PFN_glCreateMemoryObjectsEXT glCreateMemoryObjectsEXT = reinterpret_cast<PFN_glCreateMemoryObjectsEXT>(wglGetProcAddress("glCreateMemoryObjectsEXT"));
			static PFN_glImportMemoryWin32HandleEXT glImportMemoryWin32HandleEXT = reinterpret_cast<PFN_glImportMemoryWin32HandleEXT>(wglGetProcAddress("glImportMemoryWin32HandleEXT"));
			static PFN_glTexStorageMem2DEXT glTexStorageMem2DEXT = reinterpret_cast<PFN_glTexStorageMem2DEXT>(wglGetProcAddress("glTexStorageMem2DEXT"));

			glCreateMemoryObjectsEXT(1, &m_chainedSwapchains[swapchain][i].OpenGLMemoryObject);

			GLenum error = glGetError();
			if (GLenum error = glGetError() != GL_NO_ERROR)
			{
				ErrorLog("glCreateMemoryObjectsEXT error: 0x%x\n", error);
			}

			glImportMemoryWin32HandleEXT(m_chainedSwapchains[swapchain][i].OpenGLMemoryObject, 0, HANDLE_TYPE_D3D11_IMAGE_KMT_EXT, static_cast<void*>(m_chainedSwapchains[swapchain][i].SharedHandle));

			error = glGetError();
			if (GLenum error = glGetError() != GL_NO_ERROR)
			{
				ErrorLog("glImportMemoryWin32HandleEXT error: 0x%x\n", error);
			}

			glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, static_cast<GLenum>(swapchainInfo.format), swapchainInfo.width, swapchainInfo.height, m_chainedSwapchains[swapchain][i].OpenGLMemoryObject, 0);

			error = glGetError();
			if (GLenum error = glGetError() != GL_NO_ERROR)
			{
				ErrorLog("glTexStorageMem2DEXT error: 0x%x\n", error);
			}
			

			images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			images[i].next = nullptr;
			images[i].image = m_chainedSwapchains[swapchain][i].OpenGLTexture;
		}

		return true;
	}

	default:
	{
		return false;
	}
	}
}

void PassthroughRendererDX11Interop::DestroyChainedSwapchain(const XrSwapchain swapchain)
{
	if (!m_chainedSwapchains.contains(swapchain))
	{
		return;
	}

	for (int i = 0; i < m_chainedSwapchains[swapchain].size(); i++)
	{
		switch (m_applicationRenderAPI)
		{
		case Vulkan:

			break;

		case OpenGL:

			wglMakeCurrent(m_openglDeviceContext, m_openglRenderContext);

			static PFN_glDeleteMemoryObjectsEXT glDeleteMemoryObjectsEXT = reinterpret_cast<PFN_glDeleteMemoryObjectsEXT>(wglGetProcAddress("glDeleteMemoryObjectsEXT"));
			if (glDeleteMemoryObjectsEXT != nullptr)
			{
				glDeleteMemoryObjectsEXT(1, &m_chainedSwapchains[swapchain][i].OpenGLTexture);
				glDeleteTextures(1, &m_chainedSwapchains[swapchain][i].OpenGLTexture);
			}

			break;

		default:
			break;
		}

		/*if (m_chainedSwapchains[swapchain][i].SharedHandle)
		{
			CloseHandle(m_chainedSwapchains[swapchain][i].SharedHandle);
		}*/
		m_chainedSwapchains[swapchain][i].D3D11Texture.Reset();

	}

	m_chainedSwapchains.erase(swapchain);
}

