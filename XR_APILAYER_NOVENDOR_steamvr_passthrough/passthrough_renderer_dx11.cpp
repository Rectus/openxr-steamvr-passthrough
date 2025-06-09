

#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <xr_linear.h>
#include <dxgidebug.h>


#include "shaders\fullscreen_quad_vs.h"
#include "shaders\mesh_rigid_vs.h"
#include "shaders\passthrough_vs.h"
#include "shaders\passthrough_read_depth_vs.h"
#include "shaders\passthrough_stereo_vs.h"
#include "shaders\passthrough_stereo_temporal_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\alpha_prepass_masked_fullscreen_ps.h"
#include "shaders\depth_write_ps.h"
#include "shaders\depth_write_temporal_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\passthrough_stereo_composite_ps.h"
#include "shaders\passthrough_stereo_composite_temporal_ps.h"
#include "shaders\passthrough_temporal_ps.h"
#include "shaders\alpha_copy_masked_ps.h"
#include "shaders\fullscreen_passthrough_ps.h"
#include "shaders\fullscreen_passthrough_temporal_ps.h"
#include "shaders\fullscreen_passthrough_composite_ps.h"
#include "shaders\fullscreen_passthrough_composite_temporal_ps.h"
#include "shaders\debug_alpha_to_color_ps.h"
#include "shaders\debug_depth_to_color_ps.h"

#include "shaders\fill_holes_cs.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


