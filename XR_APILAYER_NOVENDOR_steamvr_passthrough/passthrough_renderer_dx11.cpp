

#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <xr_linear.h>


#include "shaders\fullscreen_quad_vs.h"
#include "shaders\mesh_rigid_vs.h"
#include "shaders\passthrough_vs.h"
#include "shaders\passthrough_stereo_vs.h"
#include "shaders\passthrough_stereo_temporal_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\passthrough_temporal_ps.h"
#include "shaders\alpha_copy_masked_ps.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;



struct VSPassConstantBuffer
{
	XrMatrix4x4f disparityViewToWorldLeft;
	XrMatrix4x4f disparityViewToWorldRight;
	XrMatrix4x4f prevDisparityViewToWorldLeft;
	XrMatrix4x4f prevDisparityViewToWorldRight;
	XrMatrix4x4f disparityToDepth;
	uint32_t disparityTextureSize[2];
	float disparityDownscaleFactor;
	float cutoutFactor;
	float cutoutOffset;
	float cutoutFilterWidth;
	int32_t disparityFilterWidth;
	uint32_t bProjectBorders;
	uint32_t bFindDiscontinuities;
	uint32_t bUseDisparityTemporalFilter;
	float disparityTemporalFilterStrength;
	float disparityTemporalFilterDistance;
};

struct VSViewConstantBuffer
{
	XrMatrix4x4f cameraProjectionToWorld;
	XrMatrix4x4f worldToCameraProjection;
	XrMatrix4x4f worldToHMDProjection;
	XrMatrix4x4f prevCameraProjectionToWorld;
	XrMatrix4x4f prevWorldToCameraProjection;
	XrMatrix4x4f prevWorldToHMDProjection;
	XrMatrix4x4f prevDispWorldToCameraProjection;
	XrVector4f frameUVBounds;
	XrVector3f hmdViewWorldPos;
	float projectionDistance;
	float floorHeightOffset;
	uint32_t cameraViewIndex;
	uint32_t bWriteDisparityFilter;
	uint32_t bisFirstRender;
};

struct VSMeshConstantBuffer
{
	XrMatrix4x4f meshToWorldTransform;
};

struct PSPassConstantBuffer
{
	XrVector2f depthRange;
	XrVector2f depthCutoffRange;
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	float sharpness;
	int32_t temporalFilterinSampling;
	uint32_t bDoColorAdjustment;
	uint32_t bDebugDepth;
	uint32_t bDebugValidStereo;
	uint32_t bUseFisheyeCorrection;
	uint32_t bIsFirstRenderOfCameraFrame;
	uint32_t bUseDepthCutoffRange;
};

struct PSViewConstantBuffer
{
	XrVector4f frameUVBounds;
	XrVector4f prepassUVBounds;
	uint32_t rtArrayIndex;
	uint32_t bDoCutout;
	uint32_t bPremultiplyAlpha;
};

struct PSMaskedConstantBuffer
{
	float maskedKey[3];
	float maskedFracChroma;
	float maskedFracLuma;
	float maskedSmooth;
	uint32_t bMaskedUseCamera;
	uint32_t bMaskedInvert;
};


inline uint32_t Align(const uint32_t value, const uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

void UploadTexture(ComPtr<ID3D11DeviceContext> deviceContext, ComPtr<ID3D11Texture2D> uploadTexture, uint8_t* inputBuffer, int height, int width)
{
	D3D11_MAPPED_SUBRESOURCE resource = {};
	deviceContext->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &resource);
	uint8_t* writePtr = (uint8_t*)resource.pData;
	uint8_t* readPtr = inputBuffer;
	for (int i = 0; i < height; i++)
	{
		memcpy(writePtr, readPtr, width);
		writePtr += resource.RowPitch;
		readPtr += width;
	}
	deviceContext->Unmap(uploadTexture.Get(), 0);
}



PassthroughRendererDX11::PassthroughRendererDX11(ID3D11Device* device, HMODULE dllMoudule, std::shared_ptr<ConfigManager> configManager)
	: m_d3dDevice(device)
	, m_dllModule(dllMoudule)
	, m_configManager(configManager)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
	, m_cameraUndistortedTextureWidth(0)
	, m_cameraUndistortedTextureHeight(0)
	, m_cameraUndistortedFrameBufferSize(0)
	, m_disparityMapWidth(0)
	, m_fovScale(0.0f)
	, m_selectedDebugTexture(DebugTexture_None)
{
	
	m_bUseHexagonGridMesh = m_configManager->GetConfig_Stereo().StereoUseHexagonGridMesh;
}


bool PassthroughRendererDX11::InitRenderer()
{
	m_frameData.clear();
	m_uvDistortionMap.Reset();
	m_disparityMapWidth = 0;

	m_d3dDevice->GetImmediateContext(&m_deviceContext);


	if (FAILED(m_d3dDevice->CreateVertexShader(g_FullscreenQuadShaderVS, sizeof(g_FullscreenQuadShaderVS), nullptr, &m_fullscreenQuadShader)))
	{
		ErrorLog("g_FullscreenQuadShaderVS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_MeshRigidShaderVS, sizeof(g_MeshRigidShaderVS), nullptr, &m_meshRigidVertexShader)))
	{
		ErrorLog("g_MeshRigidShaderVS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS), nullptr, &m_vertexShader)))
	{
		ErrorLog("g_PassthroughShaderVS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughStereoShaderVS, sizeof(g_PassthroughStereoShaderVS), nullptr, &m_stereoVertexShader)))
	{
		ErrorLog("g_PassthroughStereoShaderVS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughStereoTemporalShaderVS, sizeof(g_PassthroughStereoTemporalShaderVS), nullptr, &m_stereoTemporalVertexShader)))
	{
		ErrorLog("g_PassthroughStereoTemporalShaderVS creation failure, temporal effects disabled.\n");
		m_bIsTemporalSupported = false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS), nullptr, &m_pixelShader)))
	{
		ErrorLog("g_PassthroughShaderPS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughTemporalShaderPS, sizeof(g_PassthroughTemporalShaderPS), nullptr, &m_pixelShaderTemporal)))
	{
		ErrorLog("g_PassthroughTemporalShaderPS creation failure, temporal effects disabled.\n");
		m_bIsTemporalSupported = false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassShaderPS, sizeof(g_AlphaPrepassShaderPS), nullptr, &m_prepassShader)))
	{
		ErrorLog("g_AlphaPrepassShaderPS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassMaskedShaderPS, sizeof(g_AlphaPrepassMaskedShaderPS), nullptr, &m_maskedPrepassShader)))
	{
		ErrorLog("g_AlphaPrepassMaskedShaderPS creation failure!\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaCopyMaskedShaderPS, sizeof(g_AlphaCopyMaskedShaderPS), nullptr, &m_maskedAlphaCopyShader)))
	{
		ErrorLog("g_AlphaCopyMaskedShaderPS creation failure!\n");
		return false;
	}


	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = Align(sizeof(VSMeshConstantBuffer), 16);
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	
	for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
	{
		if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_vsMeshConstantBuffer[i])))
		{
			ErrorLog("m_vsMeshConstantBuffer creation failure!\n");
			return false;
		}
	}

	

	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = false;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depth.StencilEnable = false;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateDisabled.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthEnable = true;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLess.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLessWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateGreater.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateGreaterWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}


	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	if (FAILED(m_d3dDevice->CreateSamplerState(&sampler, m_defaultSampler.GetAddressOf())))
	{
		ErrorLog("CreateSamplerState failure!\n");
		return false;
	}

	D3D11_BLEND_DESC blendState = {};
	blendState.RenderTarget[0].BlendEnable = true;
	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateDestAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateDestAlphaPremultiplied.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassUseAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassIgnoreAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassInverseAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}


	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.ScissorEnable = true;
	rasterizerDesc.DepthBias = 0;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.GetAddressOf())))
	{
		ErrorLog("CreateRasterizerState failure!\n");
		return false;
	}

	rasterizerDesc.DepthBias = 16;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerStateDepthBias.GetAddressOf())))
	{
		ErrorLog("CreateRasterizerState failure!\n");
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC vertexDesc{};
	vertexDesc.SemanticName = "POSITION";
	vertexDesc.SemanticIndex = 0;
	vertexDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexDesc.InputSlot = 0;
	vertexDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexDesc.InstanceDataStepRate = 0;

	if (FAILED(m_d3dDevice->CreateInputLayout(&vertexDesc, 1, g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS), &m_inputLayout)))
	{
		ErrorLog("CreateInputLayout failure!\n");
		return false;
	}

	GenerateMesh();

	return true;
}


