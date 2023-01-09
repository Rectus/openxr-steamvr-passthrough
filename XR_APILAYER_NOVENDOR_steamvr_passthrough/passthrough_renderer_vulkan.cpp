
#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <PathCch.h>
#include <xr_linear.h>
#include "lodepng.h"

#include "shaders\passthrough_vs.spv.h"

#include "shaders\alpha_prepass_ps.spv.h"
#include "shaders\alpha_prepass_masked_ps.spv.h"
#include "shaders\passthrough_ps.spv.h"
#include "shaders\passthrough_masked_ps.spv.h"


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



PassthroughRendererVulkan::PassthroughRendererVulkan(XrGraphicsBindingVulkanKHR& binding, HMODULE dllMoudule, std::shared_ptr<ConfigManager> configManager)
	: m_dllModule(dllMoudule)
	, m_configManager(configManager)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
{
	m_instance = binding.instance;
	m_physDevice = binding.physicalDevice;
	m_device = binding.device;
	m_queueFamilyIndex = binding.queueFamilyIndex;
	m_queueIndex = binding.queueIndex;
}


bool PassthroughRendererVulkan::InitRenderer()
{
	vkGetDeviceQueue(m_device, m_queueFamilyIndex, m_queueIndex, &m_queue);

	m_vertexShader = CreateShaderModule(g_PassthroughShaderVS, ARRAYSIZE(g_PassthroughShaderVS) * sizeof(g_PassthroughShaderVS[0]));

	m_pixelShader = CreateShaderModule(g_PassthroughShaderPS, ARRAYSIZE(g_PassthroughShaderPS) * sizeof(g_PassthroughShaderPS[0]));

	m_prepassShader = CreateShaderModule(g_AlphaPrepassShaderPS, ARRAYSIZE(g_AlphaPrepassShaderPS) * sizeof(g_AlphaPrepassShaderPS[0]));

	m_maskedPrepassShader = CreateShaderModule(g_AlphaPrepassMaskedShaderPS, ARRAYSIZE(g_AlphaPrepassMaskedShaderPS) * sizeof(g_AlphaPrepassMaskedShaderPS[0]));

	m_maskedPixelShader = CreateShaderModule(g_PassthroughMaskedShaderPS, ARRAYSIZE(g_PassthroughMaskedShaderPS) * sizeof(g_PassthroughMaskedShaderPS[0]));

	if (!m_vertexShader || !m_pixelShader || !m_prepassShader || !m_maskedPrepassShader || !m_maskedPixelShader)
	{
		return false;
	}



	


	//D3D11_BUFFER_DESC bufferDesc = {};
	//bufferDesc.ByteWidth = sizeof(XrMatrix4x4f) * 2;
	//bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	//for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
	//{
	//	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_vsConstantBuffer[i])))
	//	{
	//		return false;
	//	}
	//}

	//bufferDesc.ByteWidth = 32;
	//if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psPassConstantBuffer)))
	//{
	//	return false;
	//}

	//bufferDesc.ByteWidth = 32;
	//if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psViewConstantBuffer)))
	//{
	//	return false;
	//}

	//bufferDesc.ByteWidth = 32;
	//if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psMaskedConstantBuffer)))
	//{
	//	return false;
	//}


	//D3D11_SAMPLER_DESC sampler = {};
	//sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	//sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	//sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	//sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	//sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	//sampler.MaxLOD = D3D12_FLOAT32_MAX;
	//if (FAILED(m_d3dDevice->CreateSamplerState(&sampler, m_defaultSampler.GetAddressOf())))
	//{
	//	return false;
	//}

	//D3D11_BLEND_DESC blendState = {};
	//blendState.RenderTarget[0].BlendEnable = true;
	//blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	//blendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	//blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
	//blendState.RenderTarget[0].DestBlend = D3D11_BLEND_DEST_ALPHA;
	//blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	//blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	//blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	//if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateBase.GetAddressOf())))
	//{
	//	return false;
	//}

	//blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	//if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateAlphaPremultiplied.GetAddressOf())))
	//{
	//	return false;
	//}

	//blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	//blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

	//if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	//{
	//	return false;
	//}

	//blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	//blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	//blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	//blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	//blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	//if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassUseAppAlpha.GetAddressOf())))
	//{
	//	return false;
	//}

	//blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	//if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassIgnoreAppAlpha.GetAddressOf())))
	//{
	//	return false;
	//}


	//D3D11_RASTERIZER_DESC rasterizerDesc = {};
	//rasterizerDesc.CullMode = D3D11_CULL_NONE;
	//rasterizerDesc.DepthClipEnable = false;
	//rasterizerDesc.FrontCounterClockwise = true;
	//rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	//rasterizerDesc.ScissorEnable = true;
	//if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.GetAddressOf())))
	//{
	//	return false;
	//}

	//SetupTestImage();
	//SetupFrameResource();

	return true;
}


