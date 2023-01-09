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

#define NUM_MESH_BOUNDARY_VERTICES 16

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
		, cameraToWorldLeft()
		, cameraToWorldRight()
		, hmdWorldToProjectionLeft()
		, hmdWorldToProjectionRight()
		, hmdViewPosWorldLeft()
		, hmdViewPosWorldRight()
		, frameLayout(Mono)
		, bIsValid(false)
	{
	}

	vr::CameraVideoStreamFrameHeader_t header;
	void* frameTextureResource;
	std::shared_ptr<std::vector<uint8_t>> frameBuffer;
	XrMatrix4x4f cameraToWorldLeft;
	XrMatrix4x4f cameraToWorldRight;
	XrMatrix4x4f hmdWorldToProjectionLeft;
	XrMatrix4x4f hmdWorldToProjectionRight;
	XrVector3f hmdViewPosWorldLeft;
	XrVector3f hmdViewPosWorldRight;
	EStereoFrameLayout frameLayout;

	bool bIsValid;
};


// Outputs Umin, Vmin, Umax, Vmax.
inline XrVector4f GetFrameUVBounds(const ERenderEye eye, const EStereoFrameLayout layout)
{
	if (eye == LEFT_EYE)
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return XrVector4f(0, 0, 0.5, 1.0);
			break;

			// The vertical layout has left camera below the right
		case StereoVerticalLayout:
			return XrVector4f(0, 0.5, 1.0, 1.0);
			break;

		case Mono:
			return XrVector4f(0, 0, 1.0, 1.0);
			break;
		}
	}
	else
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return XrVector4f(0.5, 0, 1.0, 1.0);
			break;

		case StereoVerticalLayout:
			return XrVector4f(0, 0, 1.0, 0.5);
			break;

		case Mono:
			return XrVector4f(0, 0, 1.0, 1.0);
			break;
		}
	}

	return XrVector4f(0, 0, 1.0, 1.0);
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
	void GenerateMesh();

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

	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_vertexBuffer;
	std::vector<float> m_vertices;

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
	ComPtr<ID3D12Resource> InitBuffer(UINT8** bufferCPUData, int numBuffers, int bufferSizePerAlign, int heapIndex);
	void SetupTestImage();
	void SetupFrameResource();
	bool CreateRootSignature();
	bool InitPipeline(DXGI_FORMAT rtFormat);
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	void GenerateMesh();

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

	ComPtr<ID3D12Resource> m_vertexBufferUpload;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	std::vector<float> m_vertices;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};


class PassthroughRendererVulkan : public IPassthroughRenderer
{
public:
	PassthroughRendererVulkan(XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex);
	void* GetRenderDevice();

private:

	bool SetupPipeline();
	VkShaderModule CreateShaderModule(const uint32_t* bytecode, size_t codeSize);

	void SetupTestImage();
	void SetupFrameResource();
	//void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	//void GenerateMesh();

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	int m_frameIndex = 0;

	VkInstance m_instance;
	VkPhysicalDevice m_physDevice;
	VkDevice m_device;
	uint32_t m_queueFamilyIndex;
	uint32_t m_queueIndex;
	VkQueue m_queue;

	VkShaderModule m_vertexShader;
	VkShaderModule m_pixelShader;
	VkShaderModule m_prepassShader;
	VkShaderModule m_maskedPrepassShader;
	VkShaderModule m_maskedPixelShader;

	VkRenderPass m_renderpass;

	VkPipelineLayout m_pipelineLayout;

	VkPipeline m_pipelineDefault;
	VkPipeline m_pipelineAlphaPremultiplied;
	VkPipeline m_pipelinePrepassUseAppAlpha;
	VkPipeline m_pipelinePrepassIgnoreAppAlpha;
	VkPipeline m_pipelineMaskedPrepass;
	VkPipeline m_pipelineMaskedRender;

	/*ComPtr<ID3D12Device> m_d3dDevice;
	ComPtr<ID3D12CommandQueue> m_d3dCommandQueue;

	ComPtr <ID3D12Resource> m_renderTargets[NUM_SWAPCHAINS * 2];

	ComPtr<ID3D12CommandAllocator> m_commandAllocators[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
	ComPtr<ID3D12DescriptorHeap> m_CBVSRVHeap;
	UINT m_RTVHeapDescSize = 0;
	UINT m_CBVSRVHeapDescSize = 0;
	ComPtr<ID3D12RootSignature> m_rootSignature;


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
	ComPtr<ID3D12DescriptorHeap> m_intermediateRTVHeap;*/

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};