#define SET_DXGI_DEBUGNAME(object) \
constexpr char object##_Name[] = #object; \
object->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof(object##_Name), object##_Name);



inline uint32_t Align(const uint32_t value, const uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

void UploadTexture(ComPtr<ID3D11DeviceContext> deviceContext, ComPtr<ID3D11Texture2D> uploadTexture, uint8_t* inputBuffer, int height, int width)
{
	if (inputBuffer == nullptr) { return; }

	D3D11_MAPPED_SUBRESOURCE resource = {};
	if (deviceContext->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &resource) == S_OK)
	{
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
	m_uvDistortionMap.Texture.Reset();
	m_disparityMapWidth = 0;

	m_d3dDevice->GetImmediateContext(&m_deviceContext);


	if (FAILED(m_d3dDevice->CreateComputeShader(g_FillHolesCS, sizeof(g_FillHolesCS), nullptr, &m_fillHolesCS)))
	{
		ErrorLog("g_FillHolesCS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fillHolesCS);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_FullscreenQuadVS, sizeof(g_FullscreenQuadVS), nullptr, &m_fullscreenQuadVS)))
	{
		ErrorLog("g_FullscreenQuadVS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fullscreenQuadVS);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_MeshRigidVS, sizeof(g_MeshRigidVS), nullptr, &m_meshRigidVS)))
	{
		ErrorLog("g_MeshRigidVS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_meshRigidVS);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughVS, sizeof(g_PassthroughVS), nullptr, &m_passthroughVS)))
	{
		ErrorLog("g_PassthroughVS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_passthroughVS);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughStereoVS, sizeof(g_PassthroughStereoVS), nullptr, &m_passthroughStereoVS)))
	{
		ErrorLog("g_PassthroughStereoVS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_passthroughStereoVS);

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughStereoTemporalVS, sizeof(g_PassthroughStereoTemporalVS), nullptr, &m_passthroughStereoTemporalVS)))
	{
		ErrorLog("g_PassthroughStereoTemporalVS creation failure, temporal disparity filter disabled.\n");
		m_bIsVSUAVSupported = false;
	}
	else
	{
		SET_DXGI_DEBUGNAME(m_passthroughStereoTemporalVS);
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughReadDepthVS, sizeof(g_PassthroughReadDepthVS), nullptr, &m_passthroughReadDepthVS)))
	{
		ErrorLog("g_PassthroughReadDepthVS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_passthroughReadDepthVS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughPS, sizeof(g_PassthroughPS), nullptr, &m_passthroughPS)))
	{
		ErrorLog("g_PassthroughPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_passthroughPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughTemporalPS, sizeof(g_PassthroughTemporalPS), nullptr, &m_passthroughTemporalPS)))
	{
		ErrorLog("g_PassthroughTemporalPS creation failure.\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_passthroughTemporalPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassPS, sizeof(g_AlphaPrepassPS), nullptr, &m_alphaPrepassPS)))
	{
		ErrorLog("g_AlphaPrepassPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_alphaPrepassPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassMaskedPS, sizeof(g_AlphaPrepassMaskedPS), nullptr, &m_maskedAlphaPrepassPS)))
	{
		ErrorLog("g_AlphaPrepassMaskedPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_maskedAlphaPrepassPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassMaskedFullscreenPS, sizeof(g_AlphaPrepassMaskedFullscreenPS), nullptr, &m_maskedAlphaPrepassFullscreenPS)))
	{
		ErrorLog("g_AlphaPrepassMaskedFullscreenPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_maskedAlphaPrepassFullscreenPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaCopyMaskedPS, sizeof(g_AlphaCopyMaskedPS), nullptr, &m_maskedAlphaCopyPS)))
	{
		ErrorLog("g_AlphaCopyMaskedPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_maskedAlphaCopyPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_depthWritePS, sizeof(g_depthWritePS), nullptr, &m_depthWritePS)))
	{
		ErrorLog("g_depthWritePS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthWritePS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_depthWriteTemporalPS, sizeof(g_depthWriteTemporalPS), nullptr, &m_depthWriteTemporalPS)))
	{
		ErrorLog("g_depthWriteTemporalPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthWriteTemporalPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughStereoCompositePS, sizeof(g_PassthroughStereoCompositePS), nullptr, &m_stereoCompositePS)))
	{
		ErrorLog("g_PassthroughStereoCompositePS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_stereoCompositePS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughStereoCompositeTemporalPS, sizeof(g_PassthroughStereoCompositeTemporalPS), nullptr, &m_stereoCompositeTemporalPS)))
	{
		ErrorLog("g_PassthroughStereoCompositeTemporalPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_stereoCompositeTemporalPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_FullscreenPassthroughPS, sizeof(g_FullscreenPassthroughPS), nullptr, &m_fullscreenPassthroughPS)))
	{
		ErrorLog("g_FullscreenPassthroughPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fullscreenPassthroughPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_FullscreenPassthroughTemporalPS, sizeof(g_FullscreenPassthroughTemporalPS), nullptr, &m_fullscreenPassthroughTemporalPS)))
	{
		ErrorLog("g_FullscreenPassthroughTemporalPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fullscreenPassthroughTemporalPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_FullscreenPassthroughCompositePS, sizeof(g_FullscreenPassthroughCompositePS), nullptr, &m_fullscreenPassthroughCompositePS)))
	{
		ErrorLog("g_FullscreenPassthroughCompositePS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fullscreenPassthroughCompositePS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_FullscreenPassthroughCompositeTemporalPS, sizeof(g_FullscreenPassthroughCompositeTemporalPS), nullptr, &m_fullscreenPassthroughCompositeTemporalPS)))
	{
		ErrorLog("g_FullscreenPassthroughCompositeTemporalPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_fullscreenPassthroughCompositeTemporalPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_debugAlphaToColorPS, sizeof(g_debugAlphaToColorPS), nullptr, &m_debugAlphaToColorPS)))
	{
		ErrorLog("g_debugAlphaToColorPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_debugAlphaToColorPS);

	if (FAILED(m_d3dDevice->CreatePixelShader(g_debugDepthToColorPS, sizeof(g_debugDepthToColorPS), nullptr, &m_debugDepthToColorPS)))
	{
		ErrorLog("g_debugDepthToColorPS creation failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_debugDepthToColorPS);


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
	SET_DXGI_DEBUGNAME(m_depthStencilStateDisabled);

	depth.DepthEnable = true;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLess.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthStencilStateLess);

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateLessWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthStencilStateLessWrite);

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateGreater.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthStencilStateGreater);

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateGreaterWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthStencilStateGreaterWrite);

	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depth.StencilEnable = true;
	depth.StencilReadMask = 0x1;
	depth.StencilWriteMask = 0x1;
	depth.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
	depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
	depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	depth.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
	depth.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depth.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depth.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	if (FAILED(m_d3dDevice->CreateDepthStencilState(&depth, m_depthStencilStateAlwaysWrite.GetAddressOf())))
	{
		ErrorLog("CreateDepthStencilState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_depthStencilStateAlwaysWrite);


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
	SET_DXGI_DEBUGNAME(m_defaultSampler);

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
	SET_DXGI_DEBUGNAME(m_blendStateDestAlpha);


	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateInvDestAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateInvDestAlpha);

	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateDestAlphaPremultiplied.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateDestAlphaPremultiplied);

	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateInvDestAlphaPremultiplied.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateInvDestAlphaPremultiplied);


	blendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateSrcAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateWriteAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateWriteAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MIN;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateWriteMinAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateWriteMinAlpha);

	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateWriteMaxAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateWriteMaxAlpha);


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
	SET_DXGI_DEBUGNAME(m_blendStatePrepassUseAppAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassUseInvertedAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStatePrepassUseInvertedAppAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassIgnoreAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStatePrepassIgnoreAppAlpha);

	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassZeroAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStatePrepassZeroAppAlpha);


	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassBlendAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStatePrepassBlendAppAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassBlendInverseAppAlpha.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStatePrepassBlendInverseAppAlpha);

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_BLEND_FACTOR;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateWriteFactored.GetAddressOf())))
	{
		ErrorLog("CreateBlendState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_blendStateWriteFactored);


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
	SET_DXGI_DEBUGNAME(m_rasterizerState);

	rasterizerDesc.FrontCounterClockwise = true;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerStateMirrored.GetAddressOf())))
	{
		ErrorLog("CreateRasterizerState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_rasterizerStateMirrored);

	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 16;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerStateDepthBias.GetAddressOf())))
	{
		ErrorLog("CreateRasterizerState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_rasterizerStateDepthBias);

	rasterizerDesc.FrontCounterClockwise = true;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerStateDepthBiasMirrored.GetAddressOf())))
	{
		ErrorLog("CreateRasterizerState failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_rasterizerStateDepthBiasMirrored);

	D3D11_INPUT_ELEMENT_DESC vertexDesc{};
	vertexDesc.SemanticName = "POSITION";
	vertexDesc.SemanticIndex = 0;
	vertexDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexDesc.InputSlot = 0;
	vertexDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexDesc.InstanceDataStepRate = 0;

	if (FAILED(m_d3dDevice->CreateInputLayout(&vertexDesc, 1, g_PassthroughVS, sizeof(g_PassthroughVS), &m_inputLayout)))
	{
		ErrorLog("CreateInputLayout failure!\n");
		return false;
	}
	SET_DXGI_DEBUGNAME(m_inputLayout);

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

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_debugTexture.Texture)))
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

	if (FAILED(m_d3dDevice->CreateShaderResourceView(m_debugTexture.Texture.Get(), &srvDesc, &m_debugTexture.SRV)))
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
	SET_DXGI_DEBUGNAME(m_cameraFrameUploadTexture);

	DX11FrameData& frameData = m_frameData[imageIndex];

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.cameraFrame.Texture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}
	if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.cameraFrame.Texture.Get(), &srvDesc, &frameData.cameraFrame.SRV)))
	{
		ErrorLog("Frame Resource CreateShaderResourceView error!\n");
		return;
	}
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
	SET_DXGI_DEBUGNAME(m_cameraUndistortedFrameUploadTexture);

	DX11FrameData& frameData = m_frameData[imageIndex];

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.cameraUndistortedFrame.Texture)))
	{
		ErrorLog("Frame Resource CreateTexture2D error!\n");
		return;
	}
	if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.cameraUndistortedFrame.Texture.Get(), &srvDesc, &frameData.cameraUndistortedFrame.SRV)))
	{
		ErrorLog("Frame Resource CreateShaderResourceView error!\n");
		return;
	}
}


void PassthroughRendererDX11::SetupDisparityMap(uint32_t width, uint32_t height)
{
	// TODO: merge textures to one, we shouldn't need a separtate one for temporal filtering
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16_SNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
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
	SET_DXGI_DEBUGNAME(m_disparityMapUploadTexture);


	for (int i = 0; i < m_frameData.size(); i++)
	{
		DX11FrameData& frameData = m_frameData[i];

		if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &frameData.disparityMap.Texture)))
		{
			ErrorLog("Disparity Map CreateTexture2D error!\n");
			return;
		}

		if (FAILED(m_d3dDevice->CreateUnorderedAccessView(frameData.disparityMap.Texture.Get(), &uavDesc, &frameData.disparityMap.UAV)))
		{
			ErrorLog("Disparity Map CreateUnorderedAccessView error!\n");
		}

		if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.disparityMap.Texture.Get(), &srvDesc, &frameData.disparityMap.SRV)))
		{
			ErrorLog("Disparity Map CreateShaderResourceView error!\n");
			return;
		}

		if (m_configManager->GetConfig_Stereo().StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported)
		{

			if (FAILED(m_d3dDevice->CreateTexture2D(&uavTextureDesc, nullptr, &frameData.disparityFilter.Texture)))
			{
				ErrorLog("Disparity Map UAV CreateTexture2D error!\n");
			}

			if (FAILED(m_d3dDevice->CreateUnorderedAccessView(frameData.disparityFilter.Texture.Get(), &uavDesc, &frameData.disparityFilter.UAV)))
			{
				ErrorLog("Disparity Map CreateUnorderedAccessView error!\n");
			}

			if (FAILED(m_d3dDevice->CreateShaderResourceView(frameData.disparityFilter.Texture.Get(), &srvDesc, &frameData.disparityFilter.SRV)))
			{
				ErrorLog("Disparity Map CreateShaderResourceView error!\n");
			}
		}
	}
}


void PassthroughRendererDX11::SetupPassthroughDepthStencil(uint32_t viewIndex, uint32_t swapchainIndex, uint32_t width, uint32_t height)
{
	{
		D3D11_TEXTURE2D_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R16_TYPELESS;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.ArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.CPUAccessFlags = 0;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = DXGI_FORMAT_R16_UNORM;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = DXGI_FORMAT_D16_UNORM;
		dsvDesc.Texture2D.MipSlice = 0;

		DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

		for (int i = 0; i < 2; i++)
		{
			viewData.passthroughDepthStencil[i].Width = width;
			viewData.passthroughDepthStencil[i].Height = height;

			if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &viewData.passthroughDepthStencil[i].Texture)))
			{
				ErrorLog("Passthrough Depth Stencil CreateTexture2D error!\n");
				return;
			}

			if (FAILED(m_d3dDevice->CreateShaderResourceView(viewData.passthroughDepthStencil[i].Texture.Get(), &srvDesc, &viewData.passthroughDepthStencil[i].SRV)))
			{
				ErrorLog("Passthrough Depth Stencil CreateShaderResourceView error!\n");
				return;
			}

			if (FAILED(m_d3dDevice->CreateDepthStencilView(viewData.passthroughDepthStencil[i].Texture.Get(), &dsvDesc, &viewData.passthroughDepthStencil[i].DSV)))
			{
				ErrorLog("Passthrough Depth Stencil CreateDepthStencilView error!\n");
			}
		}
	}

	{
		D3D11_TEXTURE2D_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.ArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.CPUAccessFlags = 0;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = textureDesc.Format;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = textureDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

		DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

		if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &viewData.passthroughCameraValidity.Texture)))
		{
			ErrorLog("Passthrough Camera Invalidation CreateTexture2D error!\n");
			return;
		}

		if (FAILED(m_d3dDevice->CreateShaderResourceView(viewData.passthroughCameraValidity.Texture.Get(), &srvDesc, &viewData.passthroughCameraValidity.SRV)))
		{
			ErrorLog("Passthrough Camera Invalidation CreateShaderResourceView error!\n");
			return;
		}

		if (FAILED(m_d3dDevice->CreateRenderTargetView(viewData.passthroughCameraValidity.Texture.Get(), &rtvDesc, &viewData.passthroughCameraValidity.RTV)))
		{
			ErrorLog("Passthrough Camera Invalidation CreateRenderTargetView error!\n");
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

	D3D11_SUBRESOURCE_DATA uploadData = {};
	uploadData.pSysMem = (void*)uvDistortionMap->data();
	uploadData.SysMemPitch = m_cameraTextureWidth * 8; // 2 * 32 bits

	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, &uploadData, &m_uvDistortionMap.Texture)))
	{
		ErrorLog("UV Distortion Map CreateTexture2D error!\n");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(m_uvDistortionMap.Texture.Get(), &srvDesc, &m_uvDistortionMap.SRV)))
	{
		ErrorLog("UV Distortion Map CreateShaderResourceView error!\n");
		return;
	}
}


DX11TemporaryRenderTarget& PassthroughRendererDX11::GetTemporaryRenderTarget(const uint32_t swapchainIndex, const uint32_t eyeIndex)
{
	DX11ViewData& viewData = m_viewData[eyeIndex][swapchainIndex];

	assert(viewData.renderTargets.Get());

	if (viewData.temporaryRenderTarget.AssociatedRenderTarget == viewData.renderTarget.Texture.Get())
	{
		return viewData.temporaryRenderTarget;
	}

	viewData.temporaryRenderTarget.AssociatedRenderTarget = viewData.renderTarget.Texture.Get();

	D3D11_TEXTURE2D_DESC finalRTDesc;
	viewData.renderTarget.Texture.Get()->GetDesc(&finalRTDesc);

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
	

	if (viewData.renderTarget.Texture.Get() == (ID3D11Texture2D*)rendertarget)
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

	if (FAILED(m_d3dDevice->CreateRenderTargetView((ID3D11Texture2D*)rendertarget, &rtvDesc, viewData.renderTarget.RTV.GetAddressOf())))
	{
		ErrorLog("Render Target CreateRenderTargetView error!\n");
		return;
	}

	viewData.renderTarget.Texture = (ID3D11Texture2D*)rendertarget;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	//srvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	//srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	//srvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateShaderResourceView((ID3D11Resource*)rendertarget, &srvDesc, &viewData.renderTarget.SRV)))
	{
		ErrorLog("Render Target CreateShaderResourceView error!\n");
		return;
	}

	if (!m_configManager->GetConfig_Main().EnableTemporalFiltering && m_cameraFilter[viewIndex][0].SRV != nullptr)
	{
		// Free the UAV resources so that they will be recreated with the correct size in case it changed while temporal filtering was turned off.
		m_cameraFilter[viewIndex][0].SRV.Reset();
		m_cameraFilter[viewIndex][0].UAV.Reset();
		m_cameraFilter[viewIndex][0].Texture.Reset();

		m_cameraFilter[viewIndex][1].SRV.Reset();
		m_cameraFilter[viewIndex][1].UAV.Reset();
		m_cameraFilter[viewIndex][1].Texture.Reset();
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

	bufferDesc.ByteWidth = Align(sizeof(CSConstantBuffer), 16);
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &frameData.csConstantBuffer)))
	{
		ErrorLog("m_csConstantBuffer creation failure!\n");
		return false;
	}

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