bool PassthroughRendererVulkan::SetupPipeline()
{
	VkAttachmentDescription colorDesc{};
	colorDesc.format = VK_FORMAT_R8G8B8A8_SRGB;
	colorDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	colorDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = &colorDesc;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderpass) != VK_SUCCESS)
	{
		return false;
	}


	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

	if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
	{
		return false;
	}

	std::vector<VkDynamicState> dynamicStates = 
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vi.vertexBindingDescriptionCount = 0;
	vi.pVertexBindingDescriptions = nullptr;
	vi.vertexAttributeDescriptionCount = 0;
	vi.pVertexAttributeDescriptions = nullptr;

	VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.primitiveRestartEnable = VK_FALSE;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.depthClampEnable = VK_FALSE;
	rs.rasterizerDiscardEnable = VK_FALSE;
	rs.depthBiasEnable = VK_FALSE;
	rs.depthBiasConstantFactor = 0;
	rs.depthBiasClamp = 0;
	rs.depthBiasSlopeFactor = 0;
	rs.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState attachState{};
	attachState.blendEnable = 0;
	attachState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	attachState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachState.colorBlendOp = VK_BLEND_OP_ADD;
	attachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	attachState.alphaBlendOp = VK_BLEND_OP_ADD;
	attachState.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cb.attachmentCount = 1;
	cb.pAttachments = &attachState;
	cb.logicOpEnable = VK_FALSE;
	cb.logicOp = VK_LOGIC_OP_NO_OP;
	cb.blendConstants[0] = 1.0f;
	cb.blendConstants[1] = 1.0f;
	cb.blendConstants[2] = 1.0f;
	cb.blendConstants[3] = 1.0f;

//	VkRect2D scissor = { {0, 0}, size };
//#if defined(ORIGIN_BOTTOM_LEFT)
//	// Flipped view so origin is bottom-left like GL (requires VK_KHR_maintenance1)
//	VkViewport viewport = { 0.0f, (float)size.height, (float)size.width, -(float)size.height, 0.0f, 1.0f };
//#else
//	// Will invert y after projection
//	VkViewport viewport = { 0.0f, 0.0f, (float)size.width, (float)size.height, 0.0f, 1.0f };
//#endif
	VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 2;
	//vp.pViewports = &viewport;
	vp.scissorCount = 2;
	//vp.pScissors = &scissor;

	VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	ds.depthTestEnable = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;
	ds.depthBoundsTestEnable = VK_FALSE;
	ds.stencilTestEnable = VK_FALSE;
	ds.front.failOp = VK_STENCIL_OP_KEEP;
	ds.front.passOp = VK_STENCIL_OP_KEEP;
	ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
	ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
	ds.back = ds.front;
	ds.minDepthBounds = 0.0f;
	ds.maxDepthBounds = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineShaderStageCreateInfo shaderInfoVertex{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoVertex.module = m_vertexShader;
	shaderInfoVertex.stage = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineShaderStageCreateInfo shaderInfoFragment{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfoVertex.module = m_pixelShader;
	shaderInfoVertex.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::vector<VkPipelineShaderStageCreateInfo> shaderInfo{ shaderInfoVertex, shaderInfoFragment };


	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = (uint32_t)shaderInfo.size();
	pipelineInfo.pStages = shaderInfo.data();
	pipelineInfo.pVertexInputState = &vi;
	pipelineInfo.pInputAssemblyState = &ia;
	pipelineInfo.pTessellationState = nullptr;
	pipelineInfo.pViewportState = &vp;
	pipelineInfo.pRasterizationState = &rs;
	pipelineInfo.pMultisampleState = &ms;
	pipelineInfo.pDepthStencilState = &ds;
	pipelineInfo.pColorBlendState = &cb;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = m_renderpass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineDefault) != VK_SUCCESS)
	{
		return false;
	}


	return true;
}