void PassthroughRendererDX11::SetupDebugTexture(DebugTexture& texture)
{
	DXGI_FORMAT format;

	switch (texture.Format)
	{
	case DebugTextureFormat_RGBA8:
		format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		break;
	case DebugTextureFormat_R8:
		format = DXGI_FORMAT_R8_UNORM;
		break;
	case DebugTextureFormat_R16S:
		format = DXGI_FORMAT_R16_SNORM;
		break;
	case DebugTextureFormat_R16U:
		format = DXGI_FORMAT_R16_UNORM;
		break;
	case DebugTextureFormat_R32F:
		format = DXGI_FORMAT_R32_FLOAT;
		break;
	default:
		format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	}

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = format;
	textureDesc.Width = texture.Width;
	textureDesc.Height = texture.Height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_debugTexture)))
	{
		ErrorLog("Debug Texture CreateTexture2D error!\n");
		return;
	}

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_debugTextureUpload)))
	{
		ErrorLog("Debug Texture Upload CreateTexture2D error!\n");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(m_debugTexture.Get(), &srvDesc, &m_debugTextureSRV)))
	{
		ErrorLog("Debug Texture CreateShaderResourceView error!\n");
		return;
	}
}


void PassthroughRendererDX11::SetupCameraFrameResource(const uint32_t imageIndex)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraTextureWidth;
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraFrameUploadTexture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}

	std::vector<uint8_t> image(m_cameraFrameBufferSize);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	DX11FrameData& frameData = m_frameData[imageIndex];

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.cameraFrameTexture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}
	if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.cameraFrameTexture.Get(), &srvDesc, &frameData.cameraFrameSRV)))
	{
		ErrorLog("Frame Resource CreateShaderResourceView error!\n");
		return;
	}

	m_deviceContext->CopyResource(frameData.cameraFrameTexture.Get(), m_cameraFrameUploadTexture.Get());
}


void PassthroughRendererDX11::SetupCameraUndistortedFrameResource(const uint32_t imageIndex)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraUndistortedTextureWidth;
	textureDesc.Height = m_cameraUndistortedTextureHeight;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraUndistortedFrameUploadTexture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}

	std::vector<uint8_t> image(m_cameraUndistortedFrameBufferSize);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(m_cameraUndistortedFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(m_cameraUndistortedFrameUploadTexture.Get(), 0);

	DX11FrameData& frameData = m_frameData[imageIndex];

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.cameraUndistortedFrameTexture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}
	if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.cameraUndistortedFrameTexture.Get(), &srvDesc, &frameData.cameraUndistortedFrameSRV)))
	{
		ErrorLog("Frame Resource CreateShaderResourceView error!\n");
		return;
	}

	m_deviceContext->CopyResource(frameData.cameraUndistortedFrameTexture.Get(), m_cameraUndistortedFrameUploadTexture.Get());
}


void PassthroughRendererDX11::SetupDisparityMap(uint32_t width, uint32_t height)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16_SNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	D3D11_TEXTURE2D_DESC uavTextureDesc = {};
	uavTextureDesc.MipLevels = 1;
	uavTextureDesc.Format = DXGI_FORMAT_R16G16_SNORM;
	uavTextureDesc.Width = width;
	uavTextureDesc.Height = height;
	uavTextureDesc.ArraySize = 1;
	uavTextureDesc.SampleDesc.Count = 1;
	uavTextureDesc.SampleDesc.Quality = 0;
	uavTextureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	uavTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	uavTextureDesc.CPUAccessFlags = 0;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R16G16_SNORM;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_disparityMapUploadTexture)))
	{
		ErrorLog("Disparity Map CreateTexture2D error!\n");
		return;
	}


	for (int i = 0; i < m_frameData.size(); i++)
	{
		DX11FrameData& frameData = m_frameData[i];

		if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.disparityMap)))
		{
			ErrorLog("Disparity Map CreateTexture2D error!\n");
			return;
		}
		if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.disparityMap.Get(), &srvDesc, &frameData.disparityMapSRV)))
		{
			ErrorLog("Disparity Map CreateShaderResourceView error!\n");
			return;
		}

		if (m_configManager->GetConfig_Stereo().StereoUseDisparityTemporalFiltering)
		{

			if (FAILED(m_d3dDevice->CreateTexture2D(&uavTextureDesc, nullptr, &frameData.disparityMapUAVTexture)))
			{
				ErrorLog("Disparity Map UAV CreateTexture2D error!\n");
			}

			if (FAILED(m_d3dDevice->CreateUnorderedAccessView(frameData.disparityMapUAVTexture.Get(), &uavDesc, frameData.disparityMapUAV.GetAddressOf())))
			{
				ErrorLog("Frame Resource CreateUnorderedAccessView error!\n");
			}

			if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.disparityMapUAVTexture.Get(), &srvDesc, &frameData.disparityMapUAVSRV)))
			{
				ErrorLog("Disparity Map CreateShaderResourceView error!\n");
			}
		}
	}
}