void PassthroughRendererDX11::SetupTemporalUAV(const uint32_t viewIndex, const uint32_t frameIndex, const uint32_t width, const uint32_t height)
{
	D3D11_TEXTURE2D_DESC uavTextureDesc = {};
	uavTextureDesc.MipLevels = 1;
	uavTextureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	uavTextureDesc.Width = width;
	uavTextureDesc.Height = height;
	uavTextureDesc.ArraySize = 1;
	uavTextureDesc.SampleDesc.Count = 1;
	uavTextureDesc.SampleDesc.Quality = 0;
	uavTextureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	uavTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	uavTextureDesc.CPUAccessFlags = 0;

	if (FAILED(m_d3dDevice->CreateTexture2D(&uavTextureDesc, nullptr, &m_cameraFilter[viewIndex][frameIndex].Texture)))
	{
		ErrorLog("UAV CreateTexture2D error!\n");
		return;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	if (FAILED(m_d3dDevice->CreateUnorderedAccessView(m_cameraFilter[viewIndex][frameIndex].Texture.Get(), &uavDesc, &m_cameraFilter[viewIndex][frameIndex].UAV)))
	{
		ErrorLog("UAV CreateUnorderedAccessView error!\n");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC filterSRVDesc = {};
	filterSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	filterSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
	filterSRVDesc.Texture2DArray.MipLevels = 1;
	filterSRVDesc.Texture2DArray.ArraySize = 1;

	if (FAILED(m_d3dDevice->CreateShaderResourceView(m_cameraFilter[viewIndex][frameIndex].Texture.Get(), &filterSRVDesc, &m_cameraFilter[viewIndex][frameIndex].SRV)))
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

	ID3D11Resource* bufferRes = static_cast<ID3D11Resource*>(depthBuffer);

	DX11ViewDepthData& depthData = m_viewDepthData[viewIndex][imageIndex];
	if (depthData.depthStencil.Get() == bufferRes)
	{
		return;
	}

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = static_cast<DXGI_FORMAT>(swapchainInfo.format);
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	if (FAILED(m_d3dDevice->CreateDepthStencilView(bufferRes, &dsvDesc, depthData.depthStencilView.GetAddressOf())))
	{
		ErrorLog("Depth map CreateDepthStencilView error!\n");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	switch (dsvDesc.Format)
	{
	case DXGI_FORMAT_D16_UNORM:
		srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		break;

	case DXGI_FORMAT_D32_FLOAT:
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		break;

	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		break;

	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		break;

	}

	if (FAILED(m_d3dDevice->CreateShaderResourceView(bufferRes, &srvDesc, depthData.depthSRV.GetAddressOf())))
	{
		ErrorLog("Depth map CreateShaderResourceView error!\n");
		return;
	}

	depthData.depthStencil = bufferRes;
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
	SET_DXGI_DEBUGNAME(m_cylinderMeshVertexBuffer);

	D3D11_SUBRESOURCE_DATA indexBufferData{};
	indexBufferData.pSysMem = m_cylinderMesh.triangles.data();

	CD3D11_BUFFER_DESC indexBufferDesc((UINT)m_cylinderMesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_cylinderMeshIndexBuffer)))
	{
		ErrorLog("Mesh index buffer creation error!\n");
		return;
	}
	SET_DXGI_DEBUGNAME(m_cylinderMeshIndexBuffer);
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
	SET_DXGI_DEBUGNAME(m_gridMeshVertexBuffer);

	D3D11_SUBRESOURCE_DATA indexBufferData{};
	indexBufferData.pSysMem = m_gridMesh.triangles.data();

	CD3D11_BUFFER_DESC indexBufferDesc((UINT)m_gridMesh.triangles.size() * sizeof(MeshTriangle), D3D11_BIND_INDEX_BUFFER);
	if (FAILED(m_d3dDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_gridMeshIndexBuffer)))
	{
		ErrorLog("Depth mesh index buffer creation error!\n");
		return;
	}
	SET_DXGI_DEBUGNAME(m_gridMeshIndexBuffer);
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


void PassthroughRendererDX11::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, FrameRenderParameters& renderParams, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams)
{
	m_prevFrameIndex = m_frameIndex;
	//Relying on the application not doing anything too weird with the swaphain indices
	m_frameIndex = renderParams.LeftFrameIndex;

	if (frame->bIsFirstRender) { m_currentCameraFilterIndex = m_currentCameraFilterIndex == 0 ? 1 : 0; }

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];

	DX11ViewData& viewDataLeft = m_viewData[0][renderParams.LeftFrameIndex];
	DX11ViewData& viewDataRight = m_viewData[1][renderParams.RightFrameIndex];

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
			(!m_uvDistortionMap.Texture.Get() || m_fovScale != distortionParams.fovScale))
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
		if (m_cameraFilter[0][0].Texture == nullptr) 
		{ 
			// The history filter texture needs to be tha same size as the rendered area.
			XrExtent2Di leftSize = layer->views[0].subImage.imageRect.extent;
			XrExtent2Di rightSize = layer->views[1].subImage.imageRect.extent;

			SetupTemporalUAV(0, 0, leftSize.width, leftSize.height);
			SetupTemporalUAV(0, 1, leftSize.width, leftSize.height);
			SetupTemporalUAV(1, 0, rightSize.width, rightSize.height);
			SetupTemporalUAV(1, 1, rightSize.width, rightSize.height);
		}
	}

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		bool bDisparityMapUpdated = depthFrame->bIsFirstRender;

		bool bDisparityMapReset = false;

		if (depthFrame->disparityTextureSize[0] != m_disparityMapWidth || (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported && frameData.disparityFilter.Texture == nullptr))
		{
			m_disparityMapWidth = depthFrame->disparityTextureSize[0];

			SetupDisparityMap(depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1]);

			for (int i = 0; i < m_viewData[0].size(); i++)
			{
				m_viewData[0][i].passthroughDepthStencil[0].Texture = nullptr;
				m_viewData[0][i].passthroughDepthStencil[1].Texture = nullptr;
				m_viewData[1][i].passthroughDepthStencil[0].Texture = nullptr;
				m_viewData[0][i].passthroughDepthStencil[1].Texture = nullptr;
			}
			bDisparityMapUpdated = true;
			bDisparityMapReset = true;
		}
		if (bDisparityMapUpdated)
		{
			UploadTexture(m_deviceContext, m_disparityMapUploadTexture, (uint8_t*)depthFrame->disparityMap->data(), depthFrame->disparityTextureSize[1], depthFrame->disparityTextureSize[0] * sizeof(uint16_t) * 2);
			m_prevDepthUpdatedFrameIndex = m_depthUpdatedFrameIndex;
			m_depthUpdatedFrameIndex = m_frameIndex;
			m_deviceContext->CopyResource(frameData.disparityMap.Texture.Get(), m_disparityMapUploadTexture.Get());
		}
		else
		{
			m_deviceContext->CopyResource(frameData.disparityMap.Texture.Get(), prevFrameData.disparityMap.Texture.Get());
		}

		if (stereoConf.StereoFillHoles && bDisparityMapUpdated)
		{
			RenderHoleFillCS(frameData, depthFrame);
		}

		bool bDepthMapsReset = false;

		if (bUseDepthPass)
		{
			if(viewDataLeft.passthroughDepthStencil[0].Texture == nullptr || 
				viewDataRight.passthroughDepthStencil[0].Texture == nullptr ||
				(viewDataLeft.passthroughDepthStencil[0].Width != depthFrame->disparityTextureSize[0] * stereoConf.StereoDepthMapScale / 2) ||
				(viewDataLeft.passthroughDepthStencil[0].Height != depthFrame->disparityTextureSize[1] * stereoConf.StereoDepthMapScale))
			{
				uint32_t width = depthFrame->disparityTextureSize[0] * stereoConf.StereoDepthMapScale / 2;
				uint32_t height = depthFrame->disparityTextureSize[1] * stereoConf.StereoDepthMapScale;

				SetupPassthroughDepthStencil(0, renderParams.LeftFrameIndex, width, height);
				SetupPassthroughDepthStencil(1, renderParams.RightFrameIndex, width, height);

				bDepthMapsReset = true;
			}
		}

		if (m_bUseHexagonGridMesh != stereoConf.StereoUseHexagonGridMesh || bDisparityMapReset || bDepthMapsReset)
		{
			m_bUseHexagonGridMesh = stereoConf.StereoUseHexagonGridMesh;

			GenerateDepthMesh(depthFrame->disparityTextureSize[0] * stereoConf.StereoDepthMapScale / 2, depthFrame->disparityTextureSize[1] * stereoConf.StereoDepthMapScale);
		}
	}

	VSPassConstantBuffer vsBuffer{};

	vsBuffer.worldToCameraFrameProjectionLeft = frame->worldToCameraProjectionLeft;
	vsBuffer.worldToCameraFrameProjectionRight = frame->worldToCameraProjectionRight;
	vsBuffer.worldToPrevCameraFrameProjectionLeft = frame->prevWorldToCameraProjectionLeft;
	vsBuffer.worldToPrevCameraFrameProjectionRight = frame->prevWorldToCameraProjectionRight;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		vsBuffer.worldToPrevDepthFrameProjectionLeft = depthFrame->prevDispWorldToCameraProjectionLeft;
		vsBuffer.worldToPrevDepthFrameProjectionRight = depthFrame->prevDispWorldToCameraProjectionRight;
		vsBuffer.depthFrameViewToWorldLeft = depthFrame->disparityViewToWorldLeft;
		vsBuffer.depthFrameViewToWorldRight = depthFrame->disparityViewToWorldRight;
		vsBuffer.prevDepthFrameViewToWorldLeft = depthFrame->prevDisparityViewToWorldLeft;
		vsBuffer.prevDepthFrameViewToWorldRight = depthFrame->prevDisparityViewToWorldRight;
		vsBuffer.disparityToDepth = depthFrame->disparityToDepth;
		vsBuffer.disparityDownscaleFactor = depthFrame->disparityDownscaleFactor;
		vsBuffer.disparityTextureSize[0] = depthFrame->disparityTextureSize[0];
		vsBuffer.disparityTextureSize[1] = depthFrame->disparityTextureSize[1];
		vsBuffer.minDisparity = depthFrame->minDisparity;
		vsBuffer.maxDisparity = depthFrame->maxDisparity;
		vsBuffer.cutoutFactor = stereoConf.StereoCutoutFactor;
		vsBuffer.cutoutOffset = stereoConf.StereoCutoutOffset;
		vsBuffer.cutoutFilterWidth = stereoConf.StereoCutoutFilterWidth;
		vsBuffer.disparityFilterWidth = stereoConf.StereoDisparityFilterWidth;
		vsBuffer.disparityFilterConfidenceCutout = stereoConf.StereoDisparityFilterConfidenceCutout;
		vsBuffer.bProjectBorders = !stereoConf.StereoReconstructionFreeze;
		vsBuffer.bFindDiscontinuities = stereoConf.StereoCutoutEnabled || stereoConf.StereoUseSeparateDepthPass;
		vsBuffer.bUseDisparityTemporalFilter = stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported;
		vsBuffer.bBlendDepthMaps = stereoConf.StereoCutoutEnabled;
		vsBuffer.disparityTemporalFilterStrength = stereoConf.StereoDisparityTemporalFilteringStrength;
		vsBuffer.disparityTemporalFilterDistance = stereoConf.StereoDisparityTemporalFilteringDistance;
		vsBuffer.depthContourStrength = stereoConf.StereoDepthContourStrength;
		vsBuffer.depthContourTreshold = stereoConf.StereoDepthContourThreshold;
	}

	m_renderContext->UpdateSubresource(frameData.vsPassConstantBuffer.Get(), 0, nullptr, &vsBuffer, 0, 0);


	if (mainConf.DebugSource == DebugSource_ApplicationAlpha || mainConf.DebugSource == DebugSource_ApplicationDepth)
	{
		RenderDebugView(LEFT_EYE, layer, renderParams);
		RenderDebugView(RIGHT_EYE, layer, renderParams);

		RenderFrameFinish();

		m_prevSwapchainLeft = renderParams.LeftFrameIndex;
		m_prevSwapchainRight = renderParams.RightFrameIndex;

		return;
	}


	ID3D11ShaderResourceView* psSRVs[2] = {0};
	psSRVs[1] = m_uvDistortionMap.SRV.Get();
	bool bGotDebugTexture = false;

	if (mainConf.DebugTexture != DebugTexture_None)
	{
		DebugTexture& texture = m_configManager->GetDebugTexture();
		std::lock_guard<std::mutex> readlock(texture.RWMutex);

		if (texture.CurrentTexture == mainConf.DebugTexture)
		{
			if (!m_debugTexture.SRV.Get() || texture.CurrentTexture != m_selectedDebugTexture || texture.bDimensionsUpdated)
			{
				SetupDebugTexture(texture);

				m_selectedDebugTexture = texture.CurrentTexture;
				texture.bDimensionsUpdated = false;
			}

			if (m_debugTextureUpload.Get())
			{
				UploadTexture(m_deviceContext, m_debugTextureUpload, texture.Texture.data(), texture.Height, texture.Width * texture.PixelSize);

				m_deviceContext->CopyResource(m_debugTexture.Texture.Get(), m_debugTextureUpload.Get());

				psSRVs[0] = m_debugTexture.SRV.Get();
				bGotDebugTexture = true;
			}
		}	
	}

	if (!bGotDebugTexture && frame->frameTextureResource != nullptr)
	{
		// Use shared texture
		psSRVs[0] = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else if(!bGotDebugTexture && frame->frameBuffer.get() != nullptr && frame->frameBuffer->size() > 0)
	{
		// Upload camera frame from CPU

		if (frame->header.eFrameType == vr::VRTrackedCameraFrameType_Distorted)
		{
			UploadTexture(m_deviceContext, m_cameraFrameUploadTexture, (uint8_t*)frame->frameBuffer->data(), m_cameraTextureHeight, m_cameraTextureWidth * 4);

			m_deviceContext->CopyResource(frameData.cameraFrame.Texture.Get(), m_cameraFrameUploadTexture.Get());

			psSRVs[0] = frameData.cameraFrame.SRV.Get();
		}
		else
		{
			UploadTexture(m_deviceContext, m_cameraUndistortedFrameUploadTexture, (uint8_t*)frame->frameBuffer->data(), m_cameraUndistortedTextureHeight, m_cameraUndistortedTextureWidth * 4);

			m_deviceContext->CopyResource(frameData.cameraUndistortedFrame.Texture.Get(), m_cameraUndistortedFrameUploadTexture.Get());

			psSRVs[0] = frameData.cameraUndistortedFrame.SRV.Get();
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

	m_renderContext->RSSetState(frame->bIsRenderingMirrored ? m_rasterizerStateMirrored.Get() : m_rasterizerState.Get());

	m_renderContext->VSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());
	m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	UINT numIndices;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		numIndices = (UINT)m_gridMesh.triangles.size() * 3;
	}
	else
	{
		numIndices = (UINT)m_cylinderMesh.triangles.size() * 3;
	}

	PSPassConstantBuffer psPassBuffer = {};
	psPassBuffer.worldToCameraFrameProjectionLeft = frame->worldToCameraProjectionLeft;
	psPassBuffer.worldToCameraFrameProjectionRight = frame->worldToCameraProjectionRight;
	psPassBuffer.worldToPrevCameraFrameProjectionLeft = frame->prevWorldToCameraProjectionLeft;
	psPassBuffer.worldToPrevCameraFrameProjectionRight = frame->prevWorldToCameraProjectionRight;

	psPassBuffer.depthRange = XrVector2f(NEAR_PROJECTION_DISTANCE, mainConf.ProjectionDistanceFar);
	psPassBuffer.depthCutoffRange = XrVector2f(renderParams.DepthRangeMin, renderParams.DepthRangeMax);
	psPassBuffer.opacity = mainConf.PassthroughOpacity;
	psPassBuffer.brightness = mainConf.Brightness;
	psPassBuffer.contrast = mainConf.Contrast;
	psPassBuffer.saturation = mainConf.Saturation;
	psPassBuffer.sharpness = mainConf.Sharpness;
	psPassBuffer.temporalFilteringSampling = mainConf.TemporalFilteringSampling;
	psPassBuffer.temporalFilteringFactor = mainConf.TemporalFilteringFactor;
	psPassBuffer.temporalFilteringColorRangeCutoff = mainConf.TemporalFilteringRejectionOffset;
	psPassBuffer.cutoutCombineFactor = stereoConf.StereoCutoutCombineFactor;
	psPassBuffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;
	psPassBuffer.depthTemporalFilterFactor = stereoConf.StereoDisparityTemporalFilteringStrength;
	psPassBuffer.depthTemporalFilterDistance = stereoConf.StereoDisparityTemporalFilteringDistance;
	psPassBuffer.bDebugDepth = mainConf.DebugSource == DebugSource_OutputDepth;
	psPassBuffer.debugOverlay = mainConf.DebugOverlay;
	psPassBuffer.bUseFisheyeCorrection = mainConf.ProjectionMode != Projection_RoomView2D;
	psPassBuffer.bIsFirstRenderOfCameraFrame = frame->bIsFirstRender;
	psPassBuffer.bUseDepthCutoffRange = renderParams.bEnableDepthRange;
	psPassBuffer.bClampCameraFrame = m_configManager->GetConfig_Camera().ClampCameraFrame;
	psPassBuffer.depthContourStrength = stereoConf.StereoDepthFullscreenContourStrength;
	psPassBuffer.depthContourTreshold = stereoConf.StereoDepthFullscreenContourThreshold;
	psPassBuffer.depthContourFilterWidth = stereoConf.StereoDepthFullscreenContourFilterWidth;
	psPassBuffer.bIsCutoutEnabled = stereoConf.StereoCutoutEnabled;
	psPassBuffer.bIsAppAlphaInverted = renderParams.bInvertLayerAlpha;
	psPassBuffer.bHasReversedDepth = frame->bHasReversedDepth;

	m_renderContext->UpdateSubresource(frameData.psPassConstantBuffer.Get(), 0, nullptr, &psPassBuffer, 0, 0);


	if (renderParams.BlendMode == Masked)
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
	}

	bool bRenderBackground = stereoConf.StereoDrawBackground && mainConf.ProjectionMode == Projection_StereoReconstruction && !stereoConf.StereoReconstructionFreeze && !renderParams.bEnableDepthRange && !m_configManager->GetConfig_Camera().ClampCameraFrame;

	bool bRenderAlphaPrepass =
		(renderParams.bInvertLayerAlpha && (!bUseDepthPass || renderParams.BlendMode == Masked)) || // Use prepass for inverting alpha channel
		(renderParams.BlendMode == Opaque && !renderParams.bEnableDepthBlending) || // Use prepass for filling in alpha channel
		(renderParams.BlendMode == Additive) || // Use prepass for filling in alpha channel
		(renderParams.BlendMode != Masked && renderParams.bEnableDepthBlending && !bUseDepthPass); // Use prepass for cutting out depth


	// Render left eye
	if (renderParams.LeftFrameIndex >= 0)
	{
		if (bUseDepthPass)
		{
			RenderDepthPrepassView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		if (renderParams.BlendMode == Masked)
		{
			RenderMaskedPrepassView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		RenderSetupView(LEFT_EYE, layer, frame, depthFrame, renderParams);

		if (bRenderAlphaPrepass)
		{
			RenderAlphaPrepassView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		if (mainConf.ProjectToRenderModels)
		{
			RenderViewModelsForView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		RenderPassthroughView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);

		if (bRenderBackground)
		{
			RenderBackgroundForView(LEFT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}
	}

	// Render right eye
	if (renderParams.RightFrameIndex >= 0)
	{
		if (bUseDepthPass)
		{
			RenderDepthPrepassView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		if (renderParams.BlendMode == Masked)
		{
			RenderMaskedPrepassView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		RenderSetupView(RIGHT_EYE, layer, frame, depthFrame, renderParams);

		if (bRenderAlphaPrepass)
		{
			RenderAlphaPrepassView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		if (mainConf.ProjectToRenderModels)
		{
			RenderViewModelsForView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}

		RenderPassthroughView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);

		if (bRenderBackground)
		{
			RenderBackgroundForView(RIGHT_EYE, layer, frame, depthFrame, numIndices, renderParams);
		}
	}

	RenderFrameFinish();

	m_prevSwapchainLeft = renderParams.LeftFrameIndex;
	m_prevSwapchainRight = renderParams.RightFrameIndex;
}





// Compute shader that fills up holes in the disparity map.
void PassthroughRendererDX11::RenderHoleFillCS(DX11FrameData& frameData, std::shared_ptr<DepthFrame> depthFrame)
{
	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	m_renderContext->CSSetUnorderedAccessViews(0, 1, frameData.disparityMap.UAV.GetAddressOf(), nullptr);
	m_renderContext->CSSetShader(m_fillHolesCS.Get(), nullptr, 0);
	m_renderContext->CSSetConstantBuffers(0, 1, frameData.csConstantBuffer.GetAddressOf());

	D3D11_TEXTURE2D_DESC dispDesc;
	frameData.disparityMap.Texture->GetDesc(&dispDesc);

	CSConstantBuffer csBuffer = {};
	csBuffer.disparityFrameWidth = dispDesc.Width / 2;
	csBuffer.bHoleFillLastPass = false;
	csBuffer.minDisparity = depthFrame->minDisparity;
	csBuffer.maxDisparity = depthFrame->maxDisparity;

	m_renderContext->UpdateSubresource(frameData.csConstantBuffer.Get(), 0, nullptr, &csBuffer, 0, 0);

	for (int i = 0; i < 7; i++)
	{
		m_renderContext->Dispatch(dispDesc.Width / 16, dispDesc.Height / 16, 1);
	}

	csBuffer.bHoleFillLastPass = true;
	m_renderContext->UpdateSubresource(frameData.csConstantBuffer.Get(), 0, nullptr, &csBuffer, 0, 0);
	m_renderContext->Dispatch(dispDesc.Width / 16, dispDesc.Height / 16, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	m_renderContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

}



void PassthroughRendererDX11::RenderSetupView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending &&
		m_viewDepthData[viewIndex].size() > depthSwapchainIndex &&
		m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get() != nullptr;

	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;
	bool bUseFullscreenPass = bUseDepthPass && stereoConf.StereoUseFullscreenPass;

	bool bUseDisparityTemporalFiltering = stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSViewConstantBuffer vsViewBuffer = {};

	vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	XrMatrix4x4f_Invert(&vsViewBuffer.HMDProjectionToWorld, &vsViewBuffer.worldToHMDProjection);
	vsViewBuffer.prevHMDFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevHMDFrame_WorldToHMDProjectionLeft : frame->prevHMDFrame_WorldToHMDProjectionRight;
	vsViewBuffer.prevCameraFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevCameraFrame_WorldToHMDProjectionLeft : frame->prevCameraFrame_WorldToHMDProjectionRight;

	vsViewBuffer.disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
	vsViewBuffer.projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
	vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer.cameraBlendWeight = 1.0;
	vsViewBuffer.cameraViewIndex = viewIndex;
	vsViewBuffer.bWriteDisparityFilter = false;

	m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);

	ID3D11Buffer* vsBuffers[2] = { viewData.vsViewConstantBuffer.Get(), frameData.vsPassConstantBuffer.Get() };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);


	PSViewConstantBuffer psViewBuffer = {};

	psViewBuffer.worldToHMDProjection = vsViewBuffer.worldToHMDProjection;
	psViewBuffer.HMDProjectionToWorld = vsViewBuffer.HMDProjectionToWorld;
	psViewBuffer.prevHMDFrame_WorldToHMDProjection = vsViewBuffer.prevHMDFrame_WorldToHMDProjection;
	psViewBuffer.prevCameraFrame_WorldToHMDProjection = vsViewBuffer.prevCameraFrame_WorldToHMDProjection;

	psViewBuffer.prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	psViewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer.crossUVBounds = GetFrameUVBounds((eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE), frame->frameLayout);
	psViewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer.cameraViewIndex = viewIndex;
	psViewBuffer.bDoCutout = stereoConf.StereoCutoutEnabled && !bUseDepthPass;
	psViewBuffer.bPremultiplyAlpha = (renderParams.BlendMode == AlphaBlendPremultiplied) && !bCompositeDepth;
	psViewBuffer.bUseFullscreenQuad = bUseFullscreenPass;

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[3] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get(), frameData.psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
	else
	{
		m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_renderContext->VSSetShader(m_passthroughVS.Get(), nullptr, 0);
	}
}



// Reprojects one or two disparity maps into HMD projection space depth maps.
void PassthroughRendererDX11::RenderDepthPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)viewData.passthroughDepthStencil[0].Width, (float)viewData.passthroughDepthStencil[0].Height, 0.0f, 1.0f };
	D3D11_RECT scissor = { 0, 0, (long)viewData.passthroughDepthStencil[0].Width, (long)viewData.passthroughDepthStencil[0].Height };
	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	ID3D11Buffer* vsBuffers[2] = { viewData.vsViewConstantBuffer.Get(), frameData.vsPassConstantBuffer.Get() };
	m_renderContext->VSSetConstantBuffers(0, 2, vsBuffers);

	ID3D11ShaderResourceView* vsSRVs[1] = { m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get() };
	m_renderContext->VSSetShaderResources(0, 1, vsSRVs);

	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };

	m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
	m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_renderContext->VSSetShader(m_passthroughStereoVS.Get(), nullptr, 0);

	m_renderContext->ClearDepthStencilView(viewData.passthroughDepthStencil[0].DSV.Get(), D3D11_CLEAR_DEPTH, frame->bHasReversedDepth ? 0.0f : 1.0f, 0);

	m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, viewData.passthroughCameraValidity.RTV.GetAddressOf(), viewData.passthroughDepthStencil[0].DSV.Get(), 0, 0, nullptr, nullptr);

	float blendFactor[4] = { 1,0,1,0 };
	m_renderContext->OMSetBlendState(m_blendStateWriteFactored.Get(), blendFactor, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(frame->bHasReversedDepth ? m_depthStencilStateGreaterWrite.Get() : m_depthStencilStateLessWrite.Get(), 1);

	ID3D11ShaderResourceView* oldPSSRVs[2];
	m_renderContext->PSGetShaderResources(0, 2, oldPSSRVs);

	ID3D11Buffer* psBuffers[2] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	if (stereoConf.StereoUseDisparityTemporalFiltering)
	{
		int prevSwapchain = (eye == LEFT_EYE) ? m_prevSwapchainLeft : m_prevSwapchainRight;
		ID3D11ShaderResourceView* psSRVs[2] = {
			m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[0].SRV.Get(),
			m_viewData[viewIndex][prevSwapchain].passthroughCameraValidity.SRV.Get()
		};
		m_renderContext->PSSetShaderResources(0, 2, psSRVs);

		m_renderContext->PSSetShader(m_depthWriteTemporalPS.Get(), nullptr, 0);
	}
	else
	{
		m_renderContext->PSSetShader(m_depthWritePS.Get(), nullptr, 0);
	}

	VSViewConstantBuffer vsViewBuffer = {};

	vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	XrMatrix4x4f_Invert(&vsViewBuffer.HMDProjectionToWorld, &vsViewBuffer.worldToHMDProjection);
	vsViewBuffer.prevHMDFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevHMDFrame_WorldToHMDProjectionLeft : frame->prevHMDFrame_WorldToHMDProjectionRight;
	vsViewBuffer.prevCameraFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevCameraFrame_WorldToHMDProjectionLeft : frame->prevCameraFrame_WorldToHMDProjectionRight;

	vsViewBuffer.projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
	vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer.cameraViewIndex = viewIndex;
	vsViewBuffer.cameraBlendWeight = 1.0;
	vsViewBuffer.disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);

	m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);

	PSViewConstantBuffer psViewBuffer = {};
	psViewBuffer.prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	psViewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer.bPremultiplyAlpha = false;
	psViewBuffer.bDoCutout = false;

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);


	m_renderContext->DrawIndexed(numIndices, 0, 0);


	if (stereoConf.StereoCutoutEnabled)
	{
		vsViewBuffer.cameraViewIndex = (eye != LEFT_EYE) ? 0 : 1;
		vsViewBuffer.disparityUVBounds = GetFrameUVBounds((eye == LEFT_EYE) ? RIGHT_EYE : LEFT_EYE, StereoHorizontalLayout);
		vsViewBuffer.cameraBlendWeight = stereoConf.StereoCutoutSecondaryCameraWeight;

		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);

		psViewBuffer.bDoCutout = true;

		m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

		if (stereoConf.StereoUseDisparityTemporalFiltering)
		{
			int prevSwapchain = (eye == LEFT_EYE) ? m_prevSwapchainLeft : m_prevSwapchainRight;
			ID3D11ShaderResourceView* psSRVs[2] = {
				m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[1].SRV.Get(),
				m_viewData[viewIndex][prevSwapchain].passthroughCameraValidity.SRV.Get()
			};
			m_renderContext->PSSetShaderResources(0, 2, psSRVs);
		}

		m_renderContext->ClearDepthStencilView(viewData.passthroughDepthStencil[1].DSV.Get(), D3D11_CLEAR_DEPTH, frame->bHasReversedDepth ? 0.0f : 1.0f, 0);

		m_renderContext->OMSetRenderTargets(1, viewData.passthroughCameraValidity.RTV.GetAddressOf(), viewData.passthroughDepthStencil[1].DSV.Get());

		blendFactor[0] = 0;
		blendFactor[1] = 1;
		blendFactor[2] = 0;
		blendFactor[3] = 1;
		m_renderContext->OMSetBlendState(m_blendStateWriteFactored.Get(), blendFactor, UINT_MAX);


		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}

	m_renderContext->PSSetShaderResources(0, 2, oldPSSRVs);
}