VkShaderModule PassthroughRendererVulkan::CreateShaderModule(const uint32_t* bytecode, size_t codeSize)
{
	VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	createInfo.codeSize = codeSize;
	createInfo.pCode = bytecode;

	VkShaderModule module;

	if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS)
	{
		return nullptr;
	}

	return module;
}


void PassthroughRendererVulkan::SetupTestImage()
{
	//char path[MAX_PATH];

	//if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	//{
	//	ErrorLog("Error opening test pattern.\n");
	//}

	//std::string pathStr = path;
	//std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\testpattern.png";

	//std::vector<unsigned char> image;
	//unsigned width, height;

	//unsigned error = lodepng::decode(image, width, height, imgPath.c_str());
	//if (error)
	//{
	//	ErrorLog("Error decoding test pattern.\n");
	//}

	//D3D11_TEXTURE2D_DESC textureDesc = {};
	//textureDesc.MipLevels = 1;
	//textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	//textureDesc.Width = width;
	//textureDesc.Height = height;
	//textureDesc.ArraySize = 1;
	//textureDesc.SampleDesc.Count = 1;
	//textureDesc.SampleDesc.Quality = 0;
	//textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	//textureDesc.Usage = D3D11_USAGE_DEFAULT;
	//textureDesc.CPUAccessFlags = 0;

	//m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_testPatternTexture);

	//D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	//uploadTextureDesc.BindFlags = 0;
	//uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	//uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	//ComPtr<ID3D11Texture2D> uploadTexture;
	//m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &uploadTexture);

	//D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	//srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Format = textureDesc.Format;
	//srvDesc.Texture2D.MipLevels = 1;

	//m_d3dDevice->CreateShaderResourceView(m_testPatternTexture.Get(), &srvDesc, &m_testPatternSRV);

	//D3D11_MAPPED_SUBRESOURCE res = {};
	//m_deviceContext->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	//memcpy(res.pData, image.data(), image.size());
	//m_deviceContext->Unmap(uploadTexture.Get(), 0);

	//m_deviceContext->CopyResource(m_testPatternTexture.Get(), uploadTexture.Get());
}


void PassthroughRendererVulkan::SetupFrameResource()
{
	//std::vector<uint8_t> image(m_cameraFrameBufferSize);

	//D3D11_TEXTURE2D_DESC textureDesc = {};
	//textureDesc.MipLevels = 1;
	//textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	//textureDesc.Width = m_cameraTextureWidth;
	//textureDesc.Height = m_cameraTextureHeight;
	//textureDesc.ArraySize = 1;
	//textureDesc.SampleDesc.Count = 1;
	//textureDesc.SampleDesc.Quality = 0;
	//textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	//textureDesc.Usage = D3D11_USAGE_DEFAULT;
	//textureDesc.CPUAccessFlags = 0;

	//D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	//uploadTextureDesc.BindFlags = 0;
	//uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	//uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	//D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	//srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Format = textureDesc.Format;
	//srvDesc.Texture2D.MipLevels = 1;

	//m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraFrameUploadTexture);

	//D3D11_MAPPED_SUBRESOURCE res = {};
	//m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	//memcpy(res.pData, image.data(), image.size());
	//m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	//for (int i = 0; i < NUM_SWAPCHAINS; i++)
	//{
	//	m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_cameraFrameTexture[i]);
	//	m_d3dDevice->CreateShaderResourceView(m_cameraFrameTexture[i].Get(), &srvDesc, &m_cameraFrameSRV[i]);
	//	m_deviceContext->CopyResource(m_cameraFrameTexture[i].Get(), m_cameraFrameUploadTexture.Get());
	//}
}


