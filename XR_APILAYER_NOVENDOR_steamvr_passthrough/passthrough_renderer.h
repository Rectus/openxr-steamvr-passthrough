#pragma once

#include <vector>
#include <iostream>
#include <d3dcompiler.h>
#include <wrl.h>
#include <winuser.h>
#include <functional>
#include "config_manager.h"
#include "layer.h"
#include "mesh.h"
#include "d3d11on12.h"

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


struct DX11TemporaryRenderTarget
{
	ID3D11Resource* AssociatedRenderTarget = nullptr; // Only for checking the assosiated target. May be invalid.
	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11ShaderResourceView> SRV;
};

struct DX11RenderModel
{
	DX11RenderModel()
		: deviceId(0)
		, meshPtr(nullptr)
		, numIndices(0)
		, meshToWorldTransform()
	{
	}

	uint32_t deviceId;
	Mesh<VertexFormatBasic>* meshPtr; // Only for checking equality. May be invalid.
	ComPtr<ID3D11Buffer> vertexBuffer;
	ComPtr<ID3D11Buffer> indexBuffer;
	uint32_t numIndices;
	XrMatrix4x4f meshToWorldTransform;
};


struct DX11ViewData
{
	DX11ViewData()
	{
		memset(&temporaryRenderTarget, 0, sizeof(DX11TemporaryRenderTarget));
	}

	bool bInitialized = false;

	ComPtr<ID3D11Resource> renderTarget;
	ComPtr<ID3D11RenderTargetView> renderTargetView;
	ComPtr<ID3D11ShaderResourceView> renderTargetSRV;

	DX11TemporaryRenderTarget temporaryRenderTarget;

	ComPtr<ID3D11Buffer> vsViewConstantBuffer;
	ComPtr<ID3D11Buffer> psViewConstantBuffer;

	ComPtr<ID3D11UnorderedAccessView> cameraFilterUAV;
	ComPtr<ID3D11ShaderResourceView> cameraFilterSRV;
	ComPtr<ID3D11Texture2D> cameraFilterUAVTexture;
};


struct DX11ViewDepthData
{
	ComPtr<ID3D11Resource> depthStencil;
	ComPtr<ID3D11DepthStencilView> depthStencilView;
};


struct DX11FrameData
{
	bool bInitialized = false;
	
	ComPtr<ID3D11Buffer> csConstantBuffer;
	ComPtr<ID3D11Buffer> vsPassConstantBuffer;
	ComPtr<ID3D11Buffer> psPassConstantBuffer;
	ComPtr<ID3D11Buffer> psMaskedConstantBuffer;
	
	ComPtr<ID3D11Texture2D> cameraFrameTexture;
	ComPtr<ID3D11ShaderResourceView> cameraFrameSRV;

	ComPtr<ID3D11Texture2D> cameraUndistortedFrameTexture;
	ComPtr<ID3D11ShaderResourceView> cameraUndistortedFrameSRV;

	ComPtr<ID3D11Texture2D> disparityMap;
	ComPtr<ID3D11UnorderedAccessView> disparityMapCSUAV;
	ComPtr<ID3D11ShaderResourceView> disparityMapSRV;

	ComPtr<ID3D11UnorderedAccessView> disparityMapUAV;
	ComPtr<ID3D11ShaderResourceView> disparityMapUAVSRV;
	ComPtr<ID3D11Texture2D> disparityMapUAVTexture;
};


class IPassthroughRenderer
{
public:
	virtual ~IPassthroughRenderer() {};

	virtual bool InitRenderer() = 0;
	virtual void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) = 0;
	virtual void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) {}
	virtual void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize) = 0;
	virtual void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams) = 0;
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
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize);

	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams);
	void* GetRenderDevice();