// Renders chroma key cutout into the rendertarget alpha channel.
void PassthroughRendererDX11::RenderMaskedPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	if (swapchainIndex < 0) { return; }

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;
	bool bUseFullscreenPass = bUseDepthPass && stereoConf.StereoUseFullscreenPass;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSViewConstantBuffer vsViewBuffer = {};
	vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	XrMatrix4x4f_Invert(&vsViewBuffer.HMDProjectionToWorld, &vsViewBuffer.worldToHMDProjection);
	vsViewBuffer.disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
	vsViewBuffer.projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
	vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer.cameraBlendWeight = 1.0;
	vsViewBuffer.cameraViewIndex = viewIndex;

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
	psViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	XrMatrix4x4f_Invert(&psViewBuffer.HMDProjectionToWorld, &psViewBuffer.worldToHMDProjection);
	psViewBuffer.frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer.crossUVBounds = GetFrameUVBounds((eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE), frame->frameLayout);
	psViewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer.cameraViewIndex = viewIndex;
	psViewBuffer.bDoCutout = bUseFullscreenPass && stereoConf.StereoCutoutEnabled;
	psViewBuffer.bPremultiplyAlpha = false;
	psViewBuffer.bUseFullscreenQuad = bUseFullscreenPass;

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);


	DX11TemporaryRenderTarget& tempTarget = GetTemporaryRenderTarget(m_frameIndex, bSingleStereoRenderTarget ? 0 : viewIndex);

	if (eye == LEFT_EYE || !bSingleStereoRenderTarget)
	{
		float clearColor[4] = { m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage ? 1.0f : 0, 0, 0, 0 };
		m_renderContext->ClearRenderTargetView(tempTarget.RTV.Get(), clearColor);
	}

	m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, tempTarget.RTV.GetAddressOf(), depthStencil, 0, 0, nullptr, nullptr);
	m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage == frame->bHasReversedDepth, bWriteDepth), 1);

	if (mainConf.ProjectionMode == Projection_StereoReconstruction && !bUseFullscreenPass)
	{
		if (bUseDepthPass) 
		{
			ID3D11ShaderResourceView* vsSRVs[3] = { viewData.passthroughDepthStencil[0].SRV.Get(), viewData.passthroughDepthStencil[1].SRV.Get(), viewData.passthroughCameraValidity.SRV.Get()};
			m_renderContext->VSSetShaderResources(0, 3, vsSRVs);
		}
		else if (stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported)
		{
			ID3D11ShaderResourceView* vsSRVs[2] = { m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get(), m_frameData[m_prevDepthUpdatedFrameIndex].disparityFilter.SRV.Get() };
			m_renderContext->VSSetShaderResources(0, 2, vsSRVs);
		}
		else
		{
			ID3D11ShaderResourceView* vsSRVs[2] = { m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get() };
			m_renderContext->VSSetShaderResources(0, 1, vsSRVs);
		}
	}

	ID3D11ShaderResourceView* cameraFrameSRV;

	if (mainConf.DebugTexture != DebugTexture_None)
	{
		cameraFrameSRV = m_debugTexture.SRV.Get();
	}
	else if (frame->frameTextureResource != nullptr)
	{
		cameraFrameSRV = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else
	{
		cameraFrameSRV = frameData.cameraFrame.SRV.Get();
	}

	ID3D11ShaderResourceView* prepassSourceTexture;

	if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		prepassSourceTexture = cameraFrameSRV;
	}
	else
	{
		prepassSourceTexture = viewData.renderTarget.SRV.Get();
	}

	if (bUseFullscreenPass)
	{
		ID3D11ShaderResourceView* psSRVs[5];
		m_renderContext->PSGetShaderResources(0, 2, psSRVs);

		psSRVs[0] = prepassSourceTexture;
		psSRVs[1] = m_uvDistortionMap.SRV.Get();
		psSRVs[2] = viewData.passthroughCameraValidity.SRV.Get();
		psSRVs[3] = viewData.passthroughDepthStencil[0].SRV.Get();

		if (stereoConf.StereoCutoutEnabled)
		{
			psSRVs[4] = viewData.passthroughDepthStencil[1].SRV.Get();

			m_renderContext->PSSetShaderResources(0, 5, psSRVs);
		}
		else
		{
			m_renderContext->PSSetShaderResources(0, 4, psSRVs);
		}
	}
	else if (mainConf.ProjectionMode == Projection_RoomView2D)
	{
		m_renderContext->PSSetShaderResources(0, 1, &prepassSourceTexture);
	}
	else
	{
		ID3D11ShaderResourceView* views[2] = { prepassSourceTexture, m_uvDistortionMap.SRV.Get() };
		m_renderContext->PSSetShaderResources(0, 2, views);
	}

	ID3D11Buffer* psBuffers[3] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get(), frameData.psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	m_renderContext->PSSetShader(bUseFullscreenPass ? m_maskedAlphaPrepassFullscreenPS.Get() : m_maskedAlphaPrepassPS.Get(), nullptr, 0);

	// Draw with simple vertex shader if we don't need to sample camera
	if (bUseFullscreenPass || (!bCompositeDepth && !m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage))
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_renderContext->VSSetShader(m_fullscreenQuadVS.Get(), nullptr, 0);
		

		m_renderContext->Draw(3, 0);

	}
	else
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (bUseDepthPass)
		{
			m_renderContext->VSSetShader(m_passthroughReadDepthVS.Get(), nullptr, 0);
		}
		else if (mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported)
		{
			m_renderContext->VSSetShader(m_passthroughStereoTemporalVS.Get(), nullptr, 0);
		}
		else if(mainConf.ProjectionMode == Projection_StereoReconstruction)
		{
			m_renderContext->VSSetShader(m_passthroughStereoVS.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_passthroughVS.Get(), nullptr, 0);
		}


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
		ID3D11ShaderResourceView* views[3] = { cameraFrameSRV, m_uvDistortionMap.SRV.Get(), tempTarget.SRV.Get() };
		m_renderContext->PSSetShaderResources(0, 3, views);
	}

	ID3D11ShaderResourceView* views[3] = { cameraFrameSRV, nullptr, tempTarget.SRV.Get() };

	psViewBuffer.bDoCutout = false;
	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);

	m_renderContext->OMSetRenderTargets(1, &rendertarget, depthStencil);

	if (coreConfig.CoreForceMaskedUseAppAlpha)
	{
		m_renderContext->OMSetBlendState((renderParams.bInvertLayerAlpha) ? m_blendStateWriteMaxAlpha.Get() : m_blendStateWriteMinAlpha.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(m_blendStateWriteAlpha.Get(), nullptr, UINT_MAX);
	}
	
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(false, frame->bHasReversedDepth, false), 1);

	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_renderContext->VSSetShader(m_fullscreenQuadVS.Get(), nullptr, 0);
	m_renderContext->PSSetShader(m_maskedAlphaCopyPS.Get(), nullptr, 0);


	m_renderContext->Draw(3, 0);
}




