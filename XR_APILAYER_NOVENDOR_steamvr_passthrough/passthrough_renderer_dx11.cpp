

#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <PathCch.h>
#include <xr_linear.h>
#include "lodepng.h"

#include "shaders\fullscreen_quad_vs.h"
#include "shaders\passthrough_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\passthrough_masked_ps.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


struct VSConstantBuffer
{
	XrMatrix4x4f cameraUVProjectionFar;
	XrMatrix4x4f cameraUVProjectionNear;
};


struct PSPassConstantBuffer
{
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	bool bDoColorAdjustment;
};

struct PSViewConstantBuffer
{
	XrVector2f frameUVOffset;
	XrVector2f prepassUVFactor;
	XrVector2f prepassUVOffset;
	uint32_t rtArrayIndex;
};

struct PSMaskedConstantBuffer
{
	float maskedKey[3];
	float maskedFracChroma;
	float maskedFracLuma;
	float maskedSmooth;
	bool bMaskedUseCamera;
};



PassthroughRendererDX11::PassthroughRendererDX11(ID3D11Device* device, HMODULE dllMoudule, std::shared_ptr<ConfigManager> configManager)
	: m_d3dDevice(device)
	, m_dllModule(dllMoudule)
	, m_configManager(configManager)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
{
}


bool PassthroughRendererDX11::InitRenderer()
{
	m_d3dDevice->GetImmediateContext(&m_deviceContext);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_FullscreenQuadShaderVS, sizeof(g_FullscreenQuadShaderVS), nullptr, &m_quadShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS), nullptr, &m_vertexShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS), nullptr, &m_pixelShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassShaderPS, sizeof(g_AlphaPrepassShaderPS), nullptr, &m_prepassShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassMaskedShaderPS, sizeof(g_AlphaPrepassMaskedShaderPS), nullptr, &m_maskedPrepassShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughMaskedShaderPS, sizeof(g_PassthroughMaskedShaderPS), nullptr, &m_maskedPixelShader)))
	{
		return false;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(XrMatrix4x4f) * 2;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++) 
	{
		if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr,  &m_vsConstantBuffer[i])))
		{
			return false;
		}
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psPassConstantBuffer)))
	{
		return false;
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psViewConstantBuffer)))
	{
		return false;
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psMaskedConstantBuffer)))
	{
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
		return false;
	}

	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateAlphaPremultiplied.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassUseAppAlpha.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassIgnoreAppAlpha.GetAddressOf())))
	{
		return false;
	}


	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.FrontCounterClockwise = true;
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.ScissorEnable = true;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.GetAddressOf())))
	{
		return false;
	}

	SetupTestImage();
	SetupFrameResource();

	return true;
}


void PassthroughRendererDX11::SetupTestImage()
{
	char path[MAX_PATH];

	if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	{
		ErrorLog("Error opening test pattern.\n");
	}

	std::string pathStr = path;
	std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\testpattern.png";

	std::vector<unsigned char> image;
	unsigned width, height;

	unsigned error = lodepng::decode(image, width, height, imgPath.c_str());
	if (error)
	{
		ErrorLog("Error decoding test pattern.\n");
	}

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_testPatternTexture);

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ComPtr<ID3D11Texture2D> uploadTexture;
	m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &uploadTexture);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	m_d3dDevice->CreateShaderResourceView(m_testPatternTexture.Get(), &srvDesc, &m_testPatternSRV);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(uploadTexture.Get(), 0);

	m_deviceContext->CopyResource(m_testPatternTexture.Get(), uploadTexture.Get());
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

	m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraFrameUploadTexture);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_cameraFrameTexture[i]);
		m_d3dDevice->CreateShaderResourceView(m_cameraFrameTexture[i].Get(), &srvDesc, &m_cameraFrameSRV[i]);
		m_deviceContext->CopyResource(m_cameraFrameTexture[i].Get(), m_cameraFrameUploadTexture.Get());
	}
}


void PassthroughRendererDX11::SetupTemporaryRenderTarget(ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, uint32_t width, uint32_t height)
{

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, texture)))
	{
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	m_d3dDevice->CreateShaderResourceView(*texture, &srvDesc, srv);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = textureDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	m_d3dDevice->CreateRenderTargetView(*texture, &rtvDesc, rtv);
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

	m_d3dDevice->CreateRenderTargetView((ID3D11Resource*)rendertarget, &rtvDesc, m_renderTargetViews[bufferIndex].GetAddressOf());
	m_renderTargets[bufferIndex] = (ID3D11Resource*)rendertarget;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension =  D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	//srvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	//srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	//srvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	m_d3dDevice->CreateShaderResourceView((ID3D11Resource*)rendertarget, &srvDesc, &m_renderTargetSRVs[bufferIndex]);
}


void PassthroughRendererDX11::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}


inline bool IsLinearFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return false;

	default: 
		return true;
	}
}


