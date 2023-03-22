

#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <xr_linear.h>


#include "shaders\passthrough_vs.h"
#include "shaders\passthrough_stereo_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\passthrough_masked_ps.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


struct VSPassConstantBuffer
{
	XrMatrix4x4f disparityViewToWorldLeft;
	XrMatrix4x4f disparityViewToWorldRight;
	XrMatrix4x4f disparityToDepth;
	uint32_t disparityTextureSize[2];
	float disparityDownscaleFactor;
};

struct VSViewConstantBuffer
{
	XrMatrix4x4f cameraProjectionToWorld;
	XrMatrix4x4f worldToCameraProjection;
	XrMatrix4x4f worldToHMDProjection;
	XrVector4f frameUVBounds;
	XrVector3f hmdViewWorldPos;
	float projectionDistance;
	float floorHeightOffset;
};

struct PSPassConstantBuffer
{
	XrVector2f depthRange;
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	uint32_t bDoColorAdjustment;
	uint32_t bDebugDepth;
	uint32_t bDebugValidStereo;
	uint32_t bUseFisheyeCorrection;
};

struct PSViewConstantBuffer
{
	XrVector4f frameUVBounds;
	XrVector4f prepassUVBounds;
	uint32_t rtArrayIndex;
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
	, m_disparityMapWidth(0)
	, m_fovScale(0.0f)
	, m_selectedDebugTexture(DebugTexture_None)
{
	memset(m_temportaryRenderTargets, 0, sizeof(m_temportaryRenderTargets));
}


bool PassthroughRendererDX11::InitRenderer()
{
	m_d3dDevice->GetImmediateContext(&m_deviceContext);


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

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS), nullptr, &m_pixelShader)))
	{
		ErrorLog("g_PassthroughShaderPS creation failure!\n");
		return false;
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

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughMaskedShaderPS, sizeof(g_PassthroughMaskedShaderPS), nullptr, &m_maskedPixelShader)))
	{
		ErrorLog("g_PassthroughMaskedShaderPS creation failure!\n");
		return false;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(XrMatrix4x4f) * 4;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++) 
	{
		if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr,  &m_vsViewConstantBuffer[i])))
		{
			ErrorLog("m_vsViewConstantBuffer creation failure!\n");
			return false;
		}
	}

	bufferDesc.ByteWidth = 208;
	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_vsPassConstantBuffer[i])))
		{
			ErrorLog("m_vsPassConstantBuffer creation failure!\n");
			return false;
		}
	}


	bufferDesc.ByteWidth = 48;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psPassConstantBuffer)))
	{
		ErrorLog("m_psPassConstantBuffer creation failure!\n");
		return false;
	}

	bufferDesc.ByteWidth = 48;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psViewConstantBuffer)))
	{
		ErrorLog("m_psViewConstantBuffer creation failure!\n");
		return false;
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psMaskedConstantBuffer)))
	{
		ErrorLog("m_psMaskedConstantBuffer creation failure!\n");
		return false;
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
	depth.DepthFunc = D3D11_COMPARISON_LESS;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLess.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLessWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_GREATER;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateGreater.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_GREATER;
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
	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateBase.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateAlphaPremultiplied.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
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
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.GetAddressOf())))
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

	SetupFrameResource();
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