protected:

	void SetupDebugTexture(DebugTexture& texture);
	void SetupCameraFrameResource(const uint32_t imageIndex);
	void SetupCameraUndistortedFrameResource(const uint32_t imageIndex);
	bool CheckInitViewData(const uint32_t viewIndex, const uint32_t swapchainIndex);
	bool CheckInitFrameData(const uint32_t imageIndex);
	void SetupDisparityMap(uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	DX11TemporaryRenderTarget& GetTemporaryRenderTarget(const uint32_t swapchainIndex, const uint32_t eyeIndex);
	void GenerateMesh();
	void GenerateDepthMesh(uint32_t width, uint32_t height);
	void SetupTemporalUAV(const uint32_t viewIndex, const uint32_t swapchainIndex);
	void UpdateRenderModels(CameraFrame* frame);

	void RenderHoleFillCS(DX11FrameData& frameData, std::shared_ptr<DepthFrame> depthFrame);
	void RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const int32_t depthSwapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, EPassthroughBlendMode blendMode, UINT numIndices, FrameRenderParameters& renderParams);
	void RenderMaskedPrepassView(const ERenderEye eye, const int32_t swapchainIndex, int32_t depthSwapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;

	bool m_bIsTemporalSupported = true;
	bool m_bUsingDeferredContext = false;

	int m_frameIndex = 0;
	int m_prevFrameIndex = 0;
	int m_prevSwapchainLeft = 0;
	int m_prevSwapchainRight = 0;
	int m_depthUpdatedFrameIndex = 0;
	int m_prevDepthUpdatedFrameIndex = 0;


	ComPtr<ID3D11Device> m_d3dDevice;
	ComPtr<ID3D11DeviceContext> m_deviceContext;
	ComPtr<ID3D11DeviceContext> m_renderContext;

	std::vector<DX11ViewData> m_viewData[2];
	std::vector<DX11ViewDepthData> m_viewDepthData[2];
	std::vector<DX11FrameData> m_frameData;
	
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateDisabled;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLess;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLessWrite;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreater;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreaterWrite;

	ComPtr<ID3D11ComputeShader> m_fillHolesComputeShader;
	ComPtr<ID3D11VertexShader> m_fullscreenQuadShader;
	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11VertexShader> m_meshRigidVertexShader;
	ComPtr<ID3D11VertexShader> m_stereoVertexShader;
	ComPtr<ID3D11VertexShader> m_stereoTemporalVertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11PixelShader> m_pixelShaderTemporal;
	ComPtr<ID3D11PixelShader> m_prepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPrepassShader;
	ComPtr<ID3D11PixelShader> m_maskedAlphaCopyShader;

	
	ComPtr<ID3D11Buffer> m_vsMeshConstantBuffer[vr::k_unMaxTrackedDeviceCount];
	
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateMirrored;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateDepthBias;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateDepthBiasMirrored;

	ComPtr<ID3D11BlendState> m_blendStateDestAlpha;
	ComPtr<ID3D11BlendState> m_blendStateDestAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateSrcAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassInverseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassIgnoreAppAlpha;

	ComPtr<ID3D11Texture2D> m_debugTexture;
	ComPtr<ID3D11Texture2D> m_debugTextureUpload;
	ComPtr<ID3D11ShaderResourceView> m_debugTextureSRV;
	ESelectedDebugTexture m_selectedDebugTexture;

	ComPtr<ID3D11Texture2D> m_cameraFrameUploadTexture;
	ComPtr<ID3D11Texture2D> m_cameraUndistortedFrameUploadTexture;
	ComPtr<ID3D11Texture2D> m_disparityMapUploadTexture;
	uint32_t m_disparityMapWidth;

	ComPtr<ID3D11Texture2D> m_uvDistortionMap;
	ComPtr<ID3D11ShaderResourceView> m_uvDistortionMapSRV;
	float m_fovScale;

	ComPtr<ID3D11Texture2D> m_testFrame;
	ComPtr<ID3D11Texture2D> m_testFrameUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_testFrameSRV;

	ComPtr<ID3D11InputLayout> m_inputLayout;
	
	Mesh<VertexFormatBasic> m_cylinderMesh;
	ComPtr<ID3D11Buffer> m_cylinderMeshVertexBuffer;
	ComPtr<ID3D11Buffer> m_cylinderMeshIndexBuffer;

	Mesh<VertexFormatBasic> m_gridMesh;
	ComPtr<ID3D11Buffer> m_gridMeshVertexBuffer;
	ComPtr<ID3D11Buffer> m_gridMeshIndexBuffer;
	bool m_bUseHexagonGridMesh;

	std::vector<DX11RenderModel> m_renderModels;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	uint32_t m_cameraUndistortedTextureWidth;
	uint32_t m_cameraUndistortedTextureHeight;
	uint32_t m_cameraUndistortedFrameBufferSize;
};