void PassthroughRendererDX11::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex)
{
	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Core& coreConf = m_configManager->GetConfig_Core();

	if(SUCCEEDED(m_d3dDevice->CreateDeferredContext(0, &m_renderContext)))
	{
		m_bUsingDeferredContext = true;
		m_renderContext->ClearState();
	}
	else
	{
		m_bUsingDeferredContext = false;
		m_renderContext = m_deviceContext;
	}

	if (mainConf.ShowTestImage)
	{
		m_renderContext->PSSetShaderResources(0, 1, m_testPatternSRV.GetAddressOf());
	}
	else if (frame->frameTextureResource != nullptr)
	{
		// Use shared texture
		m_renderContext->PSSetShaderResources(0, 1, (ID3D11ShaderResourceView* const*)&frame->frameTextureResource);
	}
	else
	{
		// Upload camera frame from CPU
		D3D11_MAPPED_SUBRESOURCE res = {};
		m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
		memcpy(res.pData, frame->frameBuffer->data(), frame->frameBuffer->size());
		m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

		m_deviceContext->CopyResource(m_cameraFrameTexture[m_frameIndex].Get(), m_cameraFrameUploadTexture.Get());

		m_renderContext->PSSetShaderResources(0, 1, m_cameraFrameSRV[m_frameIndex].GetAddressOf());
	}

	m_renderContext->IASetInputLayout(nullptr);
	m_renderContext->IASetVertexBuffers(0, 0, nullptr, 0, 0);
	m_renderContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_renderContext->RSSetState(m_rasterizerState.Get());

	m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	PSPassConstantBuffer buffer = {};
	buffer.opacity = mainConf.PassthroughOpacity;
	buffer.brightness = mainConf.Brightness;
	buffer.contrast = mainConf.Contrast;
	buffer.saturation = mainConf.Saturation;
	buffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;

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

		m_renderContext->UpdateSubresource(m_psMaskedConstantBuffer.Get(), 0, nullptr, &maskedBuffer, 0, 0);

		RenderPassthroughViewMasked(LEFT_EYE, leftSwapchainIndex, layer, frame);
		RenderPassthroughViewMasked(RIGHT_EYE, rightSwapchainIndex, layer, frame);
	}
	else
	{
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode);
	}
	
	RenderFrameFinish();
}


void PassthroughRendererDX11::RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	ID3D11RenderTargetView* rendertarget =  m_renderTargetViews[bufferIndex].Get();

	if (!rendertarget) { return; }

	m_renderContext->OMSetRenderTargets(1, &rendertarget, nullptr);

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSConstantBuffer buffer = {};
	buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;
	
	m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());
	m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	
	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[2] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	// Extra draw if we need to preadjust the alpha.
	if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f)
	{
		m_renderContext->PSSetShader(m_prepassShader.Get(), nullptr, 0);

		if (blendMode == AlphaBlendPremultiplied || blendMode == AlphaBlendUnpremultiplied)
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassUseAppAlpha.Get(), nullptr, UINT_MAX);
		}
		else
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassIgnoreAppAlpha.Get(), nullptr, UINT_MAX);
		}

		m_renderContext->Draw(3, 0);
	}


	if (blendMode == AlphaBlendPremultiplied)
	{
		m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else if (blendMode == AlphaBlendUnpremultiplied)
	{
		m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	}
	else if (blendMode == Additive)
	{
		m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	}

	m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	
	m_renderContext->Draw(3, 0);
}


void PassthroughRendererDX11::RenderPassthroughViewMasked(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame)
{
	if (swapchainIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	ID3D11RenderTargetView* rendertarget = m_renderTargetViews[bufferIndex].Get();

	if (!rendertarget) { return; }

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { 0, 0, rect.extent.width, rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSConstantBuffer buffer = {};
	buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());

	m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);

	{
		PSViewConstantBuffer viewBuffer = {};
		// Draw the correct half for single framebuffer views.
		if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
		{
			viewBuffer.prepassUVOffset = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f };
			viewBuffer.prepassUVFactor = { 0.5f, 1.0f };
		}
		else
		{
			viewBuffer.prepassUVOffset = { 0.0f, 0.0f };
			viewBuffer.prepassUVFactor = { 1.0f, 1.0f };
		}
		viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
		viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

		m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);
	}

	ComPtr<ID3D11Texture2D> tempTexture;
	ComPtr<ID3D11ShaderResourceView> tempSRV;
	ComPtr<ID3D11RenderTargetView> tempRTV;

	SetupTemporaryRenderTarget(&tempTexture, &tempSRV, &tempRTV, (uint32_t)rect.extent.width, (uint32_t)rect.extent.height);

	m_renderContext->OMSetRenderTargets(1, tempRTV.GetAddressOf(), nullptr);
	m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);


	ID3D11ShaderResourceView* cameraFrameSRV;

	if (m_configManager->GetConfig_Main().ShowTestImage)
	{
		cameraFrameSRV = m_testPatternSRV.Get();
	}
	else if (frame->frameTextureResource != nullptr)
	{
		cameraFrameSRV = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else
	{
		cameraFrameSRV = m_cameraFrameSRV[m_frameIndex].Get();
	}


	if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		m_renderContext->PSSetShaderResources(0, 1, &cameraFrameSRV);
	}
	else
	{
		m_renderContext->PSSetShaderResources(0, 1, m_renderTargetSRVs[bufferIndex].GetAddressOf());
	}

	ID3D11Buffer* psBuffers[3] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get(), m_psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	m_renderContext->PSSetShader(m_maskedPrepassShader.Get(), nullptr, 0);

	m_renderContext->Draw(3, 0);


	{
		D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
		D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

		m_renderContext->RSSetViewports(1, &viewport);
		m_renderContext->RSSetScissorRects(1, &scissor);
	}

	// Clear rendertarget so we can swap the places of the RTV and SRV.
	ID3D11RenderTargetView* nullRTV = nullptr;
	m_renderContext->OMSetRenderTargets(1, &nullRTV, nullptr);

	ID3D11ShaderResourceView* views[2] = { cameraFrameSRV, tempSRV.Get() };
	m_renderContext->PSSetShaderResources(0, 2, views);
	m_renderContext->OMSetRenderTargets(1, &rendertarget, nullptr);
	m_renderContext->OMSetBlendState(m_blendStateSrcAlpha.Get(), nullptr, UINT_MAX);	
	m_renderContext->PSSetShader(m_maskedPixelShader.Get(), nullptr, 0);

	m_renderContext->Draw(3, 0);
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