void PassthroughRendererDX11::SetupFrameResource()
{
	std::vector<uint8_t> image(m_cameraFrameBufferSize);

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

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraFrameUploadTexture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_cameraFrameTexture[i])))
		{
			ErrorLog("Frame Resource CreateTexture2D error!\n");
			return;
		}
		if (FAILED(m_d3dDevice->CreateShaderResourceView(m_cameraFrameTexture[i].Get(), &srvDesc, &m_cameraFrameSRV[i])))
		{
			ErrorLog("Frame Resource CreateShaderResourceView error!\n");
			return;
		}
		m_deviceContext->CopyResource(m_cameraFrameTexture[i].Get(), m_cameraFrameUploadTexture.Get());
	}
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

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_disparityMapUploadTexture)))
	{
		ErrorLog("Disparity Map CreateTexture2D error!\n");
		return;
	}

	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
	{
		if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_disparityMap[i])))
		{
			ErrorLog("Disparity Map CreateTexture2D error!\n");
			return;
		}
		if (FAILED(m_d3dDevice->CreateShaderResourceView(m_disparityMap[i].Get(), &srvDesc, &m_disparityMapSRV[i])))
		{
			ErrorLog("Disparity Map CreateShaderResourceView error!\n");
			return;
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


DX11TemporaryRenderTarget& PassthroughRendererDX11::GetTemporaryRenderTarget(uint32_t bufferIndex)
{
	assert(m_renderTargets[bufferIndex].Get());

	if (m_temportaryRenderTargets[bufferIndex].AssociatedRenderTarget == m_renderTargets[bufferIndex].Get())
	{
		return m_temportaryRenderTargets[bufferIndex];
	}

	m_temportaryRenderTargets[bufferIndex].AssociatedRenderTarget = m_renderTargets[bufferIndex].Get();

	D3D11_TEXTURE2D_DESC finalRTDesc;
	((ID3D11Texture2D*)m_renderTargets[bufferIndex].Get())->GetDesc(&finalRTDesc);

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
		return m_temportaryRenderTargets[bufferIndex];
	}

	m_temportaryRenderTargets[bufferIndex].Texture = texture;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(texture, &srvDesc, &m_temportaryRenderTargets[bufferIndex].SRV)))
	{
		ErrorLog("Temporary Render Target CreateShaderResourceView error!\n");
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = textureDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	if (FAILED(m_d3dDevice->CreateRenderTargetView(texture, &rtvDesc, &m_temportaryRenderTargets[bufferIndex].RTV)))
	{
		ErrorLog("Temporary Render Target CreateRenderTargetView error!\n");
	}

	return m_temportaryRenderTargets[bufferIndex];
}


void PassthroughRendererDX11::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;
	if (m_renderTargets[bufferIndex].Get() == (ID3D11Resource*)rendertarget)
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

	if (FAILED(m_d3dDevice->CreateRenderTargetView((ID3D11Resource*)rendertarget, &rtvDesc, m_renderTargetViews[bufferIndex].GetAddressOf())))
	{
		ErrorLog("Render Target CreateRenderTargetView error!\n");
		return;
	}

	m_renderTargets[bufferIndex] = (ID3D11Resource*)rendertarget;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension =  D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	//srvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	//srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	//srvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateShaderResourceView((ID3D11Resource*)rendertarget, &srvDesc, &m_renderTargetSRVs[bufferIndex])))
	{
		ErrorLog("Render Target CreateRenderTargetView error!\n");
		return;
	}
}


void PassthroughRendererDX11::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;
	if (m_depthStencils[bufferIndex].Get() == (ID3D11Resource*)depthBuffer)
	{
		return;
	}

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateDepthStencilView((ID3D11Resource*)depthBuffer, &dsvDesc, m_depthStencilViews[bufferIndex].GetAddressOf())))
	{
		ErrorLog("Depth map CreateDepthStencilView error!\n");
		return;
	}

	m_depthStencils[bufferIndex] = (ID3D11Resource*)depthBuffer;
}


void PassthroughRendererDX11::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}


void PassthroughRendererDX11::GenerateMesh()
{
	m_vertices.reserve(NUM_MESH_BOUNDARY_VERTICES * 4 * 6);

	// Generate a triangle strip cylinder with radius and height 1.

	float radianStep = -2.0f * MATH_PI / (float)NUM_MESH_BOUNDARY_VERTICES;

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(0.0f);
		m_vertices.push_back(1.0f);
		m_vertices.push_back(0.0f);

		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(1.0f);
		m_vertices.push_back(sinf(radianStep * i));
	}

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(1.0f);
		m_vertices.push_back(sinf(radianStep * i));

		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(0.0f);
		m_vertices.push_back(sinf(radianStep * i));
	}

	for (int i = 0; i <= NUM_MESH_BOUNDARY_VERTICES; i++)
	{
		m_vertices.push_back(cosf(radianStep * i));
		m_vertices.push_back(0.0f);
		m_vertices.push_back(sinf(radianStep * i));

		m_vertices.push_back(0.0f);
		m_vertices.push_back(0.0f);
		m_vertices.push_back(0.0f);
	}

	D3D11_SUBRESOURCE_DATA vertexBufferData{};
	vertexBufferData.pSysMem = m_vertices.data();

	CD3D11_BUFFER_DESC vertexBufferDesc((UINT)m_vertices.size() * sizeof(float), D3D11_BIND_VERTEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_vertexBuffer)))
	{
		ErrorLog("Mesh CreateBuffer error!\n");
		return;
	}
}


