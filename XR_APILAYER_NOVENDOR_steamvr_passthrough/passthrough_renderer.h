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


struct alignas(16) CSConstantBuffer
{
	uint32_t disparityFrameWidth;
	uint32_t bHoleFillLastPass;
	float minDisparity;
	float maxDisparity;
};

struct alignas(16) VSPassConstantBuffer
{
	XrMatrix4x4f worldToCameraFrameProjectionLeft;
	XrMatrix4x4f worldToCameraFrameProjectionRight;
	XrMatrix4x4f worldToPrevCameraFrameProjectionLeft;
	XrMatrix4x4f worldToPrevCameraFrameProjectionRight;
	XrMatrix4x4f worldToPrevDepthFrameProjectionLeft;
	XrMatrix4x4f worldToPrevDepthFrameProjectionRight;
	XrMatrix4x4f depthFrameViewToWorldLeft;
	XrMatrix4x4f depthFrameViewToWorldRight;
	XrMatrix4x4f prevDepthFrameViewToWorldLeft;
	XrMatrix4x4f prevDepthFrameViewToWorldRight;

	XrMatrix4x4f disparityToDepth;
	uint32_t disparityTextureSize[2];
	float minDisparity;
	float maxDisparity;
	float disparityDownscaleFactor;
	float cutoutFactor;
	float cutoutOffset;
	float cutoutFilterWidth;
	int32_t disparityFilterWidth;
	float disparityFilterConfidenceCutout;
	uint32_t bProjectBorders;
	uint32_t bFindDiscontinuities;
	uint32_t bUseDisparityTemporalFilter;
	uint32_t bBlendDepthMaps;
	float disparityTemporalFilterStrength;
	float disparityTemporalFilterDistance;
	float depthContourStrength;
	float depthContourTreshold;
};

struct alignas(16) VSViewConstantBuffer
{
	XrMatrix4x4f worldToHMDProjection;
	XrMatrix4x4f HMDProjectionToWorld;
	XrMatrix4x4f prevHMDFrame_WorldToHMDProjection;
	XrMatrix4x4f prevCameraFrame_WorldToHMDProjection;
	XrVector4f disparityUVBounds;
	XrVector3f projectionOriginWorld;
	float projectionDistance;
	float floorHeightOffset;
	float cameraBlendWeight;
	uint32_t cameraViewIndex;
	uint32_t bWriteDisparityFilter;
};

struct alignas(16) VSMeshConstantBuffer
{
	XrMatrix4x4f meshToWorldTransform;
};

struct alignas(16) PSPassConstantBuffer
{
	XrMatrix4x4f worldToCameraFrameProjectionLeft;
	XrMatrix4x4f worldToCameraFrameProjectionRight;
	XrMatrix4x4f worldToPrevCameraFrameProjectionLeft;
	XrMatrix4x4f worldToPrevCameraFrameProjectionRight;

	XrVector2f depthRange;
	XrVector2f depthCutoffRange;
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	float sharpness;
	int32_t temporalFilteringSampling;
	float temporalFilteringFactor;
	float temporalFilteringColorRangeCutoff;
	float cutoutCombineFactor;
	float depthTemporalFilterFactor;
	float depthTemporalFilterDistance;
	float depthContourStrength;
	float depthContourTreshold;
	int32_t depthContourFilterWidth;
	uint32_t debugOverlay;
	uint32_t bDoColorAdjustment;
	uint32_t bDebugDepth;
	uint32_t bUseFisheyeCorrection;
	uint32_t bIsFirstRenderOfCameraFrame;
	uint32_t bUseDepthCutoffRange;
	uint32_t bClampCameraFrame;
	uint32_t bIsCutoutEnabled;
	uint32_t bIsAppAlphaInverted;
};

struct alignas(16) PSViewConstantBuffer
{
	XrMatrix4x4f worldToHMDProjection;
	XrMatrix4x4f HMDProjectionToWorld;
	XrMatrix4x4f prevHMDFrame_WorldToHMDProjection;
	XrMatrix4x4f prevCameraFrame_WorldToHMDProjection;

