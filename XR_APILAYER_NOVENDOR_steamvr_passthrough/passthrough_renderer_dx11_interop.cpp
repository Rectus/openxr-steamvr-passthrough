#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include "comdef.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;

PassthroughRendererDX11Interop::PassthroughRendererDX11Interop(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager)
	: PassthroughRendererDX11(nullptr, dllModule, configManager)
	, m_applicationRenderAPI(DirectX12)
	, m_d3d12Device(device)
	, m_d3d12CommandQueue(commandQueue)
{
	m_VkInstance = nullptr;
	m_VkPhysDevice = nullptr;
	m_VkDevice = nullptr;
	m_VkQueueFamilyIndex = 0;
	m_VkQueueIndex = 0;
}

PassthroughRendererDX11Interop::PassthroughRendererDX11Interop(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager)
	: PassthroughRendererDX11(nullptr, dllModule, configManager)
	, m_applicationRenderAPI(Vulkan)
	, m_d3d12Device(nullptr)
	, m_d3d12CommandQueue(nullptr)
{
	m_VkInstance = binding.instance;
	m_VkPhysDevice = binding.physicalDevice;
	m_VkDevice = binding.device;
	m_VkQueueFamilyIndex = binding.queueFamilyIndex;
	m_VkQueueIndex = binding.queueIndex;
}

bool PassthroughRendererDX11Interop::InitRenderer()
{
	

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
		/*LUID luid = m_d3d12Device->GetAdapterLuid();

		ComPtr<IDXGIAdapter1> adapter;
		ComPtr<IDXGIFactory1> dxgiFactory;
		CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

		UINT adapterIndex = 0;

		while (dxgiFactory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_ADAPTER_DESC1 adapterDesc;
			adapter->GetDesc1(&adapterDesc);
			if (memcmp(&adapterDesc.AdapterLuid, &luid, sizeof(luid)) == 0)
			{
				break;
			}
			adapterIndex++;
		}
		
		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,	D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

		HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevels.data(), featureLevels.size(), D3D11_SDK_VERSION, &m_d3dDevice, nullptr, &m_deviceContext);
		if (FAILED(hr))
		{
			_com_error err(hr);
			LPCTSTR errMsg = err.ErrorMessage();
			ErrorLog("Failed to create D3D11 device!\n");
			ErrorLog((char*)errMsg);
			return false;
		}

		ComPtr<ID3D11Device> dev;*/

		std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,	D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

		if (FAILED(D3D11On12CreateDevice(m_d3d12Device.Get(), 0, featureLevels.data(), featureLevels.size(), reinterpret_cast<IUnknown**>(m_d3d12CommandQueue.GetAddressOf()), 1, 0, &m_d3dDevice, nullptr, nullptr)))
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
		IDXGIAdapter* adapter = nullptr;

		if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, nullptr, &m_deviceContext)))
		{
			ErrorLog("Failed to create D3D11 to Vulkan interop device!\n");
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
}


void PassthroughRendererDX11Interop::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams)
{
	ID3D11Resource* rts[2] = { m_renderTargets[leftSwapchainIndex].Get(), m_renderTargets[rightSwapchainIndex + NUM_SWAPCHAINS].Get() };
	m_d3d11On12Device->AcquireWrappedResources(rts, 2);

	ID3D11Resource* dts[2] = { 0 };

	if (m_depthStencils[leftSwapchainIndex] && m_depthStencils[rightSwapchainIndex + NUM_SWAPCHAINS])
	{
		dts[0] = m_depthStencils[leftSwapchainIndex].Get();
		dts[1] = m_depthStencils[rightSwapchainIndex + NUM_SWAPCHAINS].Get();

		m_d3d11On12Device->AcquireWrappedResources(dts, 2);
	}

	PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, depthFrame, distortionParams);

	m_d3d11On12Device->ReleaseWrappedResources(rts, 2);

	if (dts[0] != nullptr)
	{
		m_d3d11On12Device->ReleaseWrappedResources(dts, 2);
	}

	m_deviceContext->Flush();
}