class PassthroughRendererDX11Interop : public PassthroughRendererDX11
{
public:
	PassthroughRendererDX11Interop(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	PassthroughRendererDX11Interop(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererDX11Interop();

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	bool CreateLocalTextureVulkan(VkImage& localVulkanTexture, VkDeviceMemory& localVulkanTextureMemory, ID3D11Texture2D** localD3DTexture, HANDLE& sharedTextureHandle, const XrSwapchainCreateInfo& swapchainInfo, bool bIsDepthMap);

	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams);

private:
	ERenderAPI m_applicationRenderAPI;

	ComPtr<ID3D12Device> m_d3d12Device;
	ComPtr<ID3D11On12Device2> m_d3d11On12Device;
	ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;

	ComPtr<ID3D11Device5> m_d3d11Device5;
	ComPtr<ID3D11DeviceContext4> m_d3d11DeviceContext4;

	VkInstance m_vulkanInstance;
	VkPhysicalDevice m_vulkanPhysDevice;
	VkDevice m_vulkanDevice;
	uint32_t m_vulkanQueueFamilyIndex;
	uint32_t m_vulkanQueueIndex;
	VkQueue m_vulkanQueue;
	VkCommandPool m_vulkanCommandPool;
	VkCommandBuffer m_vulkanCommandBuffer[NUM_SWAPCHAINS * 2];

	HANDLE m_sharedTextureLeft[NUM_SWAPCHAINS] = {};
	HANDLE m_sharedTextureRight[NUM_SWAPCHAINS] = {};

	VkImage m_swapchainsLeft[NUM_SWAPCHAINS] = {};
	VkImage m_swapchainsRight[NUM_SWAPCHAINS] = {};
	VkImage m_depthBuffersLeft[NUM_SWAPCHAINS] = {};
	VkImage m_depthBuffersRight[NUM_SWAPCHAINS] = {};

	VkImage m_localRendertargetsLeft[NUM_SWAPCHAINS] = {};
	VkImage m_localRendertargetsRight[NUM_SWAPCHAINS] = {};
	VkDeviceMemory m_localRTMemLeft[NUM_SWAPCHAINS] = {};
	VkDeviceMemory m_localRTMemRight[NUM_SWAPCHAINS] = {};

	VkImage m_localDepthBuffersLeft[NUM_SWAPCHAINS] = {};
	VkImage m_localDepthBuffersRight[NUM_SWAPCHAINS] = {};
	VkDeviceMemory m_localDBMemLeft[NUM_SWAPCHAINS] = {};
	VkDeviceMemory m_localDBMemRight[NUM_SWAPCHAINS] = {};

	VkSemaphore m_semaphore;
	ComPtr<ID3D11Fence> m_semaphoreFence;
	HANDLE m_semaphoreFenceHandle;
	uint64_t m_semaphoreValue = 1;
};


class PassthroughRendererDX12 : public IPassthroughRenderer
{
public:
	PassthroughRendererDX12(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererDX12() {};

	bool InitRenderer();	
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams);
	void* GetRenderDevice();
	
private:
	ComPtr<ID3D12Resource> InitBuffer(UINT8** bufferCPUData, int numBuffers, int bufferSizePerAlign, int heapIndex);
	void SetupDebugTexture(DebugTexture& texture);
	void SetupFrameResource();
	void SetupUndistortedFrameResource();
	void SetupDisparityMap(uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	bool CreateRootSignature();
	bool InitPipeline(bool bFlipTriangles);
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	void GenerateMesh();
	void GenerateDepthMesh(uint32_t width, uint32_t height);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, UINT numIndices);
	void RenderMaskedPrepassView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, UINT numIndices);
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
	ComPtr<ID3D12PipelineState> m_psoMaskedPrepassFullscreen;
	ComPtr<ID3D12PipelineState> m_psoMaskedAlphaCopy;
	ComPtr<ID3D12PipelineState> m_psoMainPass;
	ComPtr<ID3D12PipelineState> m_psoCutoutPass;
	ComPtr<ID3D12PipelineState> m_psoHoleFillPass;