void PassthroughRendererDX11::GenerateDepthMesh(uint32_t width, uint32_t height)
{
	m_stereoVertices.reserve((width + 1) * (height + 1) * 2);

	float step = 1.0f / (float)height;

	for (int y = 0; y < (int)height; y += 2)
	{
		float y_pos = y * step;
		float y_pos1 = (y + 1) * step;
		float y_pos2 = (y + 2) * step;

		for (int x = 0; x <= (int)width; x++)
		{
			float x_pos = x * step;
			

			m_stereoVertices.push_back(x_pos);
			m_stereoVertices.push_back(y_pos1);
			m_stereoVertices.push_back(1.0f);

			m_stereoVertices.push_back(x_pos);
			m_stereoVertices.push_back(y_pos);
			m_stereoVertices.push_back(1.0f);
		}

		for (int x = (int)width; x >= 0; x--)
		{
			float x_pos = x * step;

			m_stereoVertices.push_back(x_pos);
			m_stereoVertices.push_back(y_pos1);
			m_stereoVertices.push_back(1.0f);

			m_stereoVertices.push_back(x_pos);
			m_stereoVertices.push_back(y_pos2);
			m_stereoVertices.push_back(1.0f);
		}
	}
	
	D3D11_SUBRESOURCE_DATA vertexBufferData{};
	vertexBufferData.pSysMem = m_stereoVertices.data();

	CD3D11_BUFFER_DESC vertexBufferDesc((UINT)m_stereoVertices.size() * sizeof(float), D3D11_BIND_VERTEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_stereoVertexBuffer)))
	{
		ErrorLog("Stereo Mesh CreateBuffer error!\n");
		return;
	}
}





#define GET_DEPTH_STENCIL_STATE(bEnabled, bReverse, bWrite) (bEnabled ? (bReverse ? \
	(bWrite ? m_depthStencilStateGreaterWrite.Get() : m_depthStencilStateGreater.Get() ) : \
	(bWrite ? m_depthStencilStateLessWrite.Get() : m_depthStencilStateLess.Get() ) ) : \
	m_depthStencilStateDisabled.Get() )