void PassthroughRendererDX11::SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap)
{
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	textureDesc.Width = m_cameraTextureWidth;
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_uvDistortionMap)))
	{
		ErrorLog("UV Distortion Map CreateTexture2D error!\n");
		return;
	}

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ComPtr<ID3D11Texture2D> uploadTexture;
	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &uploadTexture)))
	{
		ErrorLog("UV Distortion Map CreateTexture2D error!\n");
		return;
	}

	UploadTexture(m_deviceContext, uploadTexture, (uint8_t*)uvDistortionMap->data(), m_cameraTextureHeight, m_cameraTextureWidth * sizeof(float) * 2);

	m_deviceContext->CopyResource(m_uvDistortionMap.Get(), uploadTexture.Get());

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(m_uvDistortionMap.Get(), &srvDesc, &m_uvDistortionMapSRV)))
	{
		ErrorLog("UV Distortion Map CreateShaderResourceView error!\n");
		return;
	}
}


DX11TemporaryRenderTarget& PassthroughRendererDX11::GetTemporaryRenderTarget(const uint32_t swapchainIndex, const uint32_t eyeIndex)
{
	DX11ViewData& viewData = m_viewData[eyeIndex][swapchainIndex];

	assert(viewData.renderTargets.Get());

	if (viewData.temporaryRenderTarget.AssociatedRenderTarget == viewData.renderTarget.Get())
	{
		return viewData.temporaryRenderTarget;
	}

	viewData.temporaryRenderTarget.AssociatedRenderTarget = viewData.renderTarget.Get();

	D3D11_TEXTURE2D_DESC finalRTDesc;
	((ID3D11Texture2D*)viewData.renderTarget.Get())->GetDesc(&finalRTDesc);

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.Width = finalRTDesc.Width;
	textureDesc.Height = finalRTDesc.Height;
	textureDesc.ArraySize = finalRTDesc.ArraySize;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	ID3D11Texture2D* texture;

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &texture)))
	{
		ErrorLog("Temporary Render Target CreateTexture2D error!\n");
		return viewData.temporaryRenderTarget;
	}

	viewData.temporaryRenderTarget.Texture = texture;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(texture, &srvDesc, &viewData.temporaryRenderTarget.SRV)))
	{
		ErrorLog("Temporary Render Target CreateShaderResourceView error!\n");
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = textureDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	if (FAILED(m_d3dDevice->CreateRenderTargetView(texture, &rtvDesc, &viewData.temporaryRenderTarget.RTV)))
	{
		ErrorLog("Temporary Render Target CreateRenderTargetView error!\n");
	}

	return viewData.temporaryRenderTarget;
}


void PassthroughRendererDX11::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	if (!CheckInitFrameData(imageIndex))
	{
		ErrorLog("Failed to init DX11 frame data!\n");
		return;
	}

	if (!CheckInitViewData(viewIndex, imageIndex))
	{
		ErrorLog("Failed to init DX11 view data!\n");
		return;
	}

	DX11ViewData& viewData = m_viewData[viewIndex][imageIndex];
	

	if (viewData.renderTarget.Get() == (ID3D11Resource*)rendertarget)
	{
		return;
	}

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;
	//rtvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;

	if (FAILED(m_d3dDevice->CreateRenderTargetView((ID3D11Resource*)rendertarget, &rtvDesc, viewData.renderTargetView.GetAddressOf())))
	{
		ErrorLog("Render Target CreateRenderTargetView error!\n");
		return;
	}

	viewData.renderTarget = (ID3D11Resource*)rendertarget;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	//srvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	//srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	//srvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateShaderResourceView((ID3D11Resource*)rendertarget, &srvDesc, &viewData.renderTargetSRV)))
	{
		ErrorLog("Render Target CreateShaderResourceView error!\n");
		return;
	}

	if (m_configManager->GetConfig_Main().EnableTemporalFiltering)
	{
		SetupTemporalUAV(viewIndex, imageIndex);
	}
	else if (viewData.cameraFilterSRV != nullptr)
	{
		// Free the UAV resources so that they will be recreated with the correct size in case it changed while temporal filtering was turned off.
		viewData.cameraFilterSRV.Reset();
		viewData.cameraFilterUAV.Reset();
		viewData.cameraFilterUAVTexture.Reset();
	}
}


bool PassthroughRendererDX11::CheckInitViewData(const uint32_t viewIndex, const uint32_t swapchainIndex)
{
	if (m_viewData[viewIndex].size() < swapchainIndex + 1)
	{
		m_viewData[viewIndex].resize(swapchainIndex + 1);
	}

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	if (viewData.bInitialized)
	{
		return true;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = Align(sizeof(VSViewConstantBuffer), 16);
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &viewData.vsViewConstantBuffer)))
	{
		ErrorLog("m_vsViewConstantBuffer creation failure!\n");
		return false;
	}

	bufferDesc.ByteWidth = Align(sizeof(PSViewConstantBuffer), 16);
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &viewData.psViewConstantBuffer)))
	{
		ErrorLog("m_psViewConstantBuffer creation failure!\n");
		return false;
	}

	viewData.bInitialized = true;

	return true;
}


bool PassthroughRendererDX11::CheckInitFrameData(const uint32_t imageIndex)
{
	if (m_frameData.size() < imageIndex + 1)
	{
		m_frameData.resize(imageIndex + 1);
	}

	DX11FrameData& frameData = m_frameData[imageIndex];

	if (frameData.bInitialized)
	{
		return true;
	}


	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	bufferDesc.ByteWidth = Align(sizeof(VSPassConstantBuffer), 16);
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &frameData.vsPassConstantBuffer)))
	{
		ErrorLog("m_vsPassConstantBuffer creation failure!\n");
		return false;
	}

	bufferDesc.ByteWidth = Align(sizeof(PSPassConstantBuffer), 16);
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &frameData.psPassConstantBuffer)))
	{
		ErrorLog("m_psPassConstantBuffer creation failure!\n");
		return false;
	}

	bufferDesc.ByteWidth = Align(sizeof(PSMaskedConstantBuffer), 16);
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &frameData.psMaskedConstantBuffer)))
	{
		ErrorLog("m_psMaskedConstantBuffer creation failure!\n");
		return false;
	}

	SetupCameraFrameResource(imageIndex);
	SetupCameraUndistortedFrameResource(imageIndex);

	frameData.bInitialized = true;

	return true;
}