	DXGI_FORMAT m_swapchainFormat;
	DXGI_FORMAT m_depthStencilFormat;
	EPassthroughBlendMode m_blendMode;
	bool m_bUsingStereo;
	bool m_bUsingDepth;
	bool m_bUsingReversedDepth;
	bool m_bWriteDepth;
	
	ComPtr<ID3D12Resource> m_vsPassConstantBuffer;
	UINT8* m_vsPassConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_vsViewConstantBuffer;
	UINT8* m_vsViewConstantBufferCPUData[NUM_SWAPCHAINS * 4];

	ComPtr<ID3D12Resource> m_psPassConstantBuffer;
	UINT8* m_psPassConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_psViewConstantBuffer;
	UINT8* m_psViewConstantBufferCPUData[NUM_SWAPCHAINS * 4];

	ComPtr<ID3D12Resource> m_psMaskedConstantBuffer;
	UINT8* m_psMaskedConstantBufferCPUData[NUM_SWAPCHAINS];

	ComPtr<ID3D12Resource> m_debugTexture;
	ComPtr<ID3D12Resource> m_debugTextureUploadHeap;
	ESelectedDebugTexture m_selectedDebugTexture;

	ComPtr<ID3D12Resource> m_cameraFrameRes[NUM_SWAPCHAINS];
	ComPtr<ID3D12Resource> m_frameResUploadHeap;

	ComPtr<ID3D12Resource> m_cameraUndisortedFrameRes[NUM_SWAPCHAINS];
	ComPtr<ID3D12Resource> m_undistortedFrameResUploadHeap;

	ComPtr<ID3D12Resource> m_intermediateRenderTargets[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12DescriptorHeap> m_intermediateRTVHeap;

	ComPtr<ID3D12Resource> m_disparityMap[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D12Resource> m_disparityMapUploadHeap;
	uint32_t m_disparityMapWidth;

	ComPtr<ID3D12Resource> m_uvDistortionMap;
	ComPtr<ID3D12Resource> m_uvDistortionMapUploadHeap;
	float m_fovScale;
	
	Mesh<VertexFormatBasic> m_cylinderMesh;
	ComPtr<ID3D12Resource> m_cylinderMeshVertexBuffer;
	ComPtr<ID3D12Resource> m_cylinderMeshIndexBuffer;
	ComPtr<ID3D12Resource> m_cylinderMeshVertexBufferUpload;
	ComPtr<ID3D12Resource> m_cylinderMeshIndexBufferUpload;

	Mesh<VertexFormatBasic> m_gridMesh;
	ComPtr<ID3D12Resource> m_gridMeshVertexBuffer;
	ComPtr<ID3D12Resource> m_gridMeshIndexBuffer;
	ComPtr<ID3D12Resource> m_gridMeshVertexBufferUpload;
	ComPtr<ID3D12Resource> m_gridMeshIndexBufferUpload;
	bool m_bUseHexagonGridMesh;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	uint32_t m_cameraUndistortedTextureWidth;
	uint32_t m_cameraUndistortedTextureHeight;
	uint32_t m_cameraUndistortedFrameBufferSize;
};


class PassthroughRendererVulkan : public IPassthroughRenderer
{
public:
	PassthroughRendererVulkan(const XrGraphicsBindingVulkanKHR& binding, HMODULE dllModule, std::shared_ptr<ConfigManager> configManager);
	~PassthroughRendererVulkan();

	bool InitRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize);
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams);
	void* GetRenderDevice();

private:

	bool SetupPipeline(VkFormat format);
	VkShaderModule CreateShaderModule(const uint32_t* bytecode, size_t codeSize);