// Renders an alpha cutout into the rendertarget.
void PassthroughRendererDX11::RenderAlphaPrepassView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;
	bool bUseFullscreenPass = bUseDepthPass && stereoConf.StereoUseFullscreenPass;

	bool bUseDisparityTemporalFiltering = stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported;

	if (bCompositeDepth && depthStencil == nullptr) { return; }


	if (!bUseDepthPass && bUseDisparityTemporalFiltering && mainConf.EnableTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[2] = { m_cameraFilter[viewIndex][m_currentCameraFilterIndex].UAV.Get(), frameData.disparityFilter.UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 2, UAVs, nullptr);
	}
	else if (!bUseDepthPass && bUseDisparityTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[1] = { frameData.disparityFilter.UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 2, 1, UAVs, nullptr);
	}
	else if (mainConf.EnableTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[1] = { m_cameraFilter[viewIndex][m_currentCameraFilterIndex].UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 1, UAVs, nullptr);
	}
	else
	{
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 0, 0, nullptr, nullptr);
	}


	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		if (bUseDepthPass && bUseDisparityTemporalFiltering)
		{
			int prevSwapchain = (eye == LEFT_EYE) ? m_prevSwapchainLeft : m_prevSwapchainRight;
			ID3D11ShaderResourceView* vsSRVs[6] = {
				viewData.passthroughDepthStencil[0].SRV.Get(),
				viewData.passthroughDepthStencil[1].SRV.Get(),
				viewData.passthroughCameraValidity.SRV.Get() ,
				m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[0].SRV.Get(),
				m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[1].SRV.Get(),
				m_viewData[viewIndex][prevSwapchain].passthroughCameraValidity.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 6, vsSRVs);
		}
		else if (bUseDepthPass)
		{
			ID3D11ShaderResourceView* vsSRVs[3] = {
				viewData.passthroughDepthStencil[0].SRV.Get(),
				viewData.passthroughDepthStencil[1].SRV.Get(),
				viewData.passthroughCameraValidity.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 3, vsSRVs);
		}
		else if (bUseDisparityTemporalFiltering)
		{
			ID3D11ShaderResourceView* vsSRVs[2] = {
				m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get(),
				m_frameData[m_prevDepthUpdatedFrameIndex].disparityFilter.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 2, vsSRVs);
		}
		else
		{
			ID3D11ShaderResourceView* vsSRVs[1] = { m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get() };
			m_renderContext->VSSetShaderResources(0, 1, vsSRVs);
		}
	}

	m_renderContext->PSSetShader(m_alphaPrepassPS.Get(), nullptr, 0);


	if (bCompositeDepth && renderParams.BlendMode != Additive && !bUseDepthPass) 
	{
		// Cut out areas in the frame alpha for passthrough.

		if (renderParams.bInvertLayerAlpha)
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassBlendAppAlpha.Get(), nullptr, UINT_MAX);
		}
		else
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassBlendInverseAppAlpha.Get(), nullptr, UINT_MAX);
		}
	}
	else if (renderParams.BlendMode == AlphaBlendPremultiplied || renderParams.BlendMode == AlphaBlendUnpremultiplied || (renderParams.BlendMode == Masked && renderParams.bInvertLayerAlpha))
	{
		if (renderParams.bInvertLayerAlpha) // Invert the alpha channel unless we can write with inverted alpha directly
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassUseInvertedAppAlpha.Get(), nullptr, UINT_MAX);
		}
		else
		{
			m_renderContext->OMSetBlendState(m_blendStatePrepassUseAppAlpha.Get(), nullptr, UINT_MAX);
		}
	}
	else if (renderParams.bInvertLayerAlpha && (renderParams.BlendMode == Additive || renderParams.BlendMode == Opaque) && !bUseDepthPass)
	{
		// Zero out alpha for additive rendering
		m_renderContext->OMSetBlendState(m_blendStatePrepassZeroAppAlpha.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(m_blendStatePrepassIgnoreAppAlpha.Get(), nullptr, UINT_MAX);
	}

	bool bPrepassWriteDepth = !bUseFullscreenPass && bWriteDepth;

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, bPrepassWriteDepth), 1);


	if (bUseFullscreenPass || (renderParams.bInvertLayerAlpha && !bCompositeDepth))
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_renderContext->VSSetShader(m_fullscreenQuadVS.Get(), nullptr, 0);

		m_renderContext->Draw(3, 0);
	}
	else if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		if (bUseDepthPass)
		{

			m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_renderContext->VSSetShader(m_passthroughReadDepthVS.Get(), nullptr, 0);
		}
		else if (bUseDisparityTemporalFiltering)
		{

			m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_renderContext->VSSetShader(m_passthroughStereoTemporalVS.Get(), nullptr, 0);
		}
		else
		{

			m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_renderContext->VSSetShader(m_passthroughStereoVS.Get(), nullptr, 0);
		}

		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}
	else
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_renderContext->VSSetShader(m_passthroughVS.Get(), nullptr, 0);

		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}
	
}