void PassthroughRendererDX11::SetupTemporalUAV(const uint32_t viewIndex, const uint32_t swapchainIndex)
{
	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	D3D11_TEXTURE2D_DESC rtDesc;
	((ID3D11Texture2D*)viewData.renderTarget.Get())->GetDesc(&rtDesc);

	D3D11_TEXTURE2D_DESC uavTextureDesc = {};
	uavTextureDesc.MipLevels = 1;
	uavTextureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	uavTextureDesc.Width = rtDesc.Width;
	uavTextureDesc.Height = rtDesc.Height;
	uavTextureDesc.ArraySize = 1;
	uavTextureDesc.SampleDesc.Count = 1;
	uavTextureDesc.SampleDesc.Quality = 0;
	uavTextureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	uavTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	uavTextureDesc.CPUAccessFlags = 0;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uavTextureDesc, nullptr, &viewData.cameraFilterUAVTexture)))
	{
		ErrorLog("UAV CreateTexture2D error!\n");
		return;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	if (FAILED(m_d3dDevice->CreateUnorderedAccessView(viewData.cameraFilterUAVTexture.Get(), &uavDesc, viewData.cameraFilterUAV.GetAddressOf())))
	{
		ErrorLog("UAV CreateUnorderedAccessView error!\n");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC filterSRVDesc = {};
	filterSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	filterSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	filterSRVDesc.Texture2DArray.MipLevels = 1;
	filterSRVDesc.Texture2DArray.ArraySize = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(viewData.cameraFilterUAVTexture.Get(), &filterSRVDesc, &viewData.cameraFilterSRV)))
	{
		ErrorLog("UAV CreateShaderResourceView error!\n");
		return;
	}
}


void PassthroughRendererDX11::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	if (m_viewDepthData[viewIndex].size() < imageIndex + 1)
	{
		m_viewDepthData[viewIndex].resize(imageIndex + 1);
	}

	DX11ViewDepthData& depthData = m_viewDepthData[viewIndex][imageIndex];
	if (depthData.depthStencil.Get() == (ID3D11Resource*)depthBuffer)
	{
		return;
	}

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateDepthStencilView((ID3D11Resource*)depthBuffer, &dsvDesc, depthData.depthStencilView.GetAddressOf())))
	{
		ErrorLog("Depth map CreateDepthStencilView error!\n");
		return;
	}

	depthData.depthStencil = (ID3D11Resource*)depthBuffer;
}


void PassthroughRendererDX11::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;

	m_cameraUndistortedTextureWidth = undistortedWidth;
	m_cameraUndistortedTextureHeight = undistortedHeight;
	m_cameraUndistortedFrameBufferSize = undistortedBufferSize;
}


void PassthroughRendererDX11::GenerateMesh()
{
	MeshCreateCylinder(m_cylinderMesh, NUM_MESH_BOUNDARY_VERTICES);

	D3D11_SUBRESOURCE_DATA vertexBufferData{};
	vertexBufferData.pSysMem = m_cylinderMesh.vertices.data();

	CD3D11_BUFFER_DESC vertexBufferDesc((UINT)m_cylinderMesh.vertices.size() * sizeof(VertexFormatBasic), D3D11_BIND_VERTEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_cylinderMeshVertexBuffer)))
	{
		ErrorLog("Mesh vertex buffer creation error!\n");
		return;
	}

	D3D11_SUBRESOURCE_DATA indexBufferData{};
	indexBufferData.pSysMem = m_cylinderMesh.triangles.data();

	CD3D11_BUFFER_DESC indexBufferDesc((UINT)m_cylinderMesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_cylinderMeshIndexBuffer)))
	{
		ErrorLog("Mesh index buffer creation error!\n");
		return;
	}
}


void PassthroughRendererDX11::GenerateDepthMesh(uint32_t width, uint32_t height)
{
	m_bUseHexagonGridMesh ? MeshCreateHexGrid(m_gridMesh, width, height) : MeshCreateGrid(m_gridMesh, width, height);

	D3D11_SUBRESOURCE_DATA vertexBufferData{};
	vertexBufferData.pSysMem = m_gridMesh.vertices.data();

	CD3D11_BUFFER_DESC vertexBufferDesc((UINT)m_gridMesh.vertices.size() * sizeof(VertexFormatBasic), D3D11_BIND_VERTEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_gridMeshVertexBuffer)))
	{
		ErrorLog("Depth mesh vertex buffer creation error!\n");
		return;
	}

	D3D11_SUBRESOURCE_DATA indexBufferData{};
	indexBufferData.pSysMem = m_gridMesh.triangles.data();

	CD3D11_BUFFER_DESC indexBufferDesc((UINT)m_gridMesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_gridMeshIndexBuffer)))
	{
		ErrorLog("Depth mesh index buffer creation error!\n");
		return;
	}
}


void PassthroughRendererDX11::UpdateRenderModels(CameraFrame* frame)
{
	for (RenderModel& model : *frame->renderModels)
	{
		bool bFound = false;
		for (DX11RenderModel& dxModel : m_renderModels)
		{
			if (model.deviceId == dxModel.deviceId)
			{
				bFound = true;
				if (!dxModel.meshPtr || &model.mesh != dxModel.meshPtr)
				{
					dxModel.meshPtr = &model.mesh;

					D3D11_SUBRESOURCE_DATA vertexBufferData{};
					vertexBufferData.pSysMem = model.mesh.vertices.data();

					CD3D11_BUFFER_DESC vertexBufferDesc((UINT)model.mesh.vertices.size() * sizeof(VertexFormatBasic), D3D11_BIND_VERTEX_BUFFER);
					if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &dxModel.vertexBuffer)))
					{
						ErrorLog("Render model vertex buffer creation error!\n");
					}

					D3D11_SUBRESOURCE_DATA indexBufferData{};
					indexBufferData.pSysMem = model.mesh.triangles.data();

					CD3D11_BUFFER_DESC indexBufferDesc((UINT)model.mesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
					if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &dxModel.indexBuffer)))
					{
						ErrorLog("Render model index buffer creation error!\n");
					}

					dxModel.numIndices = (uint32_t)(model.mesh.triangles.size() * 3);
				}

				dxModel.meshToWorldTransform = model.meshToWorldTransform;

				break;
			}
		}

		if (!bFound)
		{
			DX11RenderModel dxModel;

			dxModel.deviceId = model.deviceId;
			dxModel.meshPtr = &model.mesh;

			D3D11_SUBRESOURCE_DATA vertexBufferData{};
			vertexBufferData.pSysMem = model.mesh.vertices.data();

			CD3D11_BUFFER_DESC vertexBufferDesc((UINT)model.mesh.vertices.size() * sizeof(VertexFormatBasic), D3D11_BIND_VERTEX_BUFFER);
			if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &dxModel.vertexBuffer)))
			{
				ErrorLog("Render model vertex buffer creation error!\n");
			}

			D3D11_SUBRESOURCE_DATA indexBufferData{};
			indexBufferData.pSysMem = model.mesh.triangles.data();

			CD3D11_BUFFER_DESC indexBufferDesc((UINT)model.mesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
			if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &dxModel.indexBuffer)))
			{
				ErrorLog("Render model index buffer creation error!\n");
			}

			dxModel.numIndices = (uint32_t)(model.mesh.triangles.size() * 3);
			dxModel.meshToWorldTransform = model.meshToWorldTransform;

			m_renderModels.push_back(dxModel);
		}
	}
}