void PassthroughRendererDX11::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams)
{
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

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		std::shared_lock readLock(depthFrame->readWriteMutex);

		if (depthFrame->disparityTextureSize[0] != m_disparityMapWidth)
		{
			m_disparityMapWidth = depthFrame->disparityTextureSize[0];
			SetupDisparityMap(depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1]);
			GenerateDepthMesh(depthFrame->disparityTextureSize[0] / 2, depthFrame->disparityTextureSize[1]);
		}

		UploadTexture(m_deviceContext, m_disparityMapUploadTexture, (uint8_t*)depthFrame->disparityMap->data(), depthFrame->disparityTextureSize[1], depthFrame->disparityTextureSize[0] * sizeof(uint16_t) * 2);

		m_deviceContext->CopyResource(m_disparityMap[m_frameIndex].Get(), m_disparityMapUploadTexture.Get());

		ID3D11ShaderResourceView* vsSRVs[1] = { m_disparityMapSRV[m_frameIndex].Get() };
		m_renderContext->VSSetShaderResources(0, 1, vsSRVs);


		VSPassConstantBuffer vsBuffer{};
		vsBuffer.disparityViewToWorldLeft = depthFrame->disparityViewToWorldLeft;
		vsBuffer.disparityViewToWorldRight = depthFrame->disparityViewToWorldRight;
		vsBuffer.disparityToDepth = depthFrame->disparityToDepth;
		vsBuffer.disparityDownscaleFactor = depthFrame->disparityDownscaleFactor;
		vsBuffer.disparityTextureSize[0] = depthFrame->disparityTextureSize[0];
		vsBuffer.disparityTextureSize[1] = depthFrame->disparityTextureSize[1];
		m_renderContext->UpdateSubresource(m_vsPassConstantBuffer[m_frameIndex].Get(), 0, nullptr, &vsBuffer, 0, 0);
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
	else if(!bGotDebugTexture)
	{
		// Upload camera frame from CPU
		UploadTexture(m_deviceContext, m_cameraFrameUploadTexture, (uint8_t*)frame->frameBuffer->data(), m_cameraTextureHeight, m_cameraTextureWidth * 4);

		m_deviceContext->CopyResource(m_cameraFrameTexture[m_frameIndex].Get(), m_cameraFrameUploadTexture.Get());

		psSRVs[0] = m_cameraFrameSRV[m_frameIndex].Get();
	}

	m_renderContext->PSSetShaderResources(0, 2, psSRVs);

	m_renderContext->IASetInputLayout(m_inputLayout.Get());
	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };
	
	m_renderContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_renderContext->RSSetState(m_rasterizerState.Get());

	m_renderContext->VSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());
	m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	UINT numVertices;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		numVertices = (UINT)m_stereoVertices.size() / 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_stereoVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->VSSetShader(m_stereoVertexShader.Get(), nullptr, 0);
	}
	else
	{
		numVertices = (UINT)m_vertices.size() / 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	}

	PSPassConstantBuffer buffer = {};
	buffer.depthRange = XrVector2f(NEAR_PROJECTION_DISTANCE, mainConf.ProjectionDistanceFar);
	buffer.opacity = mainConf.PassthroughOpacity;
	buffer.brightness = mainConf.Brightness;
	buffer.contrast = mainConf.Contrast;
	buffer.saturation = mainConf.Saturation;
	buffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;
	buffer.bDebugDepth = mainConf.DebugDepth;
	buffer.bDebugValidStereo = mainConf.DebugStereoValid;
	buffer.bUseFisheyeCorrection = mainConf.ProjectionMode != Projection_RoomView2D;

	m_renderContext->UpdateSubresource(m_psPassConstantBuffer.Get(), 0, nullptr, &buffer, 0, 0);

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

		m_renderContext->UpdateSubresource(m_psMaskedConstantBuffer.Get(), 0, nullptr, &maskedBuffer, 0, 0);

		RenderPassthroughViewMasked(LEFT_EYE, leftSwapchainIndex, layer, frame, numVertices);
		RenderPassthroughViewMasked(RIGHT_EYE, rightSwapchainIndex, layer, frame, numVertices);
	}
	else
	{
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode, numVertices);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode, numVertices);
	}

	RenderFrameFinish();
}


void PassthroughRendererDX11::RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, UINT numVertices)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	ID3D11RenderTargetView* rendertarget =  m_renderTargetViews[bufferIndex].Get();
	ID3D11DepthStencilView* depthStencil = m_depthStencilViews[bufferIndex].Get();

	if (!rendertarget) { return; }

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = depthConfig.DepthForceComposition && depthConfig.DepthReadFromApplication;

	m_renderContext->OMSetRenderTargets(1, &rendertarget, depthStencil);


	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	VSViewConstantBuffer buffer = {};
	buffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	buffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	buffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	buffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	buffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	buffer.projectionDistance = mainConf.ProjectionDistanceFar;
	buffer.floorHeightOffset = mainConf.FloorHeightOffset;
	
	m_renderContext->UpdateSubresource(m_vsViewConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);
	
	ID3D11Buffer* vsBuffers[2] = { m_vsViewConstantBuffer[bufferIndex].Get(), m_vsPassConstantBuffer[m_frameIndex].Get() };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);
	
	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[2] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	// Extra draw if we need to preadjust the alpha.
	if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f || bCompositeDepth)
	{
		m_renderContext->PSSetShader(m_prepassShader.Get(), nullptr, 0);

		if (bCompositeDepth)
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

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, false), 1);

		m_renderContext->Draw(numVertices, 0);
	}


	if (blendMode == AlphaBlendPremultiplied && !bCompositeDepth)
	{
		m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else if (blendMode == AlphaBlendUnpremultiplied)
	{
		m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	}
	else if (blendMode == Additive && !bCompositeDepth)
	{
		m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	}

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, depthConfig.DepthWriteOutput), 1);

	m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	
	m_renderContext->Draw(numVertices, 0);




	buffer.frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, frame->frameLayout);
	buffer.cameraProjectionToWorld = (eye != LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	buffer.worldToCameraProjection = (eye != LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	buffer.hmdViewWorldPos = (eye != LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	m_renderContext->UpdateSubresource(m_vsViewConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);
	

	/*if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f || bCompositeDepth)
	{*/
		/*m_renderContext->PSSetShader(m_prepassShader.Get(), nullptr, 0);

		if (bCompositeDepth)
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

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, false), 1);

		m_renderContext->Draw(numVertices, 0);*/



		/*if (blendMode == AlphaBlendPremultiplied && !bCompositeDepth)
		{
			m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
		}
		else if (blendMode == AlphaBlendUnpremultiplied)
		{
			m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
		}
		else if (blendMode == Additive && !bCompositeDepth)
		{
			m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
		}
		else
		{
			m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
		}*/

	viewBuffer.frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, frame->frameLayout);

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(false, frame->bHasReversedDepth, depthConfig.DepthWriteOutput), 1);

		m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	//}

	m_renderContext->Draw(numVertices, 0);
}