// Renders all SteamVR rendermodels with passthough projected on them.
void PassthroughRendererDX11::RenderViewModelsForView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;
	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;

	m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 0, 0, nullptr, nullptr);

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, bWriteDepth), 1);

	bool bInvertAlpha = renderParams.bInvertLayerAlpha && bUseDepthPass && renderParams.BlendMode != Masked;

	if (renderParams.BlendMode == AlphaBlendPremultiplied || renderParams.BlendMode == Additive)
	{
		m_renderContext->OMSetBlendState(bInvertAlpha ? m_blendStateInvDestAlphaPremultiplied.Get() : m_blendStateDestAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(bInvertAlpha ? m_blendStateInvDestAlpha.Get() : m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);
	}

	m_renderContext->VSSetShader(m_meshRigidVS.Get(), nullptr, 0);

	ID3D11Buffer* vsBuffers[3];
	m_renderContext->VSGetConstantBuffers(0, 2, vsBuffers);

	m_renderContext->PSSetShader(m_passthroughPS.Get(), nullptr, 0);

	for (DX11RenderModel model : m_renderModels)
	{
		VSMeshConstantBuffer vsMeshBuffer = { 0 };
		vsMeshBuffer.meshToWorldTransform = model.meshToWorldTransform;
		m_renderContext->UpdateSubresource(m_vsMeshConstantBuffer[model.deviceId].Get(), 0, nullptr, &vsMeshBuffer, 0, 0);
		vsBuffers[2] = m_vsMeshConstantBuffer[model.deviceId].Get();
		m_renderContext->VSSetConstantBuffers(0, 3, vsBuffers);

		const UINT strides[] = { sizeof(float) * 3 };
		const UINT offsets[] = { 0 };
		m_renderContext->IASetVertexBuffers(0, 1, model.vertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(model.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


		m_renderContext->DrawIndexed(model.numIndices, 0, 0);
	}
}




// Renders the main passthrough view.
void PassthroughRendererDX11::RenderPassthroughView(const ERenderEye eye, const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame, UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;
	bool bUseFullscreenPass = bUseDepthPass && stereoConf.StereoUseFullscreenPass;

	bool bDepthWrittenInPrepass = !bUseFullscreenPass &&
		(renderParams.BlendMode == Opaque && !renderParams.bEnableDepthBlending) ||
		(renderParams.BlendMode == Additive) ||
		(renderParams.BlendMode != Masked && renderParams.bEnableDepthBlending && !bUseDepthPass);

	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication && !bDepthWrittenInPrepass;

	bool bUseDisparityTemporalFiltering = stereoConf.StereoUseDisparityTemporalFiltering && m_bIsVSUAVSupported;

	if (!bUseDepthPass && bUseDisparityTemporalFiltering && mainConf.EnableTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[2] = { m_cameraFilter[viewIndex][m_currentCameraFilterIndex].UAV.Get(), frameData.disparityFilter.UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 2, UAVs, nullptr);
	}
	else if (!bUseDepthPass && bUseDisparityTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[1] = { frameData.disparityFilter.UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 2, 1, UAVs, nullptr);
	}
	else if (mainConf.EnableTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[1] = { m_cameraFilter[viewIndex][m_currentCameraFilterIndex].UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 1, UAVs, nullptr);
	}
	else
	{
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 0, 0, nullptr, nullptr);
	}


	ID3D11Buffer* psBuffers[2] = { frameData.psPassConstantBuffer.Get(), viewData.psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);


	if (mainConf.ProjectionMode == Projection_StereoReconstruction && !bUseFullscreenPass)
	{
		if (bUseDepthPass && bUseDisparityTemporalFiltering)
		{
			int prevSwapchain = (eye == LEFT_EYE) ? m_prevSwapchainLeft : m_prevSwapchainRight;
			ID3D11ShaderResourceView* vsSRVs[6] = {
				viewData.passthroughDepthStencil[0].SRV.Get(),
				viewData.passthroughDepthStencil[1].SRV.Get(),
				viewData.passthroughCameraValidity.SRV.Get() ,
				m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[0].SRV.Get(),
				m_viewData[viewIndex][prevSwapchain].passthroughDepthStencil[1].SRV.Get(),
				m_viewData[viewIndex][prevSwapchain].passthroughCameraValidity.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 6, vsSRVs);
		}
		else if (bUseDepthPass)
		{
			ID3D11ShaderResourceView* vsSRVs[3] = {
				viewData.passthroughDepthStencil[0].SRV.Get(),
				viewData.passthroughDepthStencil[1].SRV.Get(),
				viewData.passthroughCameraValidity.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 3, vsSRVs);
		}
		else if (bUseDisparityTemporalFiltering)
		{
			ID3D11ShaderResourceView* vsSRVs[2] = {
				m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get(),
				m_frameData[m_prevDepthUpdatedFrameIndex].disparityFilter.SRV.Get()
			};
			m_renderContext->VSSetShaderResources(0, 2, vsSRVs);
		}
		else
		{
			ID3D11ShaderResourceView* vsSRVs[1] = { m_frameData[m_depthUpdatedFrameIndex].disparityMap.SRV.Get() };
			m_renderContext->VSSetShaderResources(0, 1, vsSRVs);
		}
	}


	if (bUseDisparityTemporalFiltering && depthFrame->bIsFirstRender)
	{
		VSViewConstantBuffer vsViewBuffer = {};

		vsViewBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
		XrMatrix4x4f_Invert(&vsViewBuffer.HMDProjectionToWorld, &vsViewBuffer.worldToHMDProjection);
		vsViewBuffer.prevHMDFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevHMDFrame_WorldToHMDProjectionLeft : frame->prevHMDFrame_WorldToHMDProjectionRight;
		vsViewBuffer.prevCameraFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevCameraFrame_WorldToHMDProjectionLeft : frame->prevCameraFrame_WorldToHMDProjectionRight;

		vsViewBuffer.disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
		vsViewBuffer.projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
		vsViewBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
		vsViewBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
		vsViewBuffer.cameraBlendWeight = 1.0;
		vsViewBuffer.cameraViewIndex = viewIndex;

		vsViewBuffer.bWriteDisparityFilter = true;

		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsViewBuffer, 0, 0);
	}

	bool bInvertAlpha = (renderParams.bInvertLayerAlpha && bUseDepthPass && renderParams.BlendMode != Masked);

	if (bCompositeDepth && bUseDepthPass && renderParams.BlendMode != Additive)
	{
		m_renderContext->OMSetBlendState(m_blendStateSrcAlpha.Get(), nullptr, UINT_MAX);
	}
	else if ((renderParams.BlendMode == AlphaBlendPremultiplied && (!bCompositeDepth || bUseDepthPass)) || 
		renderParams.BlendMode == Additive)
	{
		m_renderContext->OMSetBlendState(bInvertAlpha ? m_blendStateInvDestAlphaPremultiplied.Get() : m_blendStateDestAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else
	{
		m_renderContext->OMSetBlendState(bInvertAlpha ? m_blendStateInvDestAlpha.Get() : m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);
	}

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, bWriteDepth), 1);


	if (!bUseFullscreenPass && mainConf.EnableTemporalFiltering)
	{
		ID3D11ShaderResourceView* psSRVs[3];
		m_renderContext->PSGetShaderResources(0, 2, psSRVs);

		psSRVs[2] = m_cameraFilter[viewIndex][m_currentCameraFilterIndex == 0 ? 1 : 0].SRV.Get();
		m_renderContext->PSSetShaderResources(0, 3, psSRVs);
	}


	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };

	if (bUseFullscreenPass)
	{
		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_renderContext->VSSetShader(m_fullscreenQuadVS.Get(), nullptr, 0);

		ID3D11ShaderResourceView* psSRVs[6];
		m_renderContext->PSGetShaderResources(0, 2, psSRVs);

		if (stereoConf.StereoCutoutEnabled && mainConf.EnableTemporalFiltering)
		{
			psSRVs[2] = viewData.passthroughCameraValidity.SRV.Get();
			psSRVs[3] = viewData.passthroughDepthStencil[0].SRV.Get();
			psSRVs[4] = viewData.passthroughDepthStencil[1].SRV.Get();
			psSRVs[5] = m_cameraFilter[viewIndex][m_currentCameraFilterIndex == 0 ? 1 : 0].SRV.Get();

			m_renderContext->PSSetShaderResources(0, 6, psSRVs);

			m_renderContext->PSSetShader(m_fullscreenPassthroughCompositeTemporalPS.Get(), nullptr, 0);
		}
		else if (stereoConf.StereoCutoutEnabled)
		{
			psSRVs[2] = viewData.passthroughCameraValidity.SRV.Get();
			psSRVs[3] = viewData.passthroughDepthStencil[0].SRV.Get();
			psSRVs[4] = viewData.passthroughDepthStencil[1].SRV.Get();

			m_renderContext->PSSetShaderResources(0, 5, psSRVs);

			m_renderContext->PSSetShader(m_fullscreenPassthroughCompositePS.Get(), nullptr, 0);
		}
		else if (mainConf.EnableTemporalFiltering)
		{
			psSRVs[2] = viewData.passthroughCameraValidity.SRV.Get();
			psSRVs[3] = viewData.passthroughDepthStencil[0].SRV.Get();
			psSRVs[4] = m_cameraFilter[viewIndex][m_currentCameraFilterIndex == 0 ? 1 : 0].SRV.Get();

			m_renderContext->PSSetShaderResources(0, 5, psSRVs);

			m_renderContext->PSSetShader(m_fullscreenPassthroughTemporalPS.Get(), nullptr, 0);
		}
		else
		{
			psSRVs[2] = viewData.passthroughCameraValidity.SRV.Get();
			psSRVs[3] = viewData.passthroughDepthStencil[0].SRV.Get();

			m_renderContext->PSSetShaderResources(0, 4, psSRVs);

			m_renderContext->PSSetShader(m_fullscreenPassthroughPS.Get(), nullptr, 0);
		}


		m_renderContext->Draw(3, 0);


	}
	else if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{

		m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		numIndices = (UINT)m_gridMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_gridMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_gridMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

		if (bUseDepthPass)
		{
			m_renderContext->VSSetShader(m_passthroughReadDepthVS.Get(), nullptr, 0);

			if (stereoConf.StereoCutoutEnabled && mainConf.EnableTemporalFiltering)
			{
				m_renderContext->PSSetShader(m_stereoCompositeTemporalPS.Get(), nullptr, 0);
			}
			else if (stereoConf.StereoCutoutEnabled)
			{
				m_renderContext->PSSetShader(m_stereoCompositePS.Get(), nullptr, 0);
			}
		}
		else if (bUseDisparityTemporalFiltering)
		{
			m_renderContext->VSSetShader(m_passthroughStereoTemporalVS.Get(), nullptr, 0);

			m_renderContext->PSSetShader(mainConf.EnableTemporalFiltering ? m_passthroughTemporalPS.Get() : m_passthroughPS.Get(), nullptr, 0);
		}
		else
		{
			m_renderContext->VSSetShader(m_passthroughStereoVS.Get(), nullptr, 0);

			m_renderContext->PSSetShader(mainConf.EnableTemporalFiltering ? m_passthroughTemporalPS.Get() : m_passthroughPS.Get(), nullptr, 0);
		}

		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}
	else
	{
		numIndices = (UINT)m_cylinderMesh.triangles.size() * 3;
		m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
		m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_renderContext->VSSetShader(m_passthroughVS.Get(), nullptr, 0);

		m_renderContext->PSSetShader(mainConf.EnableTemporalFiltering ? m_passthroughTemporalPS.Get() : m_passthroughPS.Get(), nullptr, 0);


		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}
	



	// Draw the other stereo camera on occluded areas when using direct rendering. 
	if (mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoCutoutEnabled && !bUseDepthPass)
	{
		float secondaryWidthFactor = 0.6f;
		XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;
		int scissorStart = (eye == LEFT_EYE) ? (int)(rect.extent.width * (1.0f - secondaryWidthFactor)) : 0;
		int scissorEnd = (eye == LEFT_EYE) ? rect.extent.width : (int)(rect.extent.width * secondaryWidthFactor);
		D3D11_RECT crossScissor = { rect.offset.x + scissorStart, rect.offset.y, rect.offset.x + scissorEnd, rect.offset.y + rect.extent.height };
		m_renderContext->RSSetScissorRects(1, &crossScissor);

		VSViewConstantBuffer vsCrossBuffer = {};
		vsCrossBuffer.worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
		XrMatrix4x4f_Invert(&vsCrossBuffer.HMDProjectionToWorld, &vsCrossBuffer.worldToHMDProjection);
		vsCrossBuffer.prevHMDFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevHMDFrame_WorldToHMDProjectionLeft : frame->prevHMDFrame_WorldToHMDProjectionRight;
		vsCrossBuffer.prevCameraFrame_WorldToHMDProjection = (eye == LEFT_EYE) ? frame->prevCameraFrame_WorldToHMDProjectionLeft : frame->prevCameraFrame_WorldToHMDProjectionRight;
		vsCrossBuffer.projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
		vsCrossBuffer.projectionDistance = mainConf.ProjectionDistanceFar;
		vsCrossBuffer.floorHeightOffset = mainConf.FloorHeightOffset;
		vsCrossBuffer.cameraBlendWeight = 1.0;

		vsCrossBuffer.disparityUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, StereoHorizontalLayout);
		vsCrossBuffer.cameraViewIndex = (eye != LEFT_EYE) ? 0 : 1;
		vsCrossBuffer.bWriteDisparityFilter = false;
		m_renderContext->UpdateSubresource(viewData.vsViewConstantBuffer.Get(), 0, nullptr, &vsCrossBuffer, 0, 0);

		PSViewConstantBuffer psCrossBuffer = {};
		psCrossBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
		psCrossBuffer.cameraViewIndex = viewIndex;
		psCrossBuffer.bUseFullscreenQuad = false;

		psCrossBuffer.frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, frame->frameLayout);
		psCrossBuffer.bDoCutout = false;
		psCrossBuffer.bPremultiplyAlpha = false;
		m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psCrossBuffer, 0, 0);

		m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, false), 1);
		m_renderContext->PSSetShader(m_passthroughPS.Get(), nullptr, 0);


		m_renderContext->DrawIndexed(numIndices, 0, 0);
	}
}