#define GET_DEPTH_STENCIL_STATE(bEnabled, bReverse, bWrite) (bEnabled ? (bReverse ? \
	(bWrite ? m_depthStencilStateGreaterWrite.Get() : m_depthStencilStateGreater.Get() ) : \
	(bWrite ? m_depthStencilStateLessWrite.Get() : m_depthStencilStateLess.Get() ) ) : \
	m_depthStencilStateDisabled.Get() )


void PassthroughRendererDX11::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams)
{
	m_prevFrameIndex = m_frameIndex;
	//Relying on the application not doing anything too weird with the swaphain indices
	m_frameIndex = leftSwapchainIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];

	DX11ViewData& viewDataLeft = m_viewData[0][leftSwapchainIndex];
	DX11ViewData& viewDataRight = m_viewData[1][rightSwapchainIndex];

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Core& coreConf = m_configManager->GetConfig_Core();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	if (SUCCEEDED(m_d3dDevice->CreateDeferredContext(0, &m_renderContext)))
	{
		m_bUsingDeferredContext = true;
		m_renderContext->ClearState();
	}
	else
	{
		m_bUsingDeferredContext = false;
		m_renderContext = m_deviceContext;
	}

	if (mainConf.ProjectionMode == Projection_StereoReconstruction && !depthFrame->bIsValid)
	{
		return;
	}

	{
		std::shared_lock readLock(distortionParams.readWriteMutex);

		if (mainConf.ProjectionMode != Projection_RoomView2D &&
			(!m_uvDistortionMap.Get() || m_fovScale != distortionParams.fovScale))
		{
			m_fovScale = distortionParams.fovScale;
			SetupUVDistortionMap(distortionParams.uvDistortionMap);
		}
	}

	if (mainConf.ProjectToRenderModels)
	{
		UpdateRenderModels(frame);
	}

	if (mainConf.EnableTemporalFiltering)
	{
		if (viewDataLeft.cameraFilterSRV == nullptr) { SetupTemporalUAV(0, leftSwapchainIndex); }
		if (viewDataRight.cameraFilterSRV == nullptr) { SetupTemporalUAV(1, rightSwapchainIndex); }
	}

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		std::shared_lock readLock(depthFrame->readWriteMutex);

		if (depthFrame->disparityTextureSize[0] != m_disparityMapWidth || m_bUseHexagonGridMesh != stereoConf.StereoUseHexagonGridMesh || (stereoConf.StereoUseDisparityTemporalFiltering && frameData.disparityMapUAVTexture == nullptr))
		{
			m_disparityMapWidth = depthFrame->disparityTextureSize[0];
			m_bUseHexagonGridMesh = stereoConf.StereoUseHexagonGridMesh;
			SetupDisparityMap(depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1]);
			GenerateDepthMesh(depthFrame->disparityTextureSize[0] / 2, depthFrame->disparityTextureSize[1]);
		}

		UploadTexture(m_deviceContext, m_disparityMapUploadTexture, (uint8_t*)depthFrame->disparityMap->data(), depthFrame->disparityTextureSize[1], depthFrame->disparityTextureSize[0] * sizeof(uint16_t) * 2);

		m_deviceContext->CopyResource(frameData.disparityMap.Get(), m_disparityMapUploadTexture.Get());

		if (stereoConf.StereoUseDisparityTemporalFiltering)
		{
			ID3D11ShaderResourceView* vsSRVs[2] = { frameData.disparityMapSRV.Get(), prevFrameData.disparityMapUAVSRV.Get() };
			m_renderContext->VSSetShaderResources(0, 2, vsSRVs);
		}
		else
		{
			ID3D11ShaderResourceView* vsSRVs[1] = { frameData.disparityMapSRV.Get() };
			m_renderContext->VSSetShaderResources(0, 1, vsSRVs);
		}

		VSPassConstantBuffer vsBuffer{};
		vsBuffer.disparityViewToWorldLeft = depthFrame->disparityViewToWorldLeft;
		vsBuffer.disparityViewToWorldRight = depthFrame->disparityViewToWorldRight;
		vsBuffer.prevDisparityViewToWorldLeft = depthFrame->prevDisparityViewToWorldLeft;
		vsBuffer.prevDisparityViewToWorldRight = depthFrame->prevDisparityViewToWorldRight;
		vsBuffer.disparityToDepth = depthFrame->disparityToDepth;
		vsBuffer.disparityDownscaleFactor = depthFrame->disparityDownscaleFactor;
		vsBuffer.disparityTextureSize[0] = depthFrame->disparityTextureSize[0];
		vsBuffer.disparityTextureSize[1] = depthFrame->disparityTextureSize[1];
		vsBuffer.cutoutFactor = stereoConf.StereoCutoutFactor;
		vsBuffer.cutoutOffset = stereoConf.StereoCutoutOffset;
		vsBuffer.cutoutFilterWidth = stereoConf.StereoCutoutFilterWidth;
		vsBuffer.disparityFilterWidth = stereoConf.StereoDisparityFilterWidth;
		vsBuffer.bProjectBorders = !stereoConf.StereoReconstructionFreeze;
		vsBuffer.bFindDiscontinuities = stereoConf.StereoCutoutEnabled;
		vsBuffer.bUseDisparityTemporalFilter = stereoConf.StereoUseDisparityTemporalFiltering;
		vsBuffer.disparityTemporalFilterStrength = stereoConf.StereoDisparityTemporalFilteringStrength;
		vsBuffer.disparityTemporalFilterDistance = stereoConf.StereoDisparityTemporalFilteringDistance;
		m_renderContext->UpdateSubresource(frameData.vsPassConstantBuffer.Get(), 0, nullptr, &vsBuffer, 0, 0);
	}

	ID3D11ShaderResourceView* psSRVs[2];
	psSRVs[1] = m_uvDistortionMapSRV.Get();
	bool bGotDebugTexture = false;

	if (mainConf.DebugTexture != DebugTexture_None)
	{
		DebugTexture& texture = m_configManager->GetDebugTexture();
		std::lock_guard<std::mutex> readlock(texture.RWMutex);

		if (texture.CurrentTexture == mainConf.DebugTexture)
		{
			if (!m_debugTextureSRV.Get() || texture.CurrentTexture != m_selectedDebugTexture || texture.bDimensionsUpdated)
			{
				SetupDebugTexture(texture);

				m_selectedDebugTexture = texture.CurrentTexture;
				texture.bDimensionsUpdated = false;
			}

			if (m_debugTextureUpload.Get())
			{
				UploadTexture(m_deviceContext, m_debugTextureUpload, texture.Texture.data(), texture.Height, texture.Width * texture.PixelSize);

				m_deviceContext->CopyResource(m_debugTexture.Get(), m_debugTextureUpload.Get());

				psSRVs[0] = m_debugTextureSRV.Get();
				bGotDebugTexture = true;
			}
		}	
	}

	if (!bGotDebugTexture && frame->frameTextureResource != nullptr)
	{
		// Use shared texture
		psSRVs[0] = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else if(!bGotDebugTexture && frame->frameBuffer.get() != nullptr)
	{
		// Upload camera frame from CPU

		if (frame->header.eFrameType == vr::VRTrackedCameraFrameType_Distorted)
		{
			UploadTexture(m_deviceContext, m_cameraFrameUploadTexture, (uint8_t*)frame->frameBuffer->data(), m_cameraTextureHeight, m_cameraTextureWidth * 4);

			m_deviceContext->CopyResource(frameData.cameraFrameTexture.Get(), m_cameraFrameUploadTexture.Get());

			psSRVs[0] = frameData.cameraFrameSRV.Get();
		}
		else
		{
			UploadTexture(m_deviceContext, m_cameraUndistortedFrameUploadTexture, (uint8_t*)frame->frameBuffer->data(), m_cameraUndistortedTextureHeight, m_cameraUndistortedTextureWidth * 4);

			m_deviceContext->CopyResource(frameData.cameraUndistortedFrameTexture.Get(), m_cameraUndistortedFrameUploadTexture.Get());

			psSRVs[0] = frameData.cameraUndistortedFrameSRV.Get();
		}
	}
	else if (!bGotDebugTexture) // No valid frame texture to render
	{
		return;
	}

	m_renderContext->PSSetShaderResources(0, 2, psSRVs);

	m_renderContext->IASetInputLayout(m_inputLayout.Get());
	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };

	m_renderContext->RSSetState(m_rasterizerState.Get());

	m_renderContext->VSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());
	m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	UINT numIndices;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		numIndices = (UINT)m_gridMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		
		if (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsTemporalSupported)
		{
			m_renderContext->VSSetShader(m_stereoTemporalVertexShader.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_stereoVertexShader.Get(), nullptr, 0);
		}
	}
	else
	{
		numIndices = (UINT)m_cylinderMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);	
	}

	PSPassConstantBuffer psBuffer = {};
	psBuffer.depthRange = XrVector2f(NEAR_PROJECTION_DISTANCE, mainConf.ProjectionDistanceFar);
	psBuffer.depthCutoffRange = XrVector2f(renderParams.DepthRangeMin, renderParams.DepthRangeMax);
	psBuffer.opacity = mainConf.PassthroughOpacity;
	psBuffer.brightness = mainConf.Brightness;
	psBuffer.contrast = mainConf.Contrast;
	psBuffer.saturation = mainConf.Saturation;
	psBuffer.sharpness = mainConf.Sharpness;
	psBuffer.temporalFilterinSampling = mainConf.TemporalFilteringSampling;
	psBuffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;
	psBuffer.bDebugDepth = mainConf.DebugDepth;
	psBuffer.bDebugValidStereo = mainConf.DebugStereoValid;
	psBuffer.bUseFisheyeCorrection = mainConf.ProjectionMode != Projection_RoomView2D;
	psBuffer.bIsFirstRenderOfCameraFrame = frame->bIsFirstRender;
	psBuffer.bUseDepthCutoffRange = renderParams.bEnableDepthRange;

	m_renderContext->UpdateSubresource(frameData.psPassConstantBuffer.Get(), 0, nullptr, &psBuffer, 0, 0);

	if (blendMode == Masked)
	{
		PSMaskedConstantBuffer maskedBuffer = {};
		maskedBuffer.maskedKey[0] = powf(coreConf.CoreForceMaskedKeyColor[0], 2.2f);
		maskedBuffer.maskedKey[1] = powf(coreConf.CoreForceMaskedKeyColor[1], 2.2f);
		maskedBuffer.maskedKey[2] = powf(coreConf.CoreForceMaskedKeyColor[2], 2.2f);
		maskedBuffer.maskedFracChroma = coreConf.CoreForceMaskedFractionChroma * 100.0f;
		maskedBuffer.maskedFracLuma = coreConf.CoreForceMaskedFractionLuma * 100.0f;
		maskedBuffer.maskedSmooth = coreConf.CoreForceMaskedSmoothing * 100.0f;
		maskedBuffer.bMaskedUseCamera = coreConf.CoreForceMaskedUseCameraImage;
		maskedBuffer.bMaskedInvert = coreConf.CoreForceMaskedInvertMask;

		m_renderContext->UpdateSubresource(frameData.psMaskedConstantBuffer.Get(), 0, nullptr, &maskedBuffer, 0, 0);

		RenderMaskedPrepassView(LEFT_EYE, leftSwapchainIndex, leftDepthSwapchainIndex, layer, frame, depthFrame, numIndices, renderParams);
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, leftDepthSwapchainIndex, layer, frame, depthFrame, blendMode, numIndices, renderParams);
		RenderMaskedPrepassView(RIGHT_EYE, rightSwapchainIndex, rightDepthSwapchainIndex, layer, frame, depthFrame, numIndices, renderParams);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, rightDepthSwapchainIndex, layer, frame, depthFrame, blendMode, numIndices, renderParams);
	}
	else
	{
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, leftDepthSwapchainIndex, layer, frame, depthFrame, blendMode, numIndices, renderParams);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, rightDepthSwapchainIndex, layer, frame, depthFrame, blendMode, numIndices, renderParams);
	}

	RenderFrameFinish();

	m_prevSwapchainLeft = leftSwapchainIndex;
	m_prevSwapchainRight = rightSwapchainIndex;
}


