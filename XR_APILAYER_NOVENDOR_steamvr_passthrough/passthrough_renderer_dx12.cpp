

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


enum ECBV_SRVIndex
{
	INDEX_CBV_VS_0 = 0,
	INDEX_CBV_VS_1,
	INDEX_CBV_VS_2,
	INDEX_CBV_VS_3,
	INDEX_CBV_VS_4,
	INDEX_CBV_VS_5,

	INDEX_CBV_PS_PASS_0,
	INDEX_CBV_PS_PASS_1,
	INDEX_CBV_PS_PASS_2,

	INDEX_CBV_PS_VIEW_0,
	INDEX_CBV_PS_VIEW_1,
	INDEX_CBV_PS_VIEW_2,
	INDEX_CBV_PS_VIEW_3,
	INDEX_CBV_PS_VIEW_4,
	INDEX_CBV_PS_VIEW_5,

	INDEX_CBV_PS_MASKED_0,
	INDEX_CBV_PS_MASKED_1,
	INDEX_CBV_PS_MASKED_2,

	INDEX_SRV_CAMERAFRAME_0,
	INDEX_SRV_CAMERAFRAME_1,
	INDEX_SRV_CAMERAFRAME_2,

	INDEX_SRV_MASKED_INTERMEDIATE_0,
	INDEX_SRV_MASKED_INTERMEDIATE_1,
	INDEX_SRV_MASKED_INTERMEDIATE_2,
	INDEX_SRV_MASKED_INTERMEDIATE_3,
	INDEX_SRV_MASKED_INTERMEDIATE_4,
	INDEX_SRV_MASKED_INTERMEDIATE_5,

	INDEX_SRV_RT_0,
	INDEX_SRV_RT_1,
	INDEX_SRV_RT_2,
	INDEX_SRV_RT_3,
	INDEX_SRV_RT_4,
	INDEX_SRV_RT_5,

	INDEX_SRV_TESTIMAGE,

	CBV_SRV_HEAPSIZE
};


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



inline uint32_t Align(const uint32_t value, const uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint32_t size, D3D12_HEAP_TYPE heapType)
{
	D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

	if (heapType == D3D12_HEAP_TYPE_UPLOAD) 
	{
		resourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
		size = Align(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	}
	
	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = heapType;

	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = size;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> buffer;

	device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &bufferDesc, resourceState, nullptr, IID_PPV_ARGS(&buffer));

	return buffer;
}

inline void TransitionResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = 0;

	commandList->ResourceBarrier(1, &barrier);
}


void UploadTexture(ID3D12GraphicsCommandList* commandList, ID3D12Resource* texture, ID3D12Resource* uploadHeap, size_t heapOffset, std::vector<uint8_t>& image, uint32_t textureWidth, uint32_t textureHeight, DXGI_FORMAT format, uint32_t pixelSize, uint32_t subResource)
{
	D3D12_SUBRESOURCE_FOOTPRINT pitchedDesc = {};
	pitchedDesc.Format = format;
	pitchedDesc.Width = textureWidth;
	pitchedDesc.Height = textureHeight;
	pitchedDesc.Depth = 1;
	pitchedDesc.RowPitch = Align(textureWidth * pixelSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTexture2D = {};
	placedTexture2D.Offset = heapOffset;
	placedTexture2D.Footprint = pitchedDesc;

	uint8_t* dataPtr;
	uploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&dataPtr));

	for (uint32_t i = 0; i < textureHeight; i++)
	{
		size_t offset = pitchedDesc.RowPitch * i;
		memcpy(dataPtr + heapOffset + offset, image.data() + offset, textureWidth * pixelSize);
	}

	uploadHeap->Unmap(0, nullptr);

	D3D12_TEXTURE_COPY_LOCATION copyDest = {};
	copyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	copyDest.pResource = texture;
	copyDest.SubresourceIndex = subResource;

	D3D12_TEXTURE_COPY_LOCATION copySrc = {};
	copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	copySrc.pResource = uploadHeap;
	copySrc.PlacedFootprint = placedTexture2D;

	commandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);
}