// Renders a background cylinder behind any gaps.
void PassthroughRendererDX11::RenderBackgroundForView(const ERenderEye eye,  const XrCompositionLayerProjection* layer, CameraFrame* frame, std::shared_ptr<DepthFrame> depthFrame,  UINT numIndices, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	DX11FrameData& prevFrameData = m_frameData[m_prevFrameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	ID3D11RenderTargetView* rendertarget = viewData.renderTarget.RTV.Get();
	if (!rendertarget) { return; }

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	ID3D11DepthStencilView* depthStencil = nullptr;

	if (m_viewDepthData[viewIndex].size() > depthSwapchainIndex)
	{
		depthStencil = m_viewDepthData[viewIndex][depthSwapchainIndex].depthStencilView.Get();
	}

	Config_Depth& depthConfig = m_configManager->GetConfig_Depth();
	bool bCompositeDepth = renderParams.bEnableDepthBlending && depthStencil != nullptr;
	bool bWriteDepth = depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication;

	bool bUseDepthPass = mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoUseSeparateDepthPass;

	// Reset constant buffers if the main pass changed them.
	if ((mainConf.ProjectionMode == Projection_StereoReconstruction && stereoConf.StereoCutoutEnabled && !bUseDepthPass) ||
		(stereoConf.StereoUseDisparityTemporalFiltering && depthFrame->bIsFirstRender))
	{
		RenderSetupView(eye, layer, frame, depthFrame, renderParams);
	}
		
	if (mainConf.EnableTemporalFiltering)
	{
		ID3D11UnorderedAccessView* UAVs[1] = { m_cameraFilter[viewIndex][m_currentCameraFilterIndex].UAV.Get() };
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 1, 1, UAVs, nullptr);
	}
	else
	{
		m_renderContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &rendertarget, depthStencil, 0, 0, nullptr, nullptr);
	}

	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT strides[] = { sizeof(float) * 3 };
	const UINT offsets[] = { 0 };
	m_renderContext->IASetVertexBuffers(0, 1, m_cylinderMeshVertexBuffer.GetAddressOf(), strides, offsets);
	m_renderContext->IASetIndexBuffer(m_cylinderMeshIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_renderContext->VSSetShader(m_passthroughVS.Get(), nullptr, 0);
	m_renderContext->RSSetState(frame->bIsRenderingMirrored ? m_rasterizerStateDepthBiasMirrored.Get() : m_rasterizerStateDepthBias.Get());

	m_renderContext->OMSetBlendState(renderParams.bInvertLayerAlpha ? m_blendStateInvDestAlpha.Get() : m_blendStateDestAlpha.Get(), nullptr, UINT_MAX);

	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(bCompositeDepth, frame->bHasReversedDepth, depthConfig.DepthWriteOutput && depthConfig.DepthReadFromApplication), 1);

	m_renderContext->PSSetShader(m_passthroughPS.Get(), nullptr, 0);


	m_renderContext->DrawIndexed((UINT)m_cylinderMesh.triangles.size() * 3, 0, 0);
}