//void PassthroughRendererVulkan::SetupTemporaryRenderTarget(ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, uint32_t width, uint32_t height)
//{
//
//	D3D11_TEXTURE2D_DESC textureDesc = {};
//	textureDesc.MipLevels = 1;
//	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
//	textureDesc.Width = width;
//	textureDesc.Height = height;
//	textureDesc.ArraySize = 1;
//	textureDesc.SampleDesc.Count = 1;
//	textureDesc.SampleDesc.Quality = 0;
//	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
//	textureDesc.Usage = D3D11_USAGE_DEFAULT;
//	textureDesc.CPUAccessFlags = 0;
//
//	if (FAILED(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, texture)))
//	{
//		return;
//	}
//
//	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
//	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
//	srvDesc.Format = textureDesc.Format;
//	srvDesc.Texture2D.MipLevels = 1;
//
//	m_d3dDevice->CreateShaderResourceView(*texture, &srvDesc, srv);
//
//	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
//	rtvDesc.Format = textureDesc.Format;
//	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
//
//	m_d3dDevice->CreateRenderTargetView(*texture, &rtvDesc, rtv);
//}


void PassthroughRendererVulkan::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	//int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	//int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;
	//if (m_renderTargets[bufferIndex].Get() == (ID3D11Resource*)rendertarget)
	//{
	//	return;
	//}

	//// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	//D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	//rtvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	//rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	//rtvDesc.Texture2DArray.ArraySize = 1;
	//rtvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;
	////rtvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;

	//m_d3dDevice->CreateRenderTargetView((ID3D11Resource*)rendertarget, &rtvDesc, m_renderTargetViews[bufferIndex].GetAddressOf());
	//m_renderTargets[bufferIndex] = (ID3D11Resource*)rendertarget;

	//D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	//srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	////srvDesc.ViewDimension = swapchainInfo.arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	////srvDesc.Texture2D.MipLevels = 1;
	//srvDesc.Texture2DArray.MipLevels = 1;
	//srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	////srvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	//m_d3dDevice->CreateShaderResourceView((ID3D11Resource*)rendertarget, &srvDesc, &m_renderTargetSRVs[bufferIndex]);
}


void PassthroughRendererVulkan::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}


