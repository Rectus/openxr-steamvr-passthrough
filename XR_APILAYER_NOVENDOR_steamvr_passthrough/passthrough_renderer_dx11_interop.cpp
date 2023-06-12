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
}


bool PassthroughRendererDX11Interop::InitRenderer()
{
	

	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
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


void PassthroughRendererDX11Interop::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, bool bEnableDepthBlending)
{
	switch (m_applicationRenderAPI)
	{
	case DirectX12:
	{
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

			PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, depthFrame, distortionParams, bEnableDepthBlending);

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
		break;
	}

	case OpenGL:
	{
		break;
	}

	default:
	{
		PassthroughRendererDX11::RenderPassthroughFrame(layer, frame, blendMode, leftSwapchainIndex, rightSwapchainIndex, depthFrame, distortionParams, bEnableDepthBlending);
		break;
	}
	}

	
}