void PassthroughRendererDX11::RenderDebugView(const ERenderEye eye, const XrCompositionLayerProjection* layer, FrameRenderParameters& renderParams)
{
	int swapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftFrameIndex : renderParams.RightFrameIndex;
	int depthSwapchainIndex = (eye == LEFT_EYE) ? renderParams.LeftDepthIndex : renderParams.RightDepthIndex;

	if (swapchainIndex < 0) { return; }

	DX11FrameData& frameData = m_frameData[m_frameIndex];
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;

	DX11ViewData& viewData = m_viewData[viewIndex][swapchainIndex];

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();

	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_renderContext->VSSetShader(m_fullscreenQuadVS.Get(), nullptr, 0);

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

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

	m_renderContext->UpdateSubresource(viewData.psViewConstantBuffer.Get(), 0, nullptr, &psViewBuffer, 0, 0);
	m_renderContext->PSSetConstantBuffers(1, 1, viewData.psViewConstantBuffer.GetAddressOf());

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);
	m_renderContext->OMSetDepthStencilState(GET_DEPTH_STENCIL_STATE(false, false, false), 1);
	

	if (mainConf.DebugSource == DebugSource_ApplicationDepth)
	{
		if (m_viewDepthData[viewIndex].size() <= depthSwapchainIndex)
		{
			return;
		}

		m_renderContext->OMSetRenderTargets(1, viewData.renderTarget.RTV.GetAddressOf(), nullptr);

		m_renderContext->PSSetShaderResources(0, 1, m_viewDepthData[viewIndex][depthSwapchainIndex].depthSRV.GetAddressOf());
		m_renderContext->PSSetShader(m_debugDepthToColorPS.Get(), nullptr, 0);

		m_renderContext->Draw(3, 0);
	}
	else if (mainConf.DebugSource == DebugSource_ApplicationAlpha)
	{
		bool bSingleStereoRenderTarget = abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2;

		DX11TemporaryRenderTarget& tempTarget = GetTemporaryRenderTarget(m_frameIndex, bSingleStereoRenderTarget ? 0 : viewIndex);

		m_renderContext->OMSetRenderTargets(1, tempTarget.RTV.GetAddressOf(), nullptr);
		m_renderContext->PSSetShaderResources(0, 1, viewData.renderTarget.SRV.GetAddressOf());
		m_renderContext->PSSetShader(m_debugAlphaToColorPS.Get(), nullptr, 0);

		
		m_renderContext->Draw(3, 0);


		m_renderContext->PSSetShader(m_debugDepthToColorPS.Get(), nullptr, 0);

		ID3D11RenderTargetView* nullRTV = nullptr;
		m_renderContext->OMSetRenderTargets(1, &nullRTV, nullptr);
		m_renderContext->PSSetShaderResources(0, 1, tempTarget.SRV.GetAddressOf());
		m_renderContext->OMSetRenderTargets(1, viewData.renderTarget.RTV.GetAddressOf(), nullptr);
		

		m_renderContext->Draw(3, 0);
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