void PassthroughRendererDX11::RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const int32_t depthSwapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, EPassthroughBlendMode blendMode, UINT numIndices, FrameRenderParameters& renderParams)
{
	if (swapchainIndex < 0) { return; }

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTargetView.Get();
	if (!rendertarget) { return; }

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	ID3D11UnorderedAccessView* UAVs[2] = { viewData.cameraFilterUAV.Get(), frameData.disparityMapUAV.Get() };

	m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 2, UAVs, nullptr);


	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	VSViewConstantBuffer vsViewBuffer = {};
	vsViewBuffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	vsViewBuffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;

	vsViewBuffer.prevCameraProjectionToWorld = (eye == LEFT_EYE) ? frame->prevCameraProjectionToWorldLeft : frame->prevCameraProjectionToWorldRight;
	vsViewBuffer.prevWorldToCameraProjection = (eye == LEFT_EYE) ? frame->prevWorldToCameraProjectionLeft : frame->prevWorldToCameraProjectionRight;
	vsViewBuffer.prevWorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevWorldToHMDProjectionLeft : frame->prevWorldToHMDProjectionRight;

	//TODO actual layout
	vsViewBuffer.frameUVBounds = GetFrameUVBounds(eye, mainConf.CameraProvider == CameraProvider_Augmented ? StereoHorizontalLayout : frame->frameLayout);
	vsViewBuffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer.cameraViewIndex = (eye == LEFT_EYE) ? 0 : 1;
	vsViewBuffer.bWriteDisparityFilter = stereoConf.StereoUseDisparityTemporalFiltering && depthFrame->bIsFirstRender;
	vsViewBuffer.bisFirstRender = stereoConf.StereoUseDisparityTemporalFiltering && depthFrame->bIsFirstRender;

	
	m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
	
	ID3D11Buffer* vsBuffers[3] = { viewData.vsViewConstantBuffer.Get(), frameData.vsPassConstantBuffer.Get(), nullptr };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);
	
	PSViewConstantBuffer psViewBuffer = {};
	psViewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer.bDoCutout = !bCompositeDepth; // can only clip invalid areas when not using depth.
	psViewBuffer.bPremultiplyAlpha = (blendMode == AlphaBlendPremultiplied) && !bCompositeDepth;

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[2] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	// Extra draw if we need to preadjust the alpha.
	if (blendMode != Masked && ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || mainConf.PassthroughOpacity < 1.0f || bCompositeDepth))
	{
		m_renderContext->PSSetShader(m_prepassShader.Get(), nullptr, 0);

		if (bCompositeDepth && blendMode != Additive)
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassInverseAppAlpha.Get(), nullptr, UINT_MAX);
		}
		else if (blendMode == AlphaBlendPremultiplied || blendMode == AlphaBlendUnpremultiplied)
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassUseAppAlpha.Get(), nullptr, UINT_MAX);
		}
		else
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassIgnoreAppAlpha.Get(), nullptr, UINT_MAX);
		}

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, bWriteDepth), 1);

		m_renderContext->DrawIndexed(numIndices, 0, 0);

		bWriteDepth = false;

		if (vsViewBuffer.bWriteDisparityFilter)
		{
			vsViewBuffer.bWriteDisparityFilter = false;
			m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
		}
	}
	else if(stereoConf.StereoUseDisparityTemporalFiltering && m_bIsTemporalSupported && depthFrame->bIsFirstRender)
	{
		vsViewBuffer.bWriteDisparityFilter = true;
		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
	}



	if ((blendMode == AlphaBlendPremultiplied && !bCompositeDepth) || blendMode == Additive)
	{
		m_renderContext->OMSetBlendState(m_blendStateDestAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);
	}

	if (mainConf.EnableTemporalFiltering && m_bIsTemporalSupported)
	{
		ID3D11ShaderResourceView* psSRVs[3];
		m_renderContext->PSGetShaderResources(0, 2, psSRVs);

		int prevSwapchain = (eye == LEFT_EYE) ? m_prevSwapchainLeft : m_prevSwapchainRight;
		psSRVs[2] = m_viewData[viewIndex][prevSwapchain].cameraFilterSRV.Get();
		m_renderContext->PSSetShaderResources(0, 3, psSRVs);
	}

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, bWriteDepth), 1);

	m_renderContext->PSSetShader(mainConf.EnableTemporalFiltering ? m_pixelShaderTemporal.Get() : m_pixelShader.Get(), nullptr, 0);



	// Project passthrough onto tracked devices
	if (mainConf.ProjectToRenderModels)
	{
		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
		m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

		m_renderContext->VSSetShader(m_meshRigidVertexShader.Get(), nullptr, 0);

		for (DX11RenderModel model : m_renderModels)
		{
			VSMeshConstantBuffer vsMeshBuffer;
			vsMeshBuffer.meshToWorldTransform = model.meshToWorldTransform;
			m_renderContext->UpdateSubresource(m_vsMeshConstantBuffer[model.deviceId].Get(), 0, nullptr, &vsMeshBuffer, 0, 0);
			vsBuffers[2] = m_vsMeshConstantBuffer[model.deviceId].Get();
			m_renderContext->VSSetConstantBuffers(0, 3, vsBuffers);

			const UINT strides[] = { sizeof(float) * 3 };
			const UINT offsets[] = { 0 };
			m_renderContext->IASetVertexBuffers(0, 1, model.vertexBuffer.GetAddressOf(), strides, offsets);
			m_renderContext->IASetIndexBuffer(model.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			m_renderContext->DrawIndexed(model.numIndices, 0, 0);
		}

		m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);
	}


	// Main pass

	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{	
		numIndices = (UINT)m_gridMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

		if (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsTemporalSupported)
		{
			m_renderContext->VSSetShader(m_stereoTemporalVertexShader.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_stereoVertexShader.Get(), nullptr, 0);
		}
	}
	else
	{
		numIndices = (UINT)m_cylinderMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	}
	
	m_renderContext->DrawIndexed(numIndices, 0, 0);




	// Draw the other stereo camera on occluded areas
	if (stereoConf.StereoCutoutEnabled)
	{
		float secondaryWidthFactor = 0.6f;
		int scissorStart = (eye == LEFT_EYE) ? (int)(rect.extent.width * (1.0f - secondaryWidthFactor)) : 0;
		int scissorEnd = (eye == LEFT_EYE) ? rect.extent.width : (int)(rect.extent.width * secondaryWidthFactor);
		D3D11_RECT crossScissor = { rect.offset.x + scissorStart, rect.offset.y, rect.offset.x + scissorEnd, rect.offset.y + rect.extent.height };
		m_renderContext->RSSetScissorRects(1, &crossScissor);

		VSViewConstantBuffer vsCrossBuffer = vsViewBuffer;
		vsCrossBuffer.frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, StereoHorizontalLayout);
		vsCrossBuffer.cameraProjectionToWorld = (eye != LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
		vsCrossBuffer.worldToCameraProjection = (eye != LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
		vsCrossBuffer.cameraViewIndex = (eye != LEFT_EYE) ? 0 : 1;
		vsCrossBuffer.bWriteDisparityFilter = false;
		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsCrossBuffer, 0, 0);

		m_renderContext->OMSetBlendState(m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);

		PSViewConstantBuffer psCrossBuffer = psViewBuffer;
		psCrossBuffer.frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, frame->frameLayout);
		psCrossBuffer.bDoCutout = true;
		psCrossBuffer.bPremultiplyAlpha = false;
		m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psCrossBuffer, 0, 0);

		// Reverse depth check to only affect ares below previously drawn
		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, !frame->bHasReversedDepth, false), 1);
		m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);

		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}


	// Draw cylinder mesh to fill out any holes
	if(stereoConf.StereoFillHoles && mainConf.ProjectionMode == Projection_StereoReconstruction && !stereoConf.StereoReconstructionFreeze && !renderParams.bEnableDepthRange)
	{
		m_renderContext->RSSetScissorRects(1, &scissor);

		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
		m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

		const UINT strides[] = { sizeof(float) * 3 };
		const UINT offsets[] = { 0 };
		m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_renderContext->RSSetState(m_rasterizerStateDepthBias.Get());

		m_renderContext->OMSetBlendState(m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication), 1);

		m_renderContext->DrawIndexed((UINT)m_cylinderMesh.triangles.size() * 3, 0, 0);


		m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		if (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsTemporalSupported)
		{
			m_renderContext->VSSetShader(m_stereoTemporalVertexShader.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_stereoVertexShader.Get(), nullptr, 0);
		}
		m_renderContext->RSSetState(m_rasterizerState.Get());
	}
}