	XrVector4f frameUVBounds;
	XrVector4f crossUVBounds;
	XrVector4f prepassUVBounds;
	uint32_t rtArrayIndex;
	int32_t cameraViewIndex;
	uint32_t bDoCutout;
	uint32_t bPremultiplyAlpha;
	uint32_t bUseFullscreenQuad;
};

struct alignas(16) PSMaskedConstantBuffer
{
	float maskedKey[3];
	float maskedFracChroma;
	float maskedFracLuma;
	float maskedSmooth;
	uint32_t bMaskedUseCamera;
	uint32_t bMaskedInvert;
};



struct DX11TemporaryRenderTarget
{
	ID3D11Resource* AssociatedRenderTarget = nullptr; // Only for checking the assosiated target. May be invalid.
	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11ShaderResourceView> SRV;
};

struct DX11RenderTexture
{
	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11ShaderResourceView> SRV;
};


struct DX11UAVSRVTexture
{
	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11UnorderedAccessView> UAV;
	ComPtr<ID3D11ShaderResourceView> SRV;
};

struct DX11DepthStencilTexture
{
	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11DepthStencilView> DSV;
	ComPtr<ID3D11ShaderResourceView> SRV;
	uint32_t Width{};
	uint32_t Height{};
};

struct DX11SRVTexture
{
	ComPtr<ID3D11Texture2D> Texture;
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

	ComPtr<ID3D11Buffer> vsViewConstantBuffer;
	ComPtr<ID3D11Buffer> psViewConstantBuffer;

	DX11RenderTexture renderTarget;
	DX11TemporaryRenderTarget temporaryRenderTarget;

	DX11DepthStencilTexture passthroughDepthStencil[2];
	DX11RenderTexture passthroughCameraValidity;
};


struct DX11ViewDepthData
{
	ComPtr<ID3D11Resource> depthStencil;
	ComPtr<ID3D11DepthStencilView> depthStencilView;
	ComPtr<ID3D11ShaderResourceView> depthSRV;
};


struct DX11FrameData
{
	bool bInitialized = false;
	
	ComPtr<ID3D11Buffer> csConstantBuffer;
	ComPtr<ID3D11Buffer> vsPassConstantBuffer;
	ComPtr<ID3D11Buffer> psPassConstantBuffer;
	ComPtr<ID3D11Buffer> psMaskedConstantBuffer;
	
	DX11SRVTexture cameraFrame;
	DX11SRVTexture cameraUndistortedFrame;

	DX11UAVSRVTexture disparityMap;
	DX11UAVSRVTexture disparityFilter;
};


class IPassthroughRenderer
{
public:
	virtual ~IPassthroughRenderer() {};

	virtual bool InitRenderer() = 0;
	virtual void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) = 0;
	virtual void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo) {}
	virtual void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize) = 0;
	virtual void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams) = 0;
	virtual void* GetRenderDevice() = 0;
	virtual bool DownloadTextureToCPU(const void* textureSRV, const uint32_t width, const uint32_t height, const uint32_t bufferSize, uint8_t* buffer) { return false; }
	virtual std::shared_timed_mutex& GetAccessMutex() = 0;
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

	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();
	std::shared_timed_mutex& GetAccessMutex() { return m_accessRendererMutex; }