void PassthroughRendererVulkan::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex)
{
	//Config_Main& mainConf = m_configManager->GetConfig_Main();
	//Config_Core& coreConf = m_configManager->GetConfig_Core();

	//if (SUCCEEDED(m_d3dDevice->CreateDeferredContext(0, &m_renderContext)))
	//{
	//	m_bUsingDeferredContext = true;
	//	m_renderContext->ClearState();
	//}
	//else
	//{
	//	m_bUsingDeferredContext = false;
	//	m_renderContext = m_deviceContext;
	//}

	//if (mainConf.ShowTestImage)
	//{
	//	m_renderContext->PSSetShaderResources(0, 1, m_testPatternSRV.GetAddressOf());
	//}
	//else if (frame->frameTextureResource != nullptr)
	//{
	//	// Use shared texture
	//	m_renderContext->PSSetShaderResources(0, 1, (ID3D11ShaderResourceView* const*)&frame->frameTextureResource);
	//}
	//else
	//{
	//	// Upload camera frame from CPU
	//	D3D11_MAPPED_SUBRESOURCE res = {};
	//	m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	//	memcpy(res.pData, frame->frameBuffer->data(), frame->frameBuffer->size());
	//	m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	//	m_deviceContext->CopyResource(m_cameraFrameTexture[m_frameIndex].Get(), m_cameraFrameUploadTexture.Get());

	//	m_renderContext->PSSetShaderResources(0, 1, m_cameraFrameSRV[m_frameIndex].GetAddressOf());
	//}

	//m_renderContext->IASetInputLayout(nullptr);
	//m_renderContext->IASetVertexBuffers(0, 0, nullptr, 0, 0);
	//m_renderContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	//m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	//m_renderContext->RSSetState(m_rasterizerState.Get());

	//m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	//PSPassConstantBuffer buffer = {};
	//buffer.opacity = mainConf.PassthroughOpacity;
	//buffer.brightness = mainConf.Brightness;
	//buffer.contrast = mainConf.Contrast;
	//buffer.saturation = mainConf.Saturation;
	//buffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;

	//m_renderContext->UpdateSubresource(m_psPassConstantBuffer.Get(), 0, nullptr, &buffer, 0, 0);

	//if (blendMode == Masked)
	//{
	//	PSMaskedConstantBuffer maskedBuffer = {};
	//	maskedBuffer.maskedKey[0] = powf(coreConf.CoreForceMaskedKeyColor[0], 2.2f);
	//	maskedBuffer.maskedKey[1] = powf(coreConf.CoreForceMaskedKeyColor[1], 2.2f);
	//	maskedBuffer.maskedKey[2] = powf(coreConf.CoreForceMaskedKeyColor[2], 2.2f);
	//	maskedBuffer.maskedFracChroma = coreConf.CoreForceMaskedFractionChroma * 100.0f;
	//	maskedBuffer.maskedFracLuma = coreConf.CoreForceMaskedFractionLuma * 100.0f;
	//	maskedBuffer.maskedSmooth = coreConf.CoreForceMaskedSmoothing * 100.0f;
	//	maskedBuffer.bMaskedUseCamera = coreConf.CoreForceMaskedUseCameraImage;

	//	m_renderContext->UpdateSubresource(m_psMaskedConstantBuffer.Get(), 0, nullptr, &maskedBuffer, 0, 0);

	//	RenderPassthroughViewMasked(LEFT_EYE, leftSwapchainIndex, layer, frame);
	//	RenderPassthroughViewMasked(RIGHT_EYE, rightSwapchainIndex, layer, frame);
	//}
	//else
	//{
	//	RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode);
	//	RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode);
	//}

	//RenderFrameFinish();
}


void PassthroughRendererVulkan::RenderPassthroughView(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode)
{
	//if (swapchainIndex < 0) { return; }

	//int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	//int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	//ID3D11RenderTargetView* rendertarget = m_renderTargetViews[bufferIndex].Get();

	//if (!rendertarget) { return; }

	//m_renderContext->OMSetRenderTargets(1, &rendertarget, nullptr);

	//XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	//D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	//D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	//m_renderContext->RSSetViewports(1, &viewport);
	//m_renderContext->RSSetScissorRects(1, &scissor);

	//VSConstantBuffer buffer = {};
	//buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	//m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	//m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());
	//m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);

	//PSViewConstantBuffer viewBuffer = {};
	//viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
	//viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	//m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

	//ID3D11Buffer* psBuffers[2] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get() };
	//m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	//// Extra draw if we need to preadjust the alpha.
	//if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f)
	//{
	//	m_renderContext->PSSetShader(m_prepassShader.Get(), nullptr, 0);

	//	if (blendMode == AlphaBlendPremultiplied || blendMode == AlphaBlendUnpremultiplied)
	//	{
	//		m_renderContext->OMSetBlendState(m_blendStatePrepassUseAppAlpha.Get(), nullptr, UINT_MAX);
	//	}
	//	else
	//	{
	//		m_renderContext->OMSetBlendState(m_blendStatePrepassIgnoreAppAlpha.Get(), nullptr, UINT_MAX);
	//	}

	//	m_renderContext->Draw(3, 0);
	//}


	//if (blendMode == AlphaBlendPremultiplied)
	//{
	//	m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	//}
	//else if (blendMode == AlphaBlendUnpremultiplied)
	//{
	//	m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	//}
	//else if (blendMode == Additive)
	//{
	//	m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	//}
	//else
	//{
	//	m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	//}

	//m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);

	//m_renderContext->Draw(3, 0);
}


void PassthroughRendererVulkan::RenderPassthroughViewMasked(const ERenderEye eye, const int32_t swapchainIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame)
{
	//if (swapchainIndex < 0) { return; }

	//int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	//int bufferIndex = viewIndex * NUM_SWAPCHAINS + swapchainIndex;

	//ID3D11RenderTargetView* rendertarget = m_renderTargetViews[bufferIndex].Get();

	//if (!rendertarget) { return; }

	//XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	//D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	//D3D11_RECT scissor = { 0, 0, rect.extent.width, rect.extent.height };

	//m_renderContext->RSSetViewports(1, &viewport);
	//m_renderContext->RSSetScissorRects(1, &scissor);

	//VSConstantBuffer buffer = {};
	//buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	//m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	//m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());

	//m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);

	//{
	//	PSViewConstantBuffer viewBuffer = {};
	//	// Draw the correct half for single framebuffer views.
	//	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	//	{
	//		viewBuffer.prepassUVOffset = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f };
	//		viewBuffer.prepassUVFactor = { 0.5f, 1.0f };
	//	}
	//	else
	//	{
	//		viewBuffer.prepassUVOffset = { 0.0f, 0.0f };
	//		viewBuffer.prepassUVFactor = { 1.0f, 1.0f };
	//	}
	//	viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
	//	viewBuffer.rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	//	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);
	//}

	//ComPtr<ID3D11Texture2D> tempTexture;
	//ComPtr<ID3D11ShaderResourceView> tempSRV;
	//ComPtr<ID3D11RenderTargetView> tempRTV;

	//SetupTemporaryRenderTarget(&tempTexture, &tempSRV, &tempRTV, (uint32_t)rect.extent.width, (uint32_t)rect.extent.height);

	//m_renderContext->OMSetRenderTargets(1, tempRTV.GetAddressOf(), nullptr);
	//m_renderContext->OMSetBlendState(nullptr, nullptr, UINT_MAX);


	//ID3D11ShaderResourceView* cameraFrameSRV;

	//if (m_configManager->GetConfig_Main().ShowTestImage)
	//{
	//	cameraFrameSRV = m_testPatternSRV.Get();
	//}
	//else if (frame->frameTextureResource != nullptr)
	//{
	//	cameraFrameSRV = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	//}
	//else
	//{
	//	cameraFrameSRV = m_cameraFrameSRV[m_frameIndex].Get();
	//}


	//if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	//{
	//	m_renderContext->PSSetShaderResources(0, 1, &cameraFrameSRV);
	//}
	//else
	//{
	//	m_renderContext->PSSetShaderResources(0, 1, m_renderTargetSRVs[bufferIndex].GetAddressOf());
	//}

	//ID3D11Buffer* psBuffers[3] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get(), m_psMaskedConstantBuffer.Get() };
	//m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	//m_renderContext->PSSetShader(m_maskedPrepassShader.Get(), nullptr, 0);

	//m_renderContext->Draw(3, 0);


	//{
	//	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	//	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	//	m_renderContext->RSSetViewports(1, &viewport);
	//	m_renderContext->RSSetScissorRects(1, &scissor);
	//}

	//// Clear rendertarget so we can swap the places of the RTV and SRV.
	//ID3D11RenderTargetView* nullRTV = nullptr;
	//m_renderContext->OMSetRenderTargets(1, &nullRTV, nullptr);

	//ID3D11ShaderResourceView* views[2] = { cameraFrameSRV, tempSRV.Get() };
	//m_renderContext->PSSetShaderResources(0, 2, views);
	//m_renderContext->OMSetRenderTargets(1, &rendertarget, nullptr);
	//m_renderContext->OMSetBlendState(m_blendStateSrcAlpha.Get(), nullptr, UINT_MAX);
	//m_renderContext->PSSetShader(m_maskedPixelShader.Get(), nullptr, 0);

	//m_renderContext->Draw(3, 0);
}


void PassthroughRendererVulkan::RenderFrameFinish()
{


	m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
}


void* PassthroughRendererVulkan::GetRenderDevice()
{
	return m_device;
}