void PassthroughRendererDX11::RenderMaskedPrepassView(const ERenderEye eye, const int32_t swapchainIndex, int32_t depthSwapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	if (swapchainIndex < 0) { return; }

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTargetView.Get();
	if (!rendertarget) { return; }

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	VSViewConstantBuffer vsViewBuffer = {};
	vsViewBuffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	vsViewBuffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	vsViewBuffer.frameUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
	vsViewBuffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer.cameraViewIndex = (eye == LEFT_EYE) ? 0 : 1;

	m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);

	ID3D11Buffer* vsBuffers[2] = { viewData.vsViewConstantBuffer.Get(), frameData.vsPassConstantBuffer.Get() };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);

	bool bSingleStereoRenderTarget = false;

	PSViewConstantBuffer psViewBuffer = {};
	// Draw the correct half for single framebuffer views.
	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	{
		bSingleStereoRenderTarget = true;

		psViewBuffer.prepassUVBounds = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f,
			(eye == LEFT_EYE) ? 0.5f : 1.0f, 1.0f };
	}
	else
	{
		psViewBuffer.prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	}
	psViewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer.bDoCutout = false;
	psViewBuffer.bPremultiplyAlpha = false;

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);


	DX11TemporaryRenderTarget& tempTarget = GetTemporaryRenderTarget(m_frameIndex, bSingleStereoRenderTarget ? 0 : viewIndex);

	if (eye == LEFT_EYE || !bSingleStereoRenderTarget)
	{
		float clearColor[4] = { m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage ? 1.0f : 0, 0, 0, 0 };
		m_renderContext->ClearRenderTargetView(tempTarget.RTV.Get(), clearColor);
	}

	m_renderContext->OMSetRenderTargets(1, tempTarget.RTV.GetAddressOf(), depthStencil);
	m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage == frame->bHasReversedDepth, bWriteDepth), 1);

	ID3D11ShaderResourceView* cameraFrameSRV;

	if (mainConf.DebugTexture != DebugTexture_None)
	{
		cameraFrameSRV = m_debugTextureSRV.Get();
	}
	else if (frame->frameTextureResource != nullptr)
	{
		cameraFrameSRV = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else
	{
		cameraFrameSRV = frameData.cameraFrameSRV.Get();
	}

	ID3D11ShaderResourceView* prepassSourceTexture;

	if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		prepassSourceTexture = cameraFrameSRV;
	}
	else
	{
		prepassSourceTexture = viewData.renderTargetSRV.Get();
	}

	if (mainConf.ProjectionMode == Projection_RoomView2D)
	{
		m_renderContext->PSSetShaderResources(0, 1, &prepassSourceTexture);
	}
	else
	{
		ID3D11ShaderResourceView* views[2] = { prepassSourceTexture, m_uvDistortionMapSRV.Get() };
		m_renderContext->PSSetShaderResources(0, 2, views);
	}

	ID3D11Buffer* psBuffers[3] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get(), frameData.psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	m_renderContext->PSSetShader(m_maskedPrepassShader.Get(), nullptr, 0);

	// Draw with simple vertex shader if we don't need to sample camera
	if (!bCompositeDepth && !m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_renderContext->VSSetShader(m_fullscreenQuadShader.Get(), nullptr, 0);
		m_renderContext->Draw(3, 0);
	}
	else
	{
		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}

	
	// Clear rendertarget so we can swap the places of the RTV and SRV.
	ID3D11RenderTargetView* nullRTV = nullptr;
	m_renderContext->OMSetRenderTargets(1, &nullRTV, nullptr);

	if (mainConf.ProjectionMode == Projection_RoomView2D)
	{
		ID3D11ShaderResourceView* views[3] = { cameraFrameSRV, nullptr, tempTarget.SRV.Get() };
		m_renderContext->PSSetShaderResources(0, 3, views);
	}
	else
	{
		ID3D11ShaderResourceView* views[3] = { cameraFrameSRV, m_uvDistortionMapSRV.Get(), tempTarget.SRV.Get() };
		m_renderContext->PSSetShaderResources(0, 3, views);
	}

	ID3D11ShaderResourceView* views[3] = { cameraFrameSRV, nullptr, tempTarget.SRV.Get() };

	
	psViewBuffer.bDoCutout = !stereoConf.StereoCutoutEnabled;
	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

	m_renderContext->OMSetRenderTargets(1, &rendertarget, depthStencil);
	m_renderContext->OMSetBlendState(m_blendStateSrcAlpha.Get(), nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(false, frame->bHasReversedDepth, false), 1);

	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_renderContext->VSSetShader(m_fullscreenQuadShader.Get(), nullptr, 0);
	m_renderContext->PSSetShader(m_maskedAlphaCopyShader.Get(), nullptr, 0);

	m_renderContext->Draw(3, 0);



	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsTemporalSupported)
		{
			m_renderContext->VSSetShader(m_stereoTemporalVertexShader.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_stereoVertexShader.Get(), nullptr, 0);
		}
	}
	else
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	}
}


void PassthroughRendererDX11::RenderFrameFinish()
{
	if (m_bUsingDeferredContext)
	{
		ComPtr<ID3D11CommandList> commandList;
		m_renderContext->FinishCommandList(false, commandList.GetAddressOf());
		m_deviceContext->ExecuteCommandList(commandList.Get(), true);
		m_renderContext.Reset();
	}
}


void* PassthroughRendererDX11::GetRenderDevice()
{
	return m_d3dDevice.Get();
}