protected:

	void SetupDebugTexture(DebugTexture& texture);
	void SetupCameraFrameResource(const uint32_t imageIndex);
	void SetupCameraUndistortedFrameResource(const uint32_t imageIndex);
	bool CheckInitViewData(const uint32_t viewIndex, const uint32_t swapchainIndex);
	bool CheckInitFrameData(const uint32_t imageIndex);
	void SetupDisparityMap(uint32_t width, uint32_t height);
	void SetupPassthroughDepthStencil(uint32_t viewIndex, uint32_t swapchainIndex, uint32_t width, uint32_t height);
	void SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap);
	DX11TemporaryRenderTarget& GetTemporaryRenderTarget(const uint32_t swapchainIndex, const uint32_t eyeIndex);
	void GenerateMesh();
	void GenerateDepthMesh(uint32_t width, uint32_t height);
	void SetupTemporalUAV(const uint32_t viewIndex, const uint32_t swapchainIndex, const uint32_t width, const uint32_t height);
	void UpdateRenderModels(CameraFrame* frame);

	void RenderHoleFillCS(DX11FrameData& frameData, std::shared_ptr<DepthFrame> depthFrame);

	void RenderSetupView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, FrameRenderParameters& renderParams);

	void RenderDepthPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);

	void RenderMaskedPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);
	
	void RenderAlphaPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame,  UINT numIndices, FrameRenderParameters& renderParams);

	void RenderViewModelsForView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);

	void RenderPassthroughView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);

	void RenderBackgroundForView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams);

	void RenderDebugView(const ERenderEye eye, const XrCompositionLayerProjection* layer, FrameRenderParameters& renderParams);

	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	HMODULE m_dllModule;
	std::shared_timed_mutex m_accessRendererMutex;

	bool m_bIsVSUAVSupported = true;
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

	DX11UAVSRVTexture m_cameraFilter[2][2];
	int m_currentCameraFilterIndex = 0;

	ComPtr<ID3D11DepthStencilState> m_depthStencilStateDisabled;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLess;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateLessWrite;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreater;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateGreaterWrite;
	ComPtr<ID3D11DepthStencilState> m_depthStencilStateAlwaysWrite;

	ComPtr<ID3D11ComputeShader> m_fillHolesCS;
	ComPtr<ID3D11VertexShader> m_fullscreenQuadVS;
	ComPtr<ID3D11VertexShader> m_passthroughVS;
	ComPtr<ID3D11VertexShader> m_meshRigidVS;
	ComPtr<ID3D11VertexShader> m_passthroughStereoVS;
	ComPtr<ID3D11VertexShader> m_passthroughStereoTemporalVS;
	ComPtr<ID3D11VertexShader> m_passthroughReadDepthVS;
	ComPtr<ID3D11PixelShader> m_passthroughPS;
	ComPtr<ID3D11PixelShader> m_passthroughTemporalPS;
	ComPtr<ID3D11PixelShader> m_alphaPrepassPS;
	ComPtr<ID3D11PixelShader> m_maskedAlphaPrepassPS;
	ComPtr<ID3D11PixelShader> m_maskedAlphaPrepassFullscreenPS;
	ComPtr<ID3D11PixelShader> m_maskedAlphaCopyPS;
	ComPtr<ID3D11PixelShader> m_depthWritePS;
	ComPtr<ID3D11PixelShader> m_depthWriteTemporalPS;
	ComPtr<ID3D11PixelShader> m_stereoCompositePS;
	ComPtr<ID3D11PixelShader> m_stereoCompositeTemporalPS;
	ComPtr<ID3D11PixelShader> m_fullscreenPassthroughPS;
	ComPtr<ID3D11PixelShader> m_fullscreenPassthroughTemporalPS;
	ComPtr<ID3D11PixelShader> m_fullscreenPassthroughCompositePS;
	ComPtr<ID3D11PixelShader> m_fullscreenPassthroughCompositeTemporalPS;
	ComPtr<ID3D11PixelShader> m_debugAlphaToColorPS;
	ComPtr<ID3D11PixelShader> m_debugDepthToColorPS;

	
	ComPtr<ID3D11Buffer> m_vsMeshConstantBuffer[vr::k_unMaxTrackedDeviceCount];
	
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateMirrored;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateDepthBias;
	ComPtr<ID3D11RasterizerState> m_rasterizerStateDepthBiasMirrored;

	ComPtr<ID3D11BlendState> m_blendStateDestAlpha;
	ComPtr<ID3D11BlendState> m_blendStateInvDestAlpha;
	ComPtr<ID3D11BlendState> m_blendStateDestAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateInvDestAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateSrcAlpha;
	ComPtr<ID3D11BlendState> m_blendStateWriteAlpha;
	ComPtr<ID3D11BlendState> m_blendStateWriteMinAlpha;
	ComPtr<ID3D11BlendState> m_blendStateWriteMaxAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassBlendAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassBlendInverseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseInvertedAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassIgnoreAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassZeroAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStateWriteFactored;

	DX11SRVTexture m_debugTexture;
	ComPtr<ID3D11Texture2D> m_debugTextureUpload;
	ESelectedDebugTexture m_selectedDebugTexture;

	ComPtr<ID3D11Texture2D> m_cameraFrameUploadTexture;
	ComPtr<ID3D11Texture2D> m_cameraUndistortedFrameUploadTexture;
	ComPtr<ID3D11Texture2D> m_disparityMapUploadTexture;
	uint32_t m_disparityMapWidth;

	DX11SRVTexture m_uvDistortionMap;
	float m_fovScale;

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
	
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	bool DownloadTextureToCPU(const void* textureSRV, const uint32_t width, const uint32_t height, const uint32_t bufferSize, uint8_t* buffer);

