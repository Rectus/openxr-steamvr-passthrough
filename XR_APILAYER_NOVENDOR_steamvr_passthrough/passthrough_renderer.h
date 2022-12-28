#pragma once

#include <vector>
#include <iostream>
#include <d3dcompiler.h>
#include <wrl.h>
#include <winuser.h>
#include <xr_linear.h>
#include "config_manager.h"
#include "layer.h"

using Microsoft::WRL::ComPtr;

#define NUM_SWAPCHAINS 3

enum ERenderEye
{
	LEFT_EYE,
	RIGHT_EYE
};

enum EPassthroughBlendMode
{
	Masked = 0,
	Opaque = 1,
	Additive = 2,
	AlphaBlendPremultiplied = 3,
	AlphaBlendUnpremultiplied = 4
};

enum EStereoFrameLayout
{
	Mono = 0,
	StereoVerticalLayout = 1, // Stereo frames are Bottom/Top (for left/right respectively)
	StereoHorizontalLayout = 2 // Stereo frames are Left/Right
};

struct CameraFrame
{
	CameraFrame()
		: header()
		, frameTextureResource(nullptr)
		, frameUVProjectionLeft()
		, frameUVProjectionRight()
		, frameLayout(Mono)
		, bIsValid(false)
	{
	}

	vr::CameraVideoStreamFrameHeader_t header;
	void* frameTextureResource;
	std::shared_ptr<std::vector<uint8_t>> frameBuffer;
	XrMatrix4x4f frameUVProjectionLeft;
	XrMatrix4x4f frameUVProjectionRight;
	EStereoFrameLayout frameLayout;
	bool bIsValid;
};


inline XrVector2f GetFrameUVOffset(const ERenderEye eye, const EStereoFrameLayout layout)
{
	if (eye == LEFT_EYE)
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return XrVector2f(0, 0);
			break;

			// The vertical layout has left camera below the right
		case StereoVerticalLayout:
			return XrVector2f(0, 0.5);
			break;

		case Mono:
			return XrVector2f(0, 0);
			break;
		}
	}
	else
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return XrVector2f(0.5, 0);
			break;

		case StereoVerticalLayout:
			return XrVector2f(0, 0);
			break;

		case Mono:
			return XrVector2f(0, 0);
			break;
		}
	}

	return XrVector2f(0, 0);
}



class IPassthroughRenderer
{
public:
	virtual bool InitRenderer() = 0;
	virtual void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) = 0;
	virtual void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize) = 0;
	virtual void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex) = 0;
	virtual void* GetRenderDevice() = 0;
};


class PassthroughRendererDX11 : public IPassthroughRenderer
{
public:
	PassthroughRendererDX11(ID3D11Device* device, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);

	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex);
	void* GetRenderDevice();

private:

	void SetupTestImage();
	void SetupFrameResource();
	void SetupTemporaryRenderTarget(ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, uint32_t width, uint32_t height);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	bool m_bUsingDeferredContext = false;
	int m_frameIndex = 0;

	ComPtr<ID3D11Device> m_d3dDevice;
	ComPtr<ID3D11DeviceContext> m_deviceContext;
	ComPtr<ID3D11DeviceContext> m_renderContext;
	

	ComPtr<ID3D11Resource> m_renderTargets[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11RenderTargetView> m_renderTargetViews[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11ShaderResourceView> m_renderTargetSRVs[NUM_SWAPCHAINS * 2];

	ComPtr<ID3D11VertexShader> m_quadShader;
	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11PixelShader> m_prepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPrepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPixelShader;

	ComPtr<ID3D11Buffer> m_vsConstantBuffer[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11Buffer> m_psPassConstantBuffer;
	ComPtr<ID3D11Buffer> m_psMaskedConstantBuffer;
	ComPtr<ID3D11Buffer> m_psViewConstantBuffer;
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;

	ComPtr<ID3D11BlendState> m_blendStateBase;
	ComPtr<ID3D11BlendState> m_blendStateAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateSrcAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassIgnoreAppAlpha;

	ComPtr<ID3D11Texture2D> m_testPatternTexture;
	ComPtr<ID3D11ShaderResourceView> m_testPatternSRV;

	ComPtr<ID3D11Texture2D> m_cameraFrameTexture[NUM_SWAPCHAINS];
	ComPtr<ID3D11Texture2D> m_cameraFrameUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_cameraFrameSRV[NUM_SWAPCHAINS];

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};


class PassthroughRendererDX12 : public IPassthroughRenderer
{
public:
	PassthroughRendererDX12(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex);
	void* GetRenderDevice();
	
private:

	void SetupTestImage();
	void SetupFrameResource();
	bool CreateRootSignature();
	bool InitPipeline(DXGI_FORMAT rtFormat);
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	int m_frameIndex = 0;

	ComPtr<ID3D12Device> m_d3dDevice;
	ComPtr<ID3D12CommandQueue> m_d3dCommandQueue;

	ComPtr <ID3D12Resource> m_renderTargets[NUM_SWAPCHAINS * 2];
	
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
	ComPtr<ID3D12DescriptorHeap> m_CBVSRVHeap;
	UINT m_RTVHeapDescSize = 0;
	UINT m_CBVSRVHeapDescSize = 0;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	ComPtr<ID3D12PipelineState> m_psoDefault;
	ComPtr<ID3D12PipelineState> m_psoAlphaPremultiplied;
	ComPtr<ID3D12PipelineState> m_psoPrepassUseAppAlpha;
	ComPtr<ID3D12PipelineState> m_psoPrepassIgnoreAppAlpha;
	ComPtr<ID3D12PipelineState> m_psoMaskedPrepass;
	ComPtr<ID3D12PipelineState> m_psoMaskedRender;
	

	ComPtr<ID3D12Resource> m_vsConstantBuffer;
	UINT8* m_vsConstantBufferCPUData[NUM_SWAPCHAINS * 2];

	ComPtr<ID3D12Resource> m_psPassConstantBuffer;
	UINT8* m_psPassConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_psViewConstantBuffer;
	UINT8* m_psViewConstantBufferCPUData[NUM_SWAPCHAINS * 2];

	ComPtr<ID3D12Resource> m_psMaskedConstantBuffer;
	UINT8* m_psMaskedConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_testPattern;
	ComPtr<ID3D12Resource> m_testPatternUploadHeap;

	ComPtr<ID3D12Resource> m_cameraFrameRes[NUM_SWAPCHAINS];
	ComPtr<ID3D12Resource> m_frameResUploadHeap;

	ComPtr<ID3D12Resource> m_intermediateRenderTargets[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12DescriptorHeap> m_intermediateRTVHeap;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};