void PassthroughRendererDX11::RenderPassthroughViewMasked(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, UINT numVertices)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	ID3D11RenderTargetView* rendertarget = m_renderTargetViews[bufferIndex].Get();
	ID3D11DepthStencilView* depthStencil = m_depthStencilViews[bufferIndex].Get();

	if (!rendertarget) { return; }

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = depthConfig.DepthForceComposition && depthConfig.DepthReadFromApplication;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	VSViewConstantBuffer buffer = {};
	buffer.cameraProjectionToWorld = (eye == LEFT_EYE) ? frame->cameraProjectionToWorldLeft : frame->cameraProjectionToWorldRight;
	buffer.worldToCameraProjection = (eye == LEFT_EYE) ? frame->worldToCameraProjectionLeft : frame->worldToCameraProjectionRight;
	buffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	buffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	buffer.hmdViewWorldPos = (eye == LEFT_EYE) ? frame->hmdViewPosWorldLeft : frame->hmdViewPosWorldRight;
	buffer.projectionDistance = mainConf.ProjectionDistanceFar;
	buffer.floorHeightOffset = mainConf.FloorHeightOffset;

	m_renderContext->UpdateSubresource(m_vsViewConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	ID3D11Buffer* vsBuffers[2] = { m_vsViewConstantBuffer[bufferIndex].Get(), m_vsPassConstantBuffer[m_frameIndex].Get() };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);

	bool bSingleStereoRenderTarget = false;

	PSViewConstantBuffer viewBuffer = {};
	// Draw the correct half for single framebuffer views.
	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	{
		bSingleStereoRenderTarget = true;

		viewBuffer.prepassUVBounds = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f,
			(eye == LEFT_EYE) ? 0.5f : 1.0f, 1.0f };
	}
	else
	{
		viewBuffer.prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	}
	viewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);


	DX11TemporaryRenderTarget& tempTarget = GetTemporaryRenderTarget(bSingleStereoRenderTarget ? swapchainIndex : bufferIndex);

	if (eye == LEFT_EYE || !bSingleStereoRenderTarget)
	{
		float clearColor[4] = { m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage ? 1.0f : 0, 0, 0, 0 };
		m_renderContext->ClearRenderTargetView(tempTarget.RTV.Get(), clearColor);
	}

	m_renderContext->OMSetRenderTargets(1, tempTarget.RTV.GetAddressOf(), depthStencil);
	m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage == frame->bHasReversedDepth, depthConfig.DepthWriteOutput), 1);

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
		cameraFrameSRV = m_cameraFrameSRV[m_frameIndex].Get();
	}

	ID3D11ShaderResourceView* prepassSourceTexture;

	if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		prepassSourceTexture = cameraFrameSRV;
	}
	else
	{
		prepassSourceTexture = m_renderTargetSRVs[bufferIndex].Get();
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

	ID3D11Buffer* psBuffers[3] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get(), m_psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	m_renderContext->PSSetShader(m_maskedPrepassShader.Get(), nullptr, 0);

	m_renderContext->Draw(numVertices, 0);


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
	
	m_renderContext->OMSetRenderTargets(1, &rendertarget, depthStencil);
	m_renderContext->OMSetBlendState(m_blendStateSrcAlpha.Get(), nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(false, frame->bHasReversedDepth, false), 1);
	m_renderContext->PSSetShader(m_maskedPixelShader.Get(), nullptr, 0);

	m_renderContext->Draw(numVertices, 0);
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

	m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
}


void* PassthroughRendererDX11::GetRenderDevice()
{
	return m_d3dDevice.Get();
}