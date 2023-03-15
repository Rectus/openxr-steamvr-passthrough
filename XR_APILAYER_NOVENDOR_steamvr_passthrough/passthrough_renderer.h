#pragma once

#include <vector>
#include <iostream>
#include <d3dcompiler.h>
#include <wrl.h>
#include <winuser.h>
#include <functional>
#include "config_manager.h"
#include "layer.h"

using Microsoft::WRL::ComPtr;

#define NUM_SWAPCHAINS 3

#define NUM_MESH_BOUNDARY_VERTICES 16


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
	virtual ~IPassthroughRenderer() {};

	virtual bool InitRenderer() = 0;
	virtual void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) = 0;
	virtual void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) {}
	virtual void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize) = 0;
	virtual void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams) = 0;
	virtual void* GetRenderDevice() = 0;
};


class PassthroughRendererDX11 : public IPassthroughRenderer
{
public:
	PassthroughRendererDX11(ID3D11Device* device, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererDX11() {};

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);

	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();

private:

	void SetupTestImage();
	void SetupFrameResource();
	void SetupDisparityMap(uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	void SetupTemporaryRenderTarget(ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, uint32_t width, uint32_t height);
	void GenerateMesh();
	void GenerateDepthMesh(uint32_t width, uint32_t height);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, UINT numVertices);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, UINT numVertices);
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

	ComPtr<ID3D11Resource> m_depthStencils[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11DepthStencilView> m_depthStencilViews[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateDisabled;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLess;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLessWrite;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreater;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreaterWrite;

	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11VertexShader> m_stereoVertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11PixelShader> m_prepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPrepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPixelShader;

	ComPtr<ID3D11Buffer> m_vsViewConstantBuffer[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11Buffer> m_vsPassConstantBuffer[NUM_SWAPCHAINS];
	ComPtr<ID3D11Buffer> m_psPassConstantBuffer;
	ComPtr<ID3D11Buffer> m_psMaskedConstantBuffer;
	ComPtr<ID3D11Buffer> m_psViewConstantBuffer;
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;

	ComPtr<ID3D11BlendState> m_blendStateBase;
	ComPtr<ID3D11BlendState> m_blendStateAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateSrcAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassInverseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassIgnoreAppAlpha;

	ComPtr<ID3D11Texture2D> m_testPatternTexture;
	ComPtr<ID3D11ShaderResourceView> m_testPatternSRV;

	ComPtr<ID3D11Texture2D> m_cameraFrameTexture[NUM_SWAPCHAINS];
	ComPtr<ID3D11Texture2D> m_cameraFrameUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_cameraFrameSRV[NUM_SWAPCHAINS];

	ComPtr<ID3D11Texture2D> m_disparityMap[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11Texture2D> m_disparityMapUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_disparityMapSRV[NUM_SWAPCHAINS * 2];
	uint32_t m_disparityMapWidth;

	ComPtr<ID3D11Texture2D> m_uvDistortionMap;
	ComPtr<ID3D11ShaderResourceView> m_uvDistortionMapSRV;
	float m_fovScale;

	ComPtr<ID3D11Texture2D> m_testFrame;
	ComPtr<ID3D11Texture2D> m_testFrameUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_testFrameSRV;

	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_vertexBuffer;
	std::vector<float> m_vertices;

	ComPtr<ID3D11Buffer> m_stereoVertexBuffer;
	std::vector<float> m_stereoVertices;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};


class PassthroughRendererDX12 : public IPassthroughRenderer
{
public:
	PassthroughRendererDX12(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererDX12() {};

	bool InitRenderer();	
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();
	
private:
	ComPtr<ID3D12Resource> InitBuffer(UINT8** bufferCPUData, int numBuffers, int bufferSizePerAlign, int heapIndex);
	void SetupTestImage();
	void SetupFrameResource();
	void SetupDisparityMap(uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	bool CreateRootSignature();
	bool InitPipeline();
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	void GenerateMesh();
	void GenerateDepthMesh(uint32_t width, uint32_t height);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, UINT numVertices);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, UINT numVertices);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	int m_frameIndex = 0;

	ComPtr<ID3D12Device> m_d3dDevice;
	ComPtr<ID3D12CommandQueue> m_d3dCommandQueue;

	ComPtr<ID3D12Resource> m_renderTargets[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12Resource> m_depthStencils[NUM_SWAPCHAINS * 2];
	
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
	ComPtr<ID3D12DescriptorHeap> m_DSVHeap;
	ComPtr<ID3D12DescriptorHeap> m_CBVSRVHeap;
	UINT m_RTVHeapDescSize = 0;
	UINT m_DSVHeapDescSize = 0;
	UINT m_CBVSRVHeapDescSize = 0;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	ComPtr<ID3D12PipelineState> m_psoPrepass;
	ComPtr<ID3D12PipelineState> m_psoMainPass;

	DXGI_FORMAT m_swapchainFormat;
	DXGI_FORMAT m_depthStencilFormat;
	EPassthroughBlendMode m_blendMode;
	bool m_bUsingStereo;
	bool m_bUsingDepth;
	bool m_bUsingReversedDepth;
	
	ComPtr<ID3D12Resource> m_vsPassConstantBuffer;
	UINT8* m_vsPassConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_vsViewConstantBuffer;
	UINT8* m_vsViewConstantBufferCPUData[NUM_SWAPCHAINS * 2];

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

	ComPtr<ID3D12Resource> m_disparityMap[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12Resource> m_disparityMapUploadHeap;
	uint32_t m_disparityMapWidth;

	ComPtr<ID3D12Resource> m_uvDistortionMap;
	ComPtr<ID3D12Resource> m_uvDistortionMapUploadHeap;
	float m_fovScale;

	ComPtr<ID3D12Resource> m_vertexBufferUpload;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	std::vector<float> m_vertices;
	
	ComPtr<ID3D12Resource> m_stereoVertexBufferUpload;
	ComPtr<ID3D12Resource> m_stereoVertexBuffer;
	std::vector<float> m_stereoVertices;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};


class PassthroughRendererVulkan : public IPassthroughRenderer
{
public:
	PassthroughRendererVulkan(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererVulkan();

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();

private:

	bool SetupPipeline(VkFormat format);
	VkShaderModule CreateShaderModule(const uint32_t* bytecode, size_t codeSize);

	bool SetupTestImage(VkCommandBuffer commandBuffer);
	bool GenerateMesh(VkCommandBuffer commandBuffer);
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	bool UpdateCameraFrameResource(VkCommandBuffer commandBuffer, int frameIndex, void* frameResource);
	void UpdateDescriptorSets(VkCommandBuffer commandBuffer, int swapchainIndex, const XrCompositionLayerProjection* layer, EPassthroughBlendMode blendMode);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode);
	void RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame);

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	std::deque<std::function<void()>> m_deletionQueue;

	int m_frameIndex = 0;

	VkInstance m_instance;
	VkPhysicalDevice m_physDevice;
	VkDevice m_device;
	uint32_t m_queueFamilyIndex;
	uint32_t m_queueIndex;
	VkQueue m_queue;
	VkCommandPool m_commandPool;
	VkCommandBuffer m_commandBuffer[NUM_SWAPCHAINS];

	VkShaderModule m_vertexShader;
	VkShaderModule m_pixelShader;
	VkShaderModule m_prepassShader;
	VkShaderModule m_maskedPrepassShader;
	VkShaderModule m_maskedPixelShader;

	VkRenderPass m_renderpass;
	VkRenderPass m_renderpassMaskedPrepass;

	VkPipelineLayout m_pipelineLayout;

	VkPipeline m_pipelineDefault;
	VkPipeline m_pipelineAlphaPremultiplied;
	VkPipeline m_pipelinePrepassUseAppAlpha;
	VkPipeline m_pipelinePrepassIgnoreAppAlpha;
	VkPipeline m_pipelineMaskedPrepass;
	VkPipeline m_pipelineMaskedRender;

	VkDescriptorPool m_descriptorPool;
	VkDescriptorSet m_descriptorSets[NUM_SWAPCHAINS * 2];
	VkDescriptorSetLayout m_descriptorLayout;
	

	VkBuffer m_psPassConstantBuffer[NUM_SWAPCHAINS];
	VkDeviceMemory m_psPassConstantBufferMem[NUM_SWAPCHAINS];
	void* m_psPassConstantBufferMappings[NUM_SWAPCHAINS];

	VkBuffer m_psMaskedConstantBuffer[NUM_SWAPCHAINS];
	VkDeviceMemory m_psMaskedConstantBufferMem[NUM_SWAPCHAINS];
	void* m_psMaskedConstantBufferMappings[NUM_SWAPCHAINS];

	VkSampler m_cameraSampler;
	VkSampler m_intermediateSampler;

	VkImage m_testPattern;
	VkImageView m_testPatternView;
	VkDeviceMemory m_testPatternMem;
	VkBuffer m_testPatternBuffer;
	VkDeviceMemory m_testPatternBufferMem;

	VkImage m_cameraFrameRes[NUM_SWAPCHAINS];
	VkImageView m_cameraFrameResView[NUM_SWAPCHAINS];
	VkImageView m_cameraFrameResArrayView[NUM_SWAPCHAINS];
	VkDeviceMemory m_cameraFrameResMem[NUM_SWAPCHAINS];
	void* m_cameraFrameResExternalHandle[NUM_SWAPCHAINS];
	
	VkImage m_renderTargets[NUM_SWAPCHAINS * 2];
	VkFramebuffer m_renderTargetFramebuffers[NUM_SWAPCHAINS * 2];
	VkImageView m_renderTargetViews[NUM_SWAPCHAINS * 2];

	VkImage m_intermediateRenderTargets[NUM_SWAPCHAINS * 2];
	VkDeviceMemory m_intermediateRenderTargetMem[NUM_SWAPCHAINS * 2];
	VkFramebuffer m_intermediateRenderTargetFramebuffers[NUM_SWAPCHAINS * 2];
	VkImageView m_intermediateRenderTargetViews[NUM_SWAPCHAINS * 2];
	uint32_t m_intermediateRenderTargetWidth[2];
	uint32_t m_intermediateRenderTargetHeight[2];

	VkImage m_uvDistortionMap;
	VkImageView m_uvDistortionMapView;
	VkDeviceMemory m_uvDistortionMapMem;
	VkBuffer m_uvDistortionMapBuffer;
	VkDeviceMemory m_uvDistortionMapBufferMem;
	float m_fovScale;

	VkDeviceMemory m_vertexBufferMem;
	VkBuffer m_vertexBuffer;
	std::vector<float> m_vertices;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;
};