	void UploadDebugTexture(DebugTexture& texture);
	bool SetupDebugTexture(DebugTexture& texture);
	bool GenerateMesh(VkCommandBuffer commandBuffer);
	void SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	bool UpdateCameraFrameResource(VkCommandBuffer commandBuffer, int frameIndex, void* frameResource);
	void UpdateDescriptorSets(VkCommandBuffer commandBuffer, int swapchainIndex, const XrCompositionLayerProjection* layer, EPassthroughBlendMode blendMode);

	void RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode);
	void RenderMaskedPrepassView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame);

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

	VkShaderModule m_fullscreenQuadShader;
	VkShaderModule m_vertexShader;
	VkShaderModule m_pixelShader;
	VkShaderModule m_prepassShader;
	VkShaderModule m_maskedPrepassShader;
	VkShaderModule m_maskedAlphaCopyShader;

	VkRenderPass m_renderpass;
	VkRenderPass m_renderpassMaskedPrepass;

	VkPipelineLayout m_pipelineLayout;

	VkPipeline m_pipelineDefault;
	VkPipeline m_pipelineAlphaPremultiplied;
	VkPipeline m_pipelinePrepassUseAppAlpha;
	VkPipeline m_pipelinePrepassIgnoreAppAlpha;
	VkPipeline m_pipelineMaskedPrepass;
	VkPipeline m_pipelineMaskedPrepassFullscreen;
	VkPipeline m_pipelineMaskedAlphaCopy;

	VkDescriptorPool m_descriptorPool;
	VkDescriptorSet m_descriptorSets[NUM_SWAPCHAINS * 2 * 2];
	VkDescriptorSetLayout m_descriptorLayout;
	
	VkBuffer m_vsPassConstantBuffer[NUM_SWAPCHAINS];
	VkDeviceMemory m_vsPassConstantBufferMem[NUM_SWAPCHAINS];
	void* m_vsPassConstantBufferMappings[NUM_SWAPCHAINS];

	VkBuffer m_vsViewConstantBuffer[NUM_SWAPCHAINS * 2];
	VkDeviceMemory m_vsViewConstantBufferMem[NUM_SWAPCHAINS * 2];
	void* m_vsViewConstantBufferMappings[NUM_SWAPCHAINS * 2];

	VkBuffer m_psPassConstantBuffer[NUM_SWAPCHAINS];
	VkDeviceMemory m_psPassConstantBufferMem[NUM_SWAPCHAINS];
	void* m_psPassConstantBufferMappings[NUM_SWAPCHAINS];

	VkBuffer m_psViewConstantBuffer[NUM_SWAPCHAINS * 2];
	VkDeviceMemory m_psViewConstantBufferMem[NUM_SWAPCHAINS * 2];
	void* m_psViewConstantBufferMappings[NUM_SWAPCHAINS * 2];

	VkBuffer m_psMaskedConstantBuffer[NUM_SWAPCHAINS];
	VkDeviceMemory m_psMaskedConstantBufferMem[NUM_SWAPCHAINS];
	void* m_psMaskedConstantBufferMappings[NUM_SWAPCHAINS];

	VkSampler m_cameraSampler;
	VkSampler m_intermediateSampler;

	VkImage m_debugTexture;
	VkImageView m_debugTextureView;
	VkDeviceMemory m_debugTextureMem;
	VkBuffer m_debugTextureBuffer;
	VkDeviceMemory m_debugTextureBufferMem;
	ESelectedDebugTexture m_selectedDebugTexture;

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

	Mesh<VertexFormatBasic> m_cylinderMesh;
	VkDeviceMemory m_cylinderMeshVertexBufferMem;
	VkBuffer m_cylinderMeshVertexBuffer;
	VkDeviceMemory m_cylinderMeshIndexBufferMem;
	VkBuffer m_cylinderMeshIndexBuffer;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	uint32_t m_cameraUndistortedTextureWidth;
	uint32_t m_cameraUndistortedTextureHeight;
	uint32_t m_cameraUndistortedFrameBufferSize;
};