private:

	void ResetRenderer();
	void InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	void InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo);
	bool CreateLocalTextureVulkan(VkImage& localVulkanTexture, VkDeviceMemory& localVulkanTextureMemory, ID3D11Texture2D** localD3DTexture, HANDLE& sharedTextureHandle, const XrSwapchainCreateInfo& swapchainInfo, bool bIsDepthMap);

	ERenderAPI m_applicationRenderAPI;
	bool m_rendererInitialized = false;

	ComPtr<ID3D12Device> m_d3d12Device = NULL;
	ComPtr<ID3D11On12Device2> m_d3d11On12Device = NULL;
	ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue = NULL;

	ComPtr<ID3D11Device5> m_d3d11Device5 = NULL;
	ComPtr<ID3D11DeviceContext4> m_d3d11DeviceContext4 = NULL;

	VkInstance m_vulkanInstance = VK_NULL_HANDLE;
	VkPhysicalDevice m_vulkanPhysDevice = VK_NULL_HANDLE;
	VkDevice m_vulkanDevice = VK_NULL_HANDLE;
	uint32_t m_vulkanQueueFamilyIndex = 0;
	uint32_t m_vulkanQueueIndex = 0;
	VkQueue m_vulkanQueue = VK_NULL_HANDLE;
	VkCommandPool m_vulkanCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer m_vulkanCommandBuffer[NUM_SWAPCHAINS * 2] = {};
	VkFence m_vulkanRenderCompleteFence = VK_NULL_HANDLE;

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

	VkSemaphore m_semaphore = VK_NULL_HANDLE;
	ComPtr<ID3D11Fence> m_semaphoreFence = NULL;
	HANDLE m_semaphoreFenceHandle = NULL;
	uint64_t m_semaphoreValue = 1;
	bool m_bFirstRender = true;

	VkDevice m_vulkanDownloadDevice = VK_NULL_HANDLE;
	VkQueue m_vulkanDownloadQueue = VK_NULL_HANDLE;
	uint32_t m_vulkanDownloadQueueFamilyIndex = 0;
	VkCommandPool m_vulkanDownloadCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer m_vulkanDownloadCommandBuffer = VK_NULL_HANDLE;
	VkFence m_vulkanDownloadFence = VK_NULL_HANDLE;
	VkBuffer m_vulkanDownloadBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vulkanDownloadBufferMemory = VK_NULL_HANDLE;
	uint32_t m_downloadBufferWidth = 0;
	uint32_t m_downloadBufferHeight = 0;
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
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();
	std::shared_timed_mutex& GetAccessMutex() { return m_accessRendererMutex; }
	
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
	std::shared_timed_mutex m_accessRendererMutex;

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
	void RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams);
	void* GetRenderDevice();
	std::shared_timed_mutex& GetAccessMutex() { return m_accessRendererMutex; }

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
	std::shared_timed_mutex m_accessRendererMutex;

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

	VkShaderModule m_fullscreenQuadVS;
	VkShaderModule m_passthroughVS;
	VkShaderModule m_passthroughPS;
	VkShaderModule m_alphaPrepassPS;
	VkShaderModule m_maskedPrepassShader;
	VkShaderModule m_maskedAlphaCopyPS;

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