PassthroughRendererDX12::PassthroughRendererDX12(ID3D12Device* device, ID3D12CommandQueue* commandQueue, HMODULE dllMoudule, std::shared_ptr<ConfigManager> configManager)
	: m_d3dDevice(device)
	, m_d3dCommandQueue(commandQueue)
	, m_dllModule(dllMoudule)
	, m_configManager(configManager)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
{
	memset(m_vsConstantBufferCPUData, 0, sizeof(m_vsConstantBufferCPUData));
	memset(m_psPassConstantBufferCPUData, 0, sizeof(m_psPassConstantBufferCPUData));
	memset(m_psViewConstantBufferCPUData, 0, sizeof(m_psViewConstantBufferCPUData));
	memset(m_psMaskedConstantBufferCPUData, 0, sizeof(m_psMaskedConstantBufferCPUData));
}


bool PassthroughRendererDX12::InitRenderer()
{
	if (!CreateRootSignature())
	{
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = NUM_SWAPCHAINS * 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVHeap));

	m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_intermediateRTVHeap));

	m_RTVHeapDescSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
	cbvSrvHeapDesc.NumDescriptors = CBV_SRV_HEAPSIZE;
	cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	m_d3dDevice->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_CBVSRVHeap));

	m_CBVSRVHeapDescSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	uint32_t align = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

	{
		m_vsConstantBuffer = CreateBuffer(m_d3dDevice.Get(), align * NUM_SWAPCHAINS * 2, D3D12_HEAP_TYPE_UPLOAD);

		UINT8* bufferPtr;
		D3D12_RANGE readRange = { 0, 0 };
		m_vsConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&bufferPtr));
		
		for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
		{
			int bufferOffset = i * align;
			m_vsConstantBufferCPUData[i] = bufferPtr + bufferOffset;

			D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (INDEX_CBV_VS_0 + i) * m_CBVSRVHeapDescSize;

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = m_vsConstantBuffer->GetGPUVirtualAddress() + bufferOffset;
			cbvDesc.SizeInBytes = align;
			m_d3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	{
		m_psPassConstantBuffer = CreateBuffer(m_d3dDevice.Get(), align * NUM_SWAPCHAINS, D3D12_HEAP_TYPE_UPLOAD);

		UINT8* bufferPtr;
		D3D12_RANGE readRange = { 0, 0 };
		m_psPassConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&bufferPtr));

		for (int i = 0; i < NUM_SWAPCHAINS; i++)
		{
			int bufferOffset = i * align;
			m_psPassConstantBufferCPUData[i] = bufferPtr + bufferOffset;

			D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (INDEX_CBV_PS_PASS_0 + i) * m_CBVSRVHeapDescSize;

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = m_psPassConstantBuffer->GetGPUVirtualAddress() + bufferOffset;
			cbvDesc.SizeInBytes = align;
			m_d3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	{
		m_psViewConstantBuffer = CreateBuffer(m_d3dDevice.Get(), align * NUM_SWAPCHAINS * 2, D3D12_HEAP_TYPE_UPLOAD);

		UINT8* bufferPtr;
		D3D12_RANGE readRange = { 0, 0 };
		m_psViewConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&bufferPtr));

		for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
		{
			int bufferOffset = i * align;
			m_psViewConstantBufferCPUData[i] = bufferPtr + bufferOffset;

			D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (INDEX_CBV_PS_VIEW_0 + i) * m_CBVSRVHeapDescSize;

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = m_psViewConstantBuffer->GetGPUVirtualAddress() + bufferOffset;
			cbvDesc.SizeInBytes = align;
			m_d3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	{
		m_psMaskedConstantBuffer = CreateBuffer(m_d3dDevice.Get(), align * NUM_SWAPCHAINS, D3D12_HEAP_TYPE_UPLOAD);

		UINT8* bufferPtr;
		D3D12_RANGE readRange = { 0, 0 };
		m_psMaskedConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&bufferPtr));

		for (int i = 0; i < NUM_SWAPCHAINS; i++)
		{
			int bufferOffset = i * align;
			m_psMaskedConstantBufferCPUData[i] = bufferPtr + bufferOffset;

			D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (INDEX_CBV_PS_MASKED_0 + i) * m_CBVSRVHeapDescSize;

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = m_psMaskedConstantBuffer->GetGPUVirtualAddress() + bufferOffset;
			cbvDesc.SizeInBytes = align;
			m_d3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
	{
		if (FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))))
		{
			ErrorLog("Failed to create D3D12 command allocator.\n");
			return false;
		}
	}

	if (FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
	{
		ErrorLog("Failed to create D3D12 command list.\n");
		return false;
	}

	SetupTestImage();
	SetupFrameResource();

	m_commandList->Close();
	ID3D12CommandList* commandLists[] = { m_commandList.Get() };
	m_d3dCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	return true;
}


void PassthroughRendererDX12::SetupTestImage()
{
	char path[MAX_PATH];

	if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	{
		ErrorLog("Error opening test pattern.\n");
	}

	std::string pathStr = path;
	std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\testpattern.png";

	std::vector<uint8_t> image;
	unsigned width, height;

	unsigned error = lodepng::decode(image, width, height, imgPath.c_str());
	if (error)
	{
		ErrorLog("Error decoding test pattern.\n");
	}

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_testPattern));

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += INDEX_SRV_TESTIMAGE * m_CBVSRVHeapDescSize;

	m_d3dDevice->CreateShaderResourceView(m_testPattern.Get(), nullptr, srvHandle);


	m_testPatternUploadHeap = CreateBuffer(m_d3dDevice.Get(), (uint32_t)image.size(), D3D12_HEAP_TYPE_UPLOAD);

	UploadTexture(m_commandList.Get(), m_testPattern.Get(), m_testPatternUploadHeap.Get(), 0, image, width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);

	TransitionResource(m_commandList.Get(), m_testPattern.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}


void PassthroughRendererDX12::SetupFrameResource()
{
	std::vector<uint8_t> image(m_cameraFrameBufferSize);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = image.data();
	textureData.RowPitch = m_cameraTextureWidth * 4;
	textureData.SlicePitch = textureData.RowPitch * m_cameraTextureHeight;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraTextureWidth;
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		m_d3dDevice->CreateCommittedResource(&heapProp,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_cameraFrameRes[i]));

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + i) * m_CBVSRVHeapDescSize;

		m_d3dDevice->CreateShaderResourceView(m_cameraFrameRes[i].Get(), nullptr, srvHandle);
	}

	m_frameResUploadHeap = CreateBuffer(m_d3dDevice.Get(), m_cameraFrameBufferSize * NUM_SWAPCHAINS, D3D12_HEAP_TYPE_UPLOAD);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		UploadTexture(m_commandList.Get(), m_cameraFrameRes[i].Get(), m_frameResUploadHeap.Get(), m_cameraFrameBufferSize * i, image, m_cameraTextureWidth, m_cameraTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);
	}
}


bool PassthroughRendererDX12::CreateRootSignature()
{
	/*D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}*/

	D3D12_DESCRIPTOR_RANGE rangeCBV0;
	rangeCBV0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	rangeCBV0.NumDescriptors = 1;
	rangeCBV0.BaseShaderRegister = 0;
	rangeCBV0.RegisterSpace = 0;
	rangeCBV0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE rangeCBV1;
	rangeCBV1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	rangeCBV1.NumDescriptors = 1;
	rangeCBV1.BaseShaderRegister = 1;
	rangeCBV1.RegisterSpace = 0;
	rangeCBV1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE rangeCBV2;
	rangeCBV2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	rangeCBV2.NumDescriptors = 1;
	rangeCBV2.BaseShaderRegister = 2;
	rangeCBV2.RegisterSpace = 0;
	rangeCBV2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE rangeSRV0;
	rangeSRV0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeSRV0.NumDescriptors = 1;
	rangeSRV0.BaseShaderRegister = 0;
	rangeSRV0.RegisterSpace = 0;
	rangeSRV0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE rangeSRV1;
	rangeSRV1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeSRV1.NumDescriptors = 1;
	rangeSRV1.BaseShaderRegister = 1;
	rangeSRV1.RegisterSpace = 0;
	rangeSRV1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	D3D12_ROOT_PARAMETER rootParams[6];
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[0].DescriptorTable.pDescriptorRanges = &rangeCBV0;

	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &rangeCBV1;

	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[2].DescriptorTable.pDescriptorRanges = &rangeCBV2;

	rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[3].DescriptorTable.pDescriptorRanges = &rangeSRV0;

	rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[4].DescriptorTable.pDescriptorRanges = &rangeSRV1;

	rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[5].DescriptorTable.pDescriptorRanges = &rangeCBV0;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		//D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 6;
	rootSignatureDesc.pParameters = rootParams;
	rootSignatureDesc.Flags = rootSignatureFlags;
	rootSignatureDesc.NumStaticSamplers = 1;
	rootSignatureDesc.pStaticSamplers = &sampler;

	ComPtr<ID3DBlob> rootSignatureBlob;
	ComPtr<ID3DBlob> errorBlob;

	if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSignatureBlob, &errorBlob)))
	{
		ErrorLog("Error serializing root signature.\n");
		return false;
	}

	if (FAILED(m_d3dDevice->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
	{
		ErrorLog("Error creating root signature.\n");
		return false;
	}

	return true;
}


void PassthroughRendererDX12::SetupIntermediateRenderTarget(uint32_t index, uint32_t width, uint32_t height)
{
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&m_intermediateRenderTargets[index]));

	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDesc = m_intermediateRTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvCPUDesc.ptr += index * m_RTVHeapDescSize;
	m_d3dDevice->CreateRenderTargetView(m_intermediateRenderTargets[index].Get(), NULL, rtvCPUDesc);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (INDEX_SRV_MASKED_INTERMEDIATE_0 + index) * m_CBVSRVHeapDescSize;

	m_d3dDevice->CreateShaderResourceView(m_intermediateRenderTargets[index].Get(), nullptr, srvHandle);
}


bool PassthroughRendererDX12::InitPipeline(DXGI_FORMAT rtFormat)
{
	D3D12_SHADER_BYTECODE quadShaderVS = { g_FullscreenQuadShaderVS, sizeof(g_FullscreenQuadShaderVS) };
	D3D12_SHADER_BYTECODE passthroughShaderVS = { g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS) };

	D3D12_SHADER_BYTECODE alphaPrepassShaderPS = { g_AlphaPrepassShaderPS, sizeof(g_AlphaPrepassShaderPS) };
	D3D12_SHADER_BYTECODE alphaPrepassMaskedShaderPS = { g_AlphaPrepassMaskedShaderPS, sizeof(g_AlphaPrepassMaskedShaderPS) };
	D3D12_SHADER_BYTECODE passthroughShaderPS = { g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS) };
	D3D12_SHADER_BYTECODE passthroughMaskedShaderPS = { g_PassthroughMaskedShaderPS, sizeof(g_PassthroughMaskedShaderPS) };

	D3D12_BLEND_DESC blendStateBase = {};
	blendStateBase.RenderTarget[0].BlendEnable = true;
	blendStateBase.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendStateBase.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendStateBase.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
	blendStateBase.RenderTarget[0].DestBlend = D3D12_BLEND_DEST_ALPHA;
	blendStateBase.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendStateBase.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStateBase.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStateAlphaPremultiplied = blendStateBase;
	blendStateAlphaPremultiplied.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStateSrcAlpha = blendStateBase;
	blendStateSrcAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendStateSrcAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

	D3D12_BLEND_DESC blendStatePrepassUseAppAlpha = blendStateBase;
	blendStatePrepassUseAppAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALPHA;
	blendStatePrepassUseAppAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	blendStatePrepassUseAppAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendStatePrepassUseAppAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStatePrepassUseAppAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStatePrepassIgnoreAppAlpha = blendStateBase;
	blendStatePrepassIgnoreAppAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	D3D12_BLEND_DESC blendStateDisabled = {};
	blendStateDisabled.RenderTarget[0].BlendEnable = false;
	blendStateDisabled.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = passthroughShaderPS;
	psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.BlendState = blendStateBase;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = rtFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoDefault))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = passthroughShaderPS;
	psoDesc.BlendState = blendStateAlphaPremultiplied;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoAlphaPremultiplied))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = alphaPrepassShaderPS;
	psoDesc.BlendState = blendStatePrepassUseAppAlpha;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoPrepassUseAppAlpha))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = alphaPrepassShaderPS;
	psoDesc.BlendState = blendStatePrepassIgnoreAppAlpha;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoPrepassIgnoreAppAlpha))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = alphaPrepassMaskedShaderPS;
	psoDesc.BlendState = blendStateDisabled;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoMaskedPrepass))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = passthroughMaskedShaderPS;
	psoDesc.BlendState = blendStateSrcAlpha;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoMaskedRender))))
	{
		ErrorLog("Error creating PSO.\n");
		return false;
	}

	return true;
}


void PassthroughRendererDX12::InitRenderTarget(const ERenderEye eye, void* rendertarget, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	if (m_renderTargets[bufferIndex].Get() == rendertarget)
	{
		return;
	}

	if (!m_psoDefault.Get())
	{
		if (!InitPipeline((DXGI_FORMAT)swapchainInfo.format))
		{
			return;
		}
	}

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDesc = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvCPUDesc.ptr += ((eye == LEFT_EYE ? 0 : NUM_SWAPCHAINS) + imageIndex) * m_RTVHeapDescSize;

	m_d3dDevice->CreateRenderTargetView((ID3D12Resource*)rendertarget, &rtvDesc, rtvCPUDesc);

	m_renderTargets[bufferIndex] = (ID3D12Resource*)rendertarget;


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = swapchainInfo.arraySize;
	rtvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (INDEX_SRV_RT_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_d3dDevice->CreateShaderResourceView(m_renderTargets[bufferIndex].Get(), &srvDesc, srvHandle);
}


void PassthroughRendererDX12::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}




void PassthroughRendererDX12::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex)
{
	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Core& coreConf = m_configManager->GetConfig_Core();

	ComPtr<ID3D12CommandAllocator> commandAllocator = m_commandAllocators[m_frameIndex];

	commandAllocator->Reset();
	m_commandList->Reset(commandAllocator.Get(), nullptr);
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetDescriptorHeaps(1, m_CBVSRVHeap.GetAddressOf());

	m_commandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	
	if (m_configManager->GetConfig_Main().ShowTestImage)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE testImageSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		testImageSRVHandle.ptr += INDEX_SRV_TESTIMAGE * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, testImageSRVHandle);
	}
	else
	{
		// Upload camera frame
		TransitionResource(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

		UploadTexture(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), m_frameResUploadHeap.Get(), m_cameraFrameBufferSize * m_frameIndex, *frame->frameBuffer.get(), m_cameraTextureWidth, m_cameraTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);

		TransitionResource(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		D3D12_GPU_DESCRIPTOR_HANDLE frameSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		frameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, frameSRVHandle);
	}

	PSPassConstantBuffer* pspBuffer = (PSPassConstantBuffer*)m_psPassConstantBufferCPUData[m_frameIndex];
	pspBuffer->opacity = mainConf.PassthroughOpacity;
	pspBuffer->brightness = mainConf.Brightness;
	pspBuffer->contrast = mainConf.Contrast;
	pspBuffer->saturation = mainConf.Saturation;
	pspBuffer->bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;


	D3D12_GPU_DESCRIPTOR_HANDLE passCBVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	passCBVHandle.ptr += (INDEX_CBV_PS_PASS_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(0, passCBVHandle);


	if (blendMode == Masked)
	{
		PSMaskedConstantBuffer* maskedBuffer = (PSMaskedConstantBuffer*)m_psMaskedConstantBufferCPUData[m_frameIndex];
		maskedBuffer->maskedKey[0] = powf(coreConf.CoreForceMaskedKeyColor[0], 2.2f);
		maskedBuffer->maskedKey[1] = powf(coreConf.CoreForceMaskedKeyColor[1], 2.2f);
		maskedBuffer->maskedKey[2] = powf(coreConf.CoreForceMaskedKeyColor[2], 2.2f);
		maskedBuffer->maskedFracChroma = coreConf.CoreForceMaskedFractionChroma * 100.0f;
		maskedBuffer->maskedFracLuma = coreConf.CoreForceMaskedFractionLuma * 100.0f;
		maskedBuffer->maskedSmooth = coreConf.CoreForceMaskedSmoothing * 100.0f;
		maskedBuffer->bMaskedUseCamera = coreConf.CoreForceMaskedUseCameraImage;

		D3D12_GPU_DESCRIPTOR_HANDLE maskedCBVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		maskedCBVHandle.ptr += (INDEX_CBV_PS_MASKED_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(2, maskedCBVHandle);

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

void PassthroughRendererDX12::RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode)
{
	if (imageIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	ID3D12Resource* rendertarget = m_renderTargets[bufferIndex].Get();

	if (!rendertarget) { return; }

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D12_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D12_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bufferIndex * m_RTVHeapDescSize;
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	VSConstantBuffer* vsBuffer = (VSConstantBuffer*)m_vsConstantBufferCPUData[bufferIndex];
	vsBuffer->cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvVSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvVSHandle.ptr += (INDEX_CBV_VS_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(5, cbvVSHandle);


	PSViewConstantBuffer* psvBuffer = (PSViewConstantBuffer*)m_psViewConstantBufferCPUData[bufferIndex];
	psvBuffer->frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
	psvBuffer->rtArrayIndex = m_frameIndex;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvPSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvPSHandle.ptr += (INDEX_CBV_PS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(1, cbvPSHandle);


	// Extra draw if we need to preadjust the alpha.
	if ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f)
	{
		if (blendMode == AlphaBlendPremultiplied || blendMode == AlphaBlendUnpremultiplied)
		{
			m_commandList->SetPipelineState(m_psoPrepassUseAppAlpha.Get());
		}
		else
		{
			m_commandList->SetPipelineState(m_psoPrepassIgnoreAppAlpha.Get());
		}

		m_commandList->DrawInstanced(3, 1, 0, 0);
	}


	if (blendMode == AlphaBlendPremultiplied)
	{
		m_commandList->SetPipelineState(m_psoAlphaPremultiplied.Get());
	}
	else if (blendMode == AlphaBlendUnpremultiplied)
	{
		m_commandList->SetPipelineState(m_psoDefault.Get());
	}
	else if (blendMode == Additive)
	{
		m_commandList->SetPipelineState(m_psoAlphaPremultiplied.Get());
	}
	else
	{
		m_commandList->SetPipelineState(m_psoDefault.Get());
	}

	m_commandList->DrawInstanced(3, 1, 0, 0);
}


void PassthroughRendererDX12::RenderPassthroughViewMasked(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame)
{
	if (imageIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	ID3D12Resource* rendertarget = m_renderTargets[bufferIndex].Get();

	if (!rendertarget) { return; }

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, rect.extent.width, rect.extent.height };

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	// Recreate the intermediate rendertarget if it can't hold the entire viewport.
	if (!m_intermediateRenderTargets[bufferIndex].Get() 
		|| m_intermediateRenderTargets[bufferIndex].Get()->GetDesc().Width < (uint64_t)rect.extent.width
		|| m_intermediateRenderTargets[bufferIndex].Get()->GetDesc().Height < (uint64_t)rect.extent.height)
	{		
		SetupIntermediateRenderTarget(bufferIndex, rect.extent.width, rect.extent.height);
	}

	VSConstantBuffer* vsBuffer = (VSConstantBuffer*)m_vsConstantBufferCPUData[bufferIndex];
	vsBuffer->cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvVSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvVSHandle.ptr += (INDEX_CBV_VS_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(5, cbvVSHandle);


	PSViewConstantBuffer* psvBuffer = (PSViewConstantBuffer*)m_psViewConstantBufferCPUData[bufferIndex];
	// Draw the correct half for single framebuffer views.
	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	{
		psvBuffer->prepassUVOffset = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f };
		psvBuffer->prepassUVFactor = { 0.5f, 1.0f };
	}
	else
	{
		psvBuffer->prepassUVOffset = { 0.0f, 0.0f };
		psvBuffer->prepassUVFactor = { 1.0f, 1.0f };
	}
	psvBuffer->frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);
	psvBuffer->rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvPSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvPSHandle.ptr += (INDEX_CBV_PS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(1, cbvPSHandle);

	D3D12_GPU_DESCRIPTOR_HANDLE cameraFrameSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();

	if (m_configManager->GetConfig_Main().ShowTestImage)
	{
		cameraFrameSRVHandle.ptr += INDEX_SRV_TESTIMAGE * m_CBVSRVHeapDescSize;
	}
	else
	{
		cameraFrameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
	}

	if (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		m_commandList->SetGraphicsRootDescriptorTable(3, cameraFrameSRVHandle);
	}
	else
	{
		D3D12_GPU_DESCRIPTOR_HANDLE inputRTSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		inputRTSRVHandle.ptr += (INDEX_SRV_RT_0 + bufferIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, inputRTSRVHandle);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_intermediateRTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bufferIndex * m_RTVHeapDescSize;
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	m_commandList->SetPipelineState(m_psoMaskedPrepass.Get());
	m_commandList->DrawInstanced(3, 1, 0, 0);



	viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bufferIndex * m_RTVHeapDescSize;
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	TransitionResource(m_commandList.Get(), m_intermediateRenderTargets[bufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	m_commandList->SetGraphicsRootDescriptorTable(3, cameraFrameSRVHandle);

	D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (INDEX_SRV_MASKED_INTERMEDIATE_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(4, srvHandle);

	m_commandList->SetPipelineState(m_psoMaskedRender.Get());
	m_commandList->DrawInstanced(3, 1, 0, 0);


	TransitionResource(m_commandList.Get(), m_intermediateRenderTargets[bufferIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}


void PassthroughRendererDX12::RenderFrameFinish()
{
	m_commandList->Close();
	m_d3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)m_commandList.GetAddressOf());

	m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
}


void* PassthroughRendererDX12::GetRenderDevice()
{
	return m_d3dDevice.Get();
}