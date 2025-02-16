
// Legacy DX12 renderer

#include "pch.h"
#include "passthrough_renderer.h"
#include <log.h>
#include <PathCch.h>
#include <xr_linear.h>
#include "lodepng.h"

#include "shaders\fullscreen_quad_vs.h"
#include "shaders\passthrough_vs.h"
#include "shaders\passthrough_stereo_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\alpha_copy_masked_ps.h"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


enum ECBV_SRVIndex
{
	INDEX_CBV_VS_PASS_0 = 0,
	INDEX_CBV_VS_PASS_1,
	INDEX_CBV_VS_PASS_2,

	INDEX_CBV_VS_VIEW_0,
	INDEX_CBV_VS_VIEW_1,
	INDEX_CBV_VS_VIEW_2,
	INDEX_CBV_VS_VIEW_3,
	INDEX_CBV_VS_VIEW_4,
	INDEX_CBV_VS_VIEW_5,

	INDEX_CBV_VS_VIEW_CROSS_0,
	INDEX_CBV_VS_VIEW_CROSS_1,
	INDEX_CBV_VS_VIEW_CROSS_2,
	INDEX_CBV_VS_VIEW_CROSS_3,
	INDEX_CBV_VS_VIEW_CROSS_4,
	INDEX_CBV_VS_VIEW_CROSS_5,

	INDEX_CBV_PS_PASS_0,
	INDEX_CBV_PS_PASS_1,
	INDEX_CBV_PS_PASS_2,

	INDEX_CBV_PS_VIEW_0,
	INDEX_CBV_PS_VIEW_1,
	INDEX_CBV_PS_VIEW_2,
	INDEX_CBV_PS_VIEW_3,
	INDEX_CBV_PS_VIEW_4,
	INDEX_CBV_PS_VIEW_5,

	INDEX_CBV_PS_VIEW_CROSS_0,
	INDEX_CBV_PS_VIEW_CROSS_1,
	INDEX_CBV_PS_VIEW_CROSS_2,
	INDEX_CBV_PS_VIEW_CROSS_3,
	INDEX_CBV_PS_VIEW_CROSS_4,
	INDEX_CBV_PS_VIEW_CROSS_5,

	INDEX_CBV_PS_MASKED_0,
	INDEX_CBV_PS_MASKED_1,
	INDEX_CBV_PS_MASKED_2,

	INDEX_SRV_CAMERAFRAME_0,
	INDEX_SRV_CAMERAFRAME_1,
	INDEX_SRV_CAMERAFRAME_2,

	INDEX_SRV_CAMERAFRAME_UNDISTORTED_0,
	INDEX_SRV_CAMERAFRAME_UNDISTORTED_1,
	INDEX_SRV_CAMERAFRAME_UNDISTORTED_2,

	INDEX_SRV_MASKED_INTERMEDIATE_0,
	INDEX_SRV_MASKED_INTERMEDIATE_1,
	INDEX_SRV_MASKED_INTERMEDIATE_2,
	INDEX_SRV_MASKED_INTERMEDIATE_3,
	INDEX_SRV_MASKED_INTERMEDIATE_4,
	INDEX_SRV_MASKED_INTERMEDIATE_5,

	INDEX_SRV_DISPARITY_0,
	INDEX_SRV_DISPARITY_1,
	INDEX_SRV_DISPARITY_2,

	INDEX_SRV_UV_DISTORTION,

	INDEX_SRV_RT_0,
	INDEX_SRV_RT_1,
	INDEX_SRV_RT_2,
	INDEX_SRV_RT_3,
	INDEX_SRV_RT_4,
	INDEX_SRV_RT_5,

	INDEX_SRV_DEBUG_TEXTURE,

	CBV_SRV_HEAPSIZE
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


void UploadTexture(ID3D12GraphicsCommandList* commandList, ID3D12Resource* texture, ID3D12Resource* uploadHeap, size_t heapOffset, uint8_t* image, uint32_t textureWidth, uint32_t textureHeight, DXGI_FORMAT format, uint32_t pixelSize, uint32_t subResource)
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
		size_t inOffset = textureWidth * pixelSize * i;
		size_t outOffset = pitchedDesc.RowPitch * i;
		memcpy(dataPtr + heapOffset + outOffset, image + inOffset, textureWidth * pixelSize);
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
	, m_selectedDebugTexture(DebugTexture_None)
	, m_bUsingDepth(false)
{
	memset(m_vsPassConstantBufferCPUData, 0, sizeof(m_vsPassConstantBufferCPUData));
	memset(m_vsViewConstantBufferCPUData, 0, sizeof(m_vsViewConstantBufferCPUData));
	memset(m_psPassConstantBufferCPUData, 0, sizeof(m_psPassConstantBufferCPUData));
	memset(m_psViewConstantBufferCPUData, 0, sizeof(m_psViewConstantBufferCPUData));
	memset(m_psMaskedConstantBufferCPUData, 0, sizeof(m_psMaskedConstantBufferCPUData));

	m_bUseHexagonGridMesh = m_configManager->GetConfig_Stereo().StereoUseHexagonGridMesh;
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

	m_RTVHeapDescSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = NUM_SWAPCHAINS * 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	m_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap));

	m_DSVHeapDescSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
	cbvSrvHeapDesc.NumDescriptors = CBV_SRV_HEAPSIZE;
	cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	m_d3dDevice->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_CBVSRVHeap));

	m_CBVSRVHeapDescSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_vsPassConstantBuffer = InitBuffer(m_vsPassConstantBufferCPUData, NUM_SWAPCHAINS, 2, INDEX_CBV_VS_PASS_0);
	m_vsViewConstantBuffer = InitBuffer(m_vsViewConstantBufferCPUData, NUM_SWAPCHAINS * 4, 2, INDEX_CBV_VS_VIEW_0);
	m_psPassConstantBuffer = InitBuffer(m_psPassConstantBufferCPUData, NUM_SWAPCHAINS, 1, INDEX_CBV_PS_PASS_0);
	m_psViewConstantBuffer = InitBuffer(m_psViewConstantBufferCPUData, NUM_SWAPCHAINS * 4, 1, INDEX_CBV_PS_VIEW_0);
	m_psMaskedConstantBuffer = InitBuffer(m_psMaskedConstantBufferCPUData, NUM_SWAPCHAINS, 1, INDEX_CBV_PS_MASKED_0);

	if (!m_vsPassConstantBuffer || !m_vsViewConstantBuffer || !m_psPassConstantBuffer || !m_psViewConstantBuffer || !m_psMaskedConstantBuffer)
	{
		ErrorLog("Failed to create D3D12 constant buffers.\n");
		return false;
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

	SetupFrameResource();
	SetupUndistortedFrameResource();
	GenerateMesh();

	m_commandList->Close();
	ID3D12CommandList* commandLists[] = { m_commandList.Get() };
	m_d3dCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	return true;
}


ComPtr<ID3D12Resource> PassthroughRendererDX12::InitBuffer(UINT8** bufferCPUData, int numBuffers, int bufferSizePerAlign, int heapIndex)
{
	uint32_t align = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

	ComPtr<ID3D12Resource> buffer = CreateBuffer(m_d3dDevice.Get(), align * numBuffers * bufferSizePerAlign, D3D12_HEAP_TYPE_UPLOAD);

	UINT8* bufferPtr;
	D3D12_RANGE readRange = { 0, 0 };
	buffer->Map(0, &readRange, reinterpret_cast<void**>(&bufferPtr));

	for (int i = 0; i < numBuffers; i++)
	{
		int bufferOffset = i * align * bufferSizePerAlign;
		bufferCPUData[i] = bufferPtr + bufferOffset;

		D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
		cbvHandle.ptr += (heapIndex + i) * m_CBVSRVHeapDescSize;

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = buffer->GetGPUVirtualAddress() + bufferOffset;
		cbvDesc.SizeInBytes = align * bufferSizePerAlign;
		m_d3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
	}

	return buffer;
}


void PassthroughRendererDX12::SetupDebugTexture(DebugTexture& texture)
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

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = format;
	textureDesc.Width = texture.Width;
	textureDesc.Height = texture.Height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_debugTexture));

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += INDEX_SRV_DEBUG_TEXTURE * m_CBVSRVHeapDescSize;

	m_d3dDevice->CreateShaderResourceView(m_debugTexture.Get(), nullptr, srvHandle);

	int heapSize = Align(texture.Width * texture.PixelSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * texture.Height;

	m_debugTextureUploadHeap = CreateBuffer(m_d3dDevice.Get(), heapSize, D3D12_HEAP_TYPE_UPLOAD);
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
		m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc,D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_cameraFrameRes[i]));

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + i) * m_CBVSRVHeapDescSize;

		m_d3dDevice->CreateShaderResourceView(m_cameraFrameRes[i].Get(), nullptr, srvHandle);
	}

	int heapSize = Align(m_cameraTextureWidth * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * m_cameraTextureHeight * NUM_SWAPCHAINS;
	m_frameResUploadHeap = CreateBuffer(m_d3dDevice.Get(), heapSize, D3D12_HEAP_TYPE_UPLOAD);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		UploadTexture(m_commandList.Get(), m_cameraFrameRes[i].Get(), m_frameResUploadHeap.Get(), m_cameraFrameBufferSize * i, image.data(), m_cameraTextureWidth, m_cameraTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);
	}
}


void PassthroughRendererDX12::SetupUndistortedFrameResource()
{
	std::vector<uint8_t> image(m_cameraUndistortedFrameBufferSize);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = image.data();
	textureData.RowPitch = m_cameraUndistortedTextureWidth * 4;
	textureData.SlicePitch = textureData.RowPitch * m_cameraUndistortedTextureHeight;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraUndistortedTextureWidth;
	textureDesc.Height = m_cameraUndistortedTextureHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_cameraUndisortedFrameRes[i]));

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += (INDEX_SRV_CAMERAFRAME_UNDISTORTED_0 + i) * m_CBVSRVHeapDescSize;

		m_d3dDevice->CreateShaderResourceView(m_cameraUndisortedFrameRes[i].Get(), nullptr, srvHandle);
	}

	int heapSize = Align(m_cameraUndistortedTextureWidth * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * m_cameraUndistortedTextureHeight * NUM_SWAPCHAINS;
	m_undistortedFrameResUploadHeap = CreateBuffer(m_d3dDevice.Get(), heapSize, D3D12_HEAP_TYPE_UPLOAD);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		UploadTexture(m_commandList.Get(), m_cameraUndisortedFrameRes[i].Get(), m_undistortedFrameResUploadHeap.Get(), m_cameraUndistortedFrameBufferSize * i, image.data(), m_cameraUndistortedTextureWidth, m_cameraUndistortedTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);
	}
}


void PassthroughRendererDX12::SetupDisparityMap(uint32_t width, uint32_t height)
{
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16_SNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_disparityMap[i]));

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += (INDEX_SRV_DISPARITY_0 + i) * m_CBVSRVHeapDescSize;

		m_d3dDevice->CreateShaderResourceView(m_disparityMap[i].Get(), nullptr, srvHandle);
	}

	int rowPitch = Align(width * sizeof(uint16_t) * 2, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	int heapSize = Align(rowPitch * height, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) * NUM_SWAPCHAINS;

	m_disparityMapUploadHeap = CreateBuffer(m_d3dDevice.Get(), heapSize, D3D12_HEAP_TYPE_UPLOAD);
}


void PassthroughRendererDX12::SetupUVDistortionMap(std::shared_ptr<std::vector<float>> uvDistortionMap)
{
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = uvDistortionMap->data();
	textureData.RowPitch = m_cameraTextureWidth * 2 * sizeof(float);
	textureData.SlicePitch = textureData.RowPitch * m_cameraTextureHeight;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	textureDesc.Width = m_cameraTextureWidth;
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

	m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_uvDistortionMap));

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += INDEX_SRV_UV_DISTORTION * m_CBVSRVHeapDescSize;

	m_d3dDevice->CreateShaderResourceView(m_uvDistortionMap.Get(), nullptr, srvHandle);

	int rowPitch = Align(m_cameraTextureWidth * 2 * sizeof(float), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	m_uvDistortionMapUploadHeap = CreateBuffer(m_d3dDevice.Get(), rowPitch * m_cameraTextureHeight, D3D12_HEAP_TYPE_UPLOAD);

	UploadTexture(m_commandList.Get(), m_uvDistortionMap.Get(), m_uvDistortionMapUploadHeap.Get(), 0, (uint8_t*)uvDistortionMap->data(), m_cameraTextureWidth, m_cameraTextureHeight, DXGI_FORMAT_R32G32_FLOAT, 2 * sizeof(float), 0);

	TransitionResource(m_commandList.Get(), m_uvDistortionMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

	D3D12_DESCRIPTOR_RANGE rangeSRV2;
	rangeSRV2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeSRV2.NumDescriptors = 1;
	rangeSRV2.BaseShaderRegister = 2;
	rangeSRV2.RegisterSpace = 0;
	rangeSRV2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE rangeUAV2;
	rangeUAV2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	rangeUAV2.NumDescriptors = 1;
	rangeUAV2.BaseShaderRegister = 2;
	rangeUAV2.RegisterSpace = 0;
	rangeUAV2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	D3D12_ROOT_PARAMETER rootParams[11];
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
	rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[5].DescriptorTable.pDescriptorRanges = &rangeSRV2;


	rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[6].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[6].DescriptorTable.pDescriptorRanges = &rangeCBV0;

	rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[7].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[7].DescriptorTable.pDescriptorRanges = &rangeCBV1;

	rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[8].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[8].DescriptorTable.pDescriptorRanges = &rangeSRV0;

	rootParams[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[9].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[9].DescriptorTable.pDescriptorRanges = &rangeSRV1;

	rootParams[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[10].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[10].DescriptorTable.pDescriptorRanges = &rangeUAV2;


	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 11;
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

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R8_UNORM;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	m_d3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_intermediateRenderTargets[index]));

	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDesc = m_intermediateRTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvCPUDesc.ptr += index * m_RTVHeapDescSize;
	m_d3dDevice->CreateRenderTargetView(m_intermediateRenderTargets[index].Get(), NULL, rtvCPUDesc);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (INDEX_SRV_MASKED_INTERMEDIATE_0 + index) * m_CBVSRVHeapDescSize;

	m_d3dDevice->CreateShaderResourceView(m_intermediateRenderTargets[index].Get(), nullptr, srvHandle);
}


bool PassthroughRendererDX12::InitPipeline(bool bFlipTriangles)
{
	D3D12_SHADER_BYTECODE fullscreenQuadShaderVS = { g_FullscreenQuadShaderVS, sizeof(g_FullscreenQuadShaderVS) };
	D3D12_SHADER_BYTECODE passthroughShaderVS = { g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS) };
	D3D12_SHADER_BYTECODE passthroughStereoShaderVS = { g_PassthroughStereoShaderVS, sizeof(g_PassthroughStereoShaderVS) };

	D3D12_SHADER_BYTECODE alphaPrepassShaderPS = { g_AlphaPrepassShaderPS, sizeof(g_AlphaPrepassShaderPS) };
	D3D12_SHADER_BYTECODE alphaPrepassMaskedShaderPS = { g_AlphaPrepassMaskedShaderPS, sizeof(g_AlphaPrepassMaskedShaderPS) };
	D3D12_SHADER_BYTECODE passthroughShaderPS = { g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS) };
	D3D12_SHADER_BYTECODE alphaCopyMaskedShaderPS = { g_AlphaCopyMaskedShaderPS, sizeof(g_AlphaCopyMaskedShaderPS) };


	D3D12_BLEND_DESC blendStateDestAlpha = {};
	blendStateDestAlpha.RenderTarget[0].BlendEnable = true;
	blendStateDestAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendStateDestAlpha.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendStateDestAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
	blendStateDestAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_DEST_ALPHA;
	blendStateDestAlpha.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendStateDestAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStateDestAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStateDestAlphaPremultiplied = blendStateDestAlpha;
	blendStateDestAlphaPremultiplied.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStateSrcAlpha = blendStateDestAlpha;
	blendStateSrcAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALPHA;
	blendStateSrcAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	blendStateSrcAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendStateSrcAlpha.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendStateSrcAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStateSrcAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	D3D12_BLEND_DESC blendStatePrepassUseAppAlpha = blendStateDestAlpha;
	blendStatePrepassUseAppAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALPHA;
	blendStatePrepassUseAppAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	blendStatePrepassUseAppAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendStatePrepassUseAppAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStatePrepassUseAppAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStatePrepassIgnoreAppAlpha = blendStateDestAlpha;
	blendStatePrepassIgnoreAppAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	D3D12_BLEND_DESC blendStateInverseAppAlpha = blendStateDestAlpha;
	blendStateInverseAppAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendStateInverseAppAlpha.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendStateInverseAppAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_DEST_ALPHA;
	blendStateInverseAppAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_INV_DEST_ALPHA;
	blendStateInverseAppAlpha.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_SUBTRACT;
	blendStateInverseAppAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendStateInverseAppAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

	D3D12_BLEND_DESC blendStateDisabled = {};
	blendStateDisabled.RenderTarget[0].BlendEnable = false;
	blendStateDisabled.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;


	D3D12_INPUT_ELEMENT_DESC vertexDesc{};
	vertexDesc.SemanticName = "POSITION";
	vertexDesc.SemanticIndex = 0;
	vertexDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexDesc.InputSlot = 0;
	vertexDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	vertexDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	vertexDesc.InstanceDataStepRate = 0;


	D3D12_DEPTH_STENCIL_DESC depthStencilPrepass{};
	depthStencilPrepass.DepthEnable = m_bUsingDepth;
	depthStencilPrepass.DepthFunc = ((m_blendMode == Masked) ? (m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage == m_bUsingReversedDepth) : m_bUsingReversedDepth) ? D3D12_COMPARISON_FUNC_GREATER_EQUAL :
			D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depthStencilPrepass.DepthWriteMask = m_bWriteDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;

	D3D12_DEPTH_STENCIL_DESC depthStencilMain{};
	depthStencilMain.DepthEnable = m_bUsingDepth;
	depthStencilMain.DepthFunc = m_bUsingReversedDepth ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depthStencilMain.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	D3D12_DEPTH_STENCIL_DESC depthStencilCutout{};
	depthStencilCutout.DepthEnable = m_bUsingDepth;
	depthStencilCutout.DepthFunc = m_bUsingReversedDepth ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	depthStencilCutout.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	D3D12_DEPTH_STENCIL_DESC depthStencilDisabled{};
	depthStencilDisabled.DepthEnable = false;


	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.InputLayout.pInputElementDescs = &vertexDesc;
	psoDesc.InputLayout.NumElements = 1;
	psoDesc.RasterizerState.FrontCounterClockwise = bFlipTriangles ? TRUE : FALSE;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = m_swapchainFormat;
	psoDesc.DSVFormat = m_depthStencilFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.VS = m_bUsingStereo ? passthroughStereoShaderVS : passthroughShaderVS;


	psoDesc.DepthStencilState = depthStencilMain;
	psoDesc.PS = passthroughShaderPS;

	if((m_blendMode == AlphaBlendPremultiplied && !m_bUsingDepth) || m_blendMode == Additive)
	{
		psoDesc.BlendState = blendStateDestAlphaPremultiplied;
	}
	else
	{
		psoDesc.BlendState = blendStateDestAlpha;
	}

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoMainPass))))
	{
		ErrorLog("Error creating main PSO.\n");
		return false;
	}

	
	psoDesc.DepthStencilState = depthStencilCutout;
	psoDesc.BlendState = blendStateDestAlpha;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoCutoutPass))))
	{
		ErrorLog("Error creating cutout PSO.\n");
		return false;
	}


	psoDesc.DepthStencilState = depthStencilPrepass;
	psoDesc.PS = m_blendMode == Masked ? alphaPrepassMaskedShaderPS : alphaPrepassShaderPS;

	if (m_blendMode == Masked)
	{
		psoDesc.BlendState = blendStateDisabled;
	}
	else if (m_bUsingDepth && m_blendMode != Additive)
	{
		psoDesc.BlendState = blendStateInverseAppAlpha;
	}
	else if (m_blendMode == AlphaBlendPremultiplied || m_blendMode == AlphaBlendUnpremultiplied)
	{
		psoDesc.BlendState = blendStatePrepassUseAppAlpha;
	}
	else
	{
		psoDesc.BlendState = blendStatePrepassIgnoreAppAlpha;
	}

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoPrepass))))
	{
		ErrorLog("Error creating prepass PSO.\n");
		return false;
	}

	psoDesc.VS = fullscreenQuadShaderVS;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoMaskedPrepassFullscreen))))
	{
		ErrorLog("Error creating prepass PSO.\n");
		return false;
	}

	psoDesc.DepthStencilState = depthStencilDisabled;
	psoDesc.BlendState = blendStateSrcAlpha;
	psoDesc.VS = fullscreenQuadShaderVS;
	psoDesc.PS = alphaCopyMaskedShaderPS;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoMaskedAlphaCopy))))
	{
		ErrorLog("Error creating masked alpha copy PSO.\n");
		return false;
	}


	psoDesc.DepthStencilState = depthStencilPrepass;
	psoDesc.BlendState = blendStateDestAlpha;
	psoDesc.RasterizerState.DepthBias = 16;
	psoDesc.VS = passthroughShaderVS;
	psoDesc.PS = passthroughShaderPS;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoHoleFillPass))))
	{
		ErrorLog("Error creating hole fill PSO.\n");
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

	m_swapchainFormat = (DXGI_FORMAT)swapchainInfo.format;

	// The RTV and SRV are set to use size 1 arrays to support both single and array for passed targets.
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDesc = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvCPUDesc.ptr += bufferIndex * m_RTVHeapDescSize;

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


void PassthroughRendererDX12::InitDepthBuffer(const ERenderEye eye, void* depthBuffer, const uint32_t imageIndex, const XrSwapchainCreateInfo& swapchainInfo)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;
	if (m_depthStencils[bufferIndex].Get() == (ID3D12Resource*)depthBuffer)
	{
		return;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = (DXGI_FORMAT)swapchainInfo.format;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.FirstArraySlice = swapchainInfo.arraySize > 1 ? viewIndex : 0;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUDesc = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
	dsvCPUDesc.ptr += bufferIndex * m_DSVHeapDescSize;

	m_d3dDevice->CreateDepthStencilView((ID3D12Resource*)depthBuffer, &dsvDesc, dsvCPUDesc);

	m_depthStencils[bufferIndex] = (ID3D12Resource*)depthBuffer;
	m_depthStencilFormat = (DXGI_FORMAT)swapchainInfo.format;
}


void PassthroughRendererDX12::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize, const uint32_t undistortedWidth, const uint32_t undistortedHeight, const uint32_t undistortedBufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;

	m_cameraUndistortedTextureWidth = undistortedWidth;
	m_cameraUndistortedTextureHeight = undistortedHeight;
	m_cameraUndistortedFrameBufferSize = undistortedBufferSize;
}


void PassthroughRendererDX12::GenerateMesh()
{
	MeshCreateCylinder(m_cylinderMesh, NUM_MESH_BOUNDARY_VERTICES);

	uint32_t bufferSize = (uint32_t) (m_cylinderMesh.vertices.size() * sizeof(VertexFormatBasic));

	m_cylinderMeshVertexBuffer = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_DEFAULT);
	m_cylinderMeshVertexBufferUpload = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_UPLOAD);

	void* mappedData;
	const D3D12_RANGE readRange{ 0, 0 };
	
	m_cylinderMeshVertexBufferUpload->Map(0, &readRange, &mappedData);
	memcpy(mappedData, m_cylinderMesh.vertices.data(), bufferSize);
	m_cylinderMeshVertexBufferUpload->Unmap(0, nullptr);

	m_commandList->CopyBufferRegion(m_cylinderMeshVertexBuffer.Get(), 0, m_cylinderMeshVertexBufferUpload.Get(), 0, bufferSize);
	TransitionResource(m_commandList.Get(), m_cylinderMeshVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	bufferSize = (uint32_t)(m_cylinderMesh.triangles.size() * sizeof(MeshTriangle));

	m_cylinderMeshIndexBuffer = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_DEFAULT);
	m_cylinderMeshIndexBufferUpload = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_UPLOAD);

	m_cylinderMeshIndexBufferUpload->Map(0, &readRange, &mappedData);
	memcpy(mappedData, m_cylinderMesh.triangles.data(), bufferSize);
	m_cylinderMeshIndexBufferUpload->Unmap(0, nullptr);

	m_commandList->CopyBufferRegion(m_cylinderMeshIndexBuffer.Get(), 0, m_cylinderMeshIndexBufferUpload.Get(), 0, bufferSize);
	TransitionResource(m_commandList.Get(), m_cylinderMeshIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
}


void PassthroughRendererDX12::GenerateDepthMesh(uint32_t width, uint32_t height)
{
	m_bUseHexagonGridMesh ? MeshCreateHexGrid(m_gridMesh, width, height) : MeshCreateGrid(m_gridMesh, width, height);

	uint32_t bufferSize = (uint32_t)(m_gridMesh.vertices.size() * sizeof(VertexFormatBasic));

	m_gridMeshVertexBuffer = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_DEFAULT);
	m_gridMeshVertexBufferUpload = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_UPLOAD);

	void* mappedData;
	const D3D12_RANGE readRange{ 0, 0 };

	m_gridMeshVertexBufferUpload->Map(0, &readRange, &mappedData);
	memcpy(mappedData, m_gridMesh.vertices.data(), bufferSize);
	m_gridMeshVertexBufferUpload->Unmap(0, nullptr);

	m_commandList->CopyBufferRegion(m_gridMeshVertexBuffer.Get(), 0, m_gridMeshVertexBufferUpload.Get(), 0, bufferSize);
	TransitionResource(m_commandList.Get(), m_gridMeshVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	bufferSize = (uint32_t)(m_gridMesh.triangles.size() * sizeof(MeshTriangle));

	m_gridMeshIndexBuffer = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_DEFAULT);
	m_gridMeshIndexBufferUpload = CreateBuffer(m_d3dDevice.Get(), bufferSize, D3D12_HEAP_TYPE_UPLOAD);

	m_gridMeshIndexBufferUpload->Map(0, &readRange, &mappedData);
	memcpy(mappedData, m_gridMesh.triangles.data(), bufferSize);
	m_gridMeshIndexBufferUpload->Unmap(0, nullptr);

	m_commandList->CopyBufferRegion(m_gridMeshIndexBuffer.Get(), 0, m_gridMeshIndexBufferUpload.Get(), 0, bufferSize);
	TransitionResource(m_commandList.Get(), m_gridMeshIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
}





void PassthroughRendererDX12::RenderPassthroughFrame(const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, int leftSwapchainIndex, int rightSwapchainIndex, int leftDepthSwapchainIndex, int rightDepthSwapchainIndex, std::shared_ptr<DepthFrame> depthFrame, UVDistortionParameters& distortionParams, FrameRenderParameters& renderParams)
{
	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Core& coreConf = m_configManager->GetConfig_Core();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();
	Config_Depth& depthConf = m_configManager->GetConfig_Depth();

	if (mainConf.ProjectionMode == Projection_StereoReconstruction && !depthFrame->bIsValid)
	{
		return;
	}

	bool bCompositeDepth = renderParams.bEnableDepthBlending && m_depthStencils[0].Get() != nullptr;
	bool bDepthWrtite = depthConf.DepthWriteOutput && depthConf.DepthReadFromApplication;
	bool bUseReversedDepth = (m_blendMode == Masked) ? coreConf.CoreForceMaskedUseCameraImage == frame->bHasReversedDepth : frame->bHasReversedDepth;

	if (!m_psoMainPass.Get() || m_blendMode != blendMode || m_bUsingStereo != (mainConf.ProjectionMode == Projection_StereoReconstruction) || m_bUsingDepth != bCompositeDepth || m_bUsingReversedDepth != bUseReversedDepth || m_bWriteDepth != bDepthWrtite)
	{
		m_blendMode = blendMode;
		m_bUsingStereo = (mainConf.ProjectionMode == Projection_StereoReconstruction);
		m_bUsingDepth = bCompositeDepth;
		m_bUsingReversedDepth = bUseReversedDepth;
		m_bWriteDepth = bDepthWrtite;

		if (!InitPipeline(frame->bIsRenderingMirrored))
		{
			return;
		}
	}

	ComPtr<ID3D12CommandAllocator> commandAllocator = m_commandAllocators[m_frameIndex];

	commandAllocator->Reset();
	m_commandList->Reset(commandAllocator.Get(), nullptr);
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetDescriptorHeaps(1, m_CBVSRVHeap.GetAddressOf());

	{
		std::shared_lock readLock(distortionParams.readWriteMutex);

		if (mainConf.ProjectionMode != Projection_RoomView2D &&
			(!m_uvDistortionMap.Get() || m_fovScale != distortionParams.fovScale))
		{
			m_fovScale = distortionParams.fovScale;
			SetupUVDistortionMap(distortionParams.uvDistortionMap);
		}
	}

	if (mainConf.ProjectionMode != Projection_RoomView2D)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE uvDistortionSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		uvDistortionSRVHandle.ptr += INDEX_SRV_UV_DISTORTION * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(4, uvDistortionSRVHandle);
	}

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		if (depthFrame->disparityTextureSize[0] != m_disparityMapWidth || stereoConf.StereoUseHexagonGridMesh != m_bUseHexagonGridMesh)
		{
			m_disparityMapWidth = depthFrame->disparityTextureSize[0];
			m_bUseHexagonGridMesh = stereoConf.StereoUseHexagonGridMesh;
			SetupDisparityMap(depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1]);
			GenerateDepthMesh(depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1]);
		}
		else
		{
			TransitionResource(m_commandList.Get(), m_disparityMap[m_frameIndex].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		}

		int rowPitch = Align(depthFrame->disparityTextureSize[0] * sizeof(uint16_t) * 2, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		size_t disparityTextureSize = Align(depthFrame->disparityTextureSize[1] * rowPitch, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

		UploadTexture(m_commandList.Get(), m_disparityMap[m_frameIndex].Get(), m_disparityMapUploadHeap.Get(), disparityTextureSize * m_frameIndex, (uint8_t*)depthFrame->disparityMap->data(), depthFrame->disparityTextureSize[0], depthFrame->disparityTextureSize[1], DXGI_FORMAT_R16G16_SNORM, sizeof(uint16_t) * 2, 0);

		TransitionResource(m_commandList.Get(), m_disparityMap[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		D3D12_GPU_DESCRIPTOR_HANDLE disparitySRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		disparitySRVHandle.ptr += (INDEX_SRV_DISPARITY_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(8, disparitySRVHandle);
	}

	VSPassConstantBuffer* vsPassBuffer = (VSPassConstantBuffer*)m_vsPassConstantBufferCPUData[m_frameIndex];
	vsPassBuffer->worldToCameraFrameProjectionLeft = frame->worldToCameraProjectionLeft;
	vsPassBuffer->worldToCameraFrameProjectionRight = frame->worldToCameraProjectionRight;
	vsPassBuffer->worldToPrevCameraFrameProjectionLeft = frame->prevWorldToCameraProjectionLeft;
	vsPassBuffer->worldToPrevCameraFrameProjectionRight = frame->prevWorldToCameraProjectionRight;

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		vsPassBuffer->worldToPrevDepthFrameProjectionLeft = depthFrame->prevDispWorldToCameraProjectionLeft;
		vsPassBuffer->worldToPrevDepthFrameProjectionRight = depthFrame->prevDispWorldToCameraProjectionRight;
		vsPassBuffer->depthFrameViewToWorldLeft = depthFrame->disparityViewToWorldLeft;
		vsPassBuffer->depthFrameViewToWorldRight = depthFrame->disparityViewToWorldRight;
		vsPassBuffer->prevDepthFrameViewToWorldLeft = depthFrame->prevDisparityViewToWorldLeft;
		vsPassBuffer->prevDepthFrameViewToWorldRight = depthFrame->prevDisparityViewToWorldRight;
		vsPassBuffer->disparityToDepth = depthFrame->disparityToDepth;
		vsPassBuffer->disparityTextureSize[0] = depthFrame->disparityTextureSize[0];
		vsPassBuffer->disparityTextureSize[1] = depthFrame->disparityTextureSize[1];
		vsPassBuffer->disparityDownscaleFactor = depthFrame->disparityDownscaleFactor;
		vsPassBuffer->minDisparity = depthFrame->minDisparity;
		vsPassBuffer->maxDisparity = depthFrame->maxDisparity;
		vsPassBuffer->cutoutFactor = stereoConf.StereoCutoutFactor;
		vsPassBuffer->cutoutOffset = stereoConf.StereoCutoutOffset;
		vsPassBuffer->cutoutFilterWidth = stereoConf.StereoCutoutFilterWidth;
		vsPassBuffer->disparityFilterWidth = stereoConf.StereoDisparityFilterWidth;
		vsPassBuffer->bProjectBorders = !stereoConf.StereoReconstructionFreeze;
		vsPassBuffer->bFindDiscontinuities = stereoConf.StereoCutoutEnabled;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE vsPassCBVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	vsPassCBVHandle.ptr += (INDEX_CBV_VS_PASS_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(7, vsPassCBVHandle);

	UINT numIndices;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};

	if (mainConf.ProjectionMode == Projection_StereoReconstruction)
	{
		numIndices = (UINT)m_gridMesh.triangles.size() * 3;

		vertexBufferView.BufferLocation = m_gridMeshVertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = (UINT)m_gridMesh.vertices.size() * sizeof(VertexFormatBasic);
		vertexBufferView.StrideInBytes = sizeof(VertexFormatBasic);

		indexBufferView.BufferLocation = m_gridMeshIndexBuffer->GetGPUVirtualAddress();
		indexBufferView.SizeInBytes = (UINT)m_gridMesh.triangles.size() * sizeof(MeshTriangle);
		indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}
	else
	{
		numIndices = (UINT)m_cylinderMesh.triangles.size() * 3;

		vertexBufferView.BufferLocation = m_cylinderMeshVertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = (UINT)m_cylinderMesh.vertices.size() * sizeof(VertexFormatBasic);
		vertexBufferView.StrideInBytes = sizeof(VertexFormatBasic);

		indexBufferView.BufferLocation = m_cylinderMeshIndexBuffer->GetGPUVirtualAddress();
		indexBufferView.SizeInBytes = (UINT)m_cylinderMesh.triangles.size() * sizeof(MeshTriangle);
		indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	m_commandList->IASetIndexBuffer(&indexBufferView);
	

	bool bGotDebugTexture = false;

	if (mainConf.DebugTexture != DebugTexture_None)
	{
		DebugTexture& texture = m_configManager->GetDebugTexture();
		std::lock_guard<std::mutex> readlock(texture.RWMutex);

		if (texture.CurrentTexture == mainConf.DebugTexture)
		{
			if (!m_debugTexture.Get() || texture.CurrentTexture != m_selectedDebugTexture || texture.bDimensionsUpdated)
			{
				SetupDebugTexture(texture);

				m_selectedDebugTexture = texture.CurrentTexture;
				texture.bDimensionsUpdated = false;
			}

			if (m_debugTextureUploadHeap.Get())
			{
				TransitionResource(m_commandList.Get(), m_debugTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

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

				UploadTexture(m_commandList.Get(), m_debugTexture.Get(), m_debugTextureUploadHeap.Get(), 0, texture.Texture.data(), texture.Width, texture.Height, format, texture.PixelSize, 0);

				TransitionResource(m_commandList.Get(), m_debugTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				bGotDebugTexture = true;
			}
		}
	}

	if (bGotDebugTexture)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE debugTextureSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		debugTextureSRVHandle.ptr += INDEX_SRV_DEBUG_TEXTURE * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, debugTextureSRVHandle);
	}
	else if(frame->header.eFrameType == vr::VRTrackedCameraFrameType_Distorted)
	{
		// Upload camera frame
		TransitionResource(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

		UploadTexture(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), m_frameResUploadHeap.Get(), m_cameraFrameBufferSize * m_frameIndex, frame->frameBuffer->data(), m_cameraTextureWidth, m_cameraTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);

		TransitionResource(m_commandList.Get(), m_cameraFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		D3D12_GPU_DESCRIPTOR_HANDLE frameSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		frameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, frameSRVHandle);
	}
	else
	{
		// Upload camera frame
		TransitionResource(m_commandList.Get(), m_cameraUndisortedFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

		UploadTexture(m_commandList.Get(), m_cameraUndisortedFrameRes[m_frameIndex].Get(), m_undistortedFrameResUploadHeap.Get(), m_cameraUndistortedFrameBufferSize* m_frameIndex, frame->frameBuffer->data(), m_cameraUndistortedTextureWidth, m_cameraUndistortedTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 0);

		TransitionResource(m_commandList.Get(), m_cameraUndisortedFrameRes[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		D3D12_GPU_DESCRIPTOR_HANDLE frameSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		frameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_UNDISTORTED_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(3, frameSRVHandle);
	}

	PSPassConstantBuffer* psPassBuffer = (PSPassConstantBuffer*)m_psPassConstantBufferCPUData[m_frameIndex];
	psPassBuffer->depthRange = XrVector2f(NEAR_PROJECTION_DISTANCE, mainConf.ProjectionDistanceFar);
	psPassBuffer->depthCutoffRange = XrVector2f(renderParams.DepthRangeMin, renderParams.DepthRangeMax);
	psPassBuffer->opacity = mainConf.PassthroughOpacity;
	psPassBuffer->brightness = mainConf.Brightness;
	psPassBuffer->contrast = mainConf.Contrast;
	psPassBuffer->saturation = mainConf.Saturation;
	psPassBuffer->sharpness = mainConf.Sharpness;
	psPassBuffer->bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;
	psPassBuffer->bDebugDepth = mainConf.DebugDepth;
	psPassBuffer->bDebugValidStereo = mainConf.DebugStereoValid;
	psPassBuffer->bUseFisheyeCorrection = mainConf.ProjectionMode != Projection_RoomView2D;
	psPassBuffer->bUseDepthCutoffRange = renderParams.bEnableDepthRange;
	psPassBuffer->bClampCameraFrame = m_configManager->GetConfig_Camera().ClampCameraFrame;

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
		maskedBuffer->bMaskedInvert = coreConf.CoreForceMaskedInvertMask;

		D3D12_GPU_DESCRIPTOR_HANDLE maskedCBVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		maskedCBVHandle.ptr += (INDEX_CBV_PS_MASKED_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(2, maskedCBVHandle);

		RenderMaskedPrepassView(LEFT_EYE, leftSwapchainIndex, layer, frame, numIndices);
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode, numIndices);
		RenderMaskedPrepassView(RIGHT_EYE, rightSwapchainIndex, layer, frame, numIndices);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode, numIndices);
	}
	else
	{
		RenderPassthroughView(LEFT_EYE, leftSwapchainIndex, layer, frame, blendMode, numIndices);
		RenderPassthroughView(RIGHT_EYE, rightSwapchainIndex, layer, frame, blendMode, numIndices);
	}
	RenderFrameFinish();
}

void PassthroughRendererDX12::RenderPassthroughView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, EPassthroughBlendMode blendMode, UINT numIndices)
{
	if (imageIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	ID3D12Resource* rendertarget = m_renderTargets[bufferIndex].Get();

	if (!rendertarget) { return; }

	bool bCompositeDepth = m_bUsingDepth && m_depthStencils[bufferIndex].Get() != nullptr;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D12_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D12_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bufferIndex * m_RTVHeapDescSize;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
	dsvHandle.ptr += bufferIndex * m_DSVHeapDescSize;

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	Config_Main& mainConf = m_configManager->GetConfig_Main();
	Config_Stereo& stereoConf = m_configManager->GetConfig_Stereo();

	VSViewConstantBuffer* vsViewBuffer = (VSViewConstantBuffer*)m_vsViewConstantBufferCPUData[bufferIndex];
	vsViewBuffer->worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	vsViewBuffer->disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
	vsViewBuffer->projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
	vsViewBuffer->projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer->floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer->cameraViewIndex = (eye == LEFT_EYE) ? 0 : 1;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvVSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvVSHandle.ptr += (INDEX_CBV_VS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(6, cbvVSHandle);


	PSViewConstantBuffer* psViewBuffer = (PSViewConstantBuffer*)m_psViewConstantBufferCPUData[bufferIndex];
	psViewBuffer->frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer->rtArrayIndex = m_frameIndex;
	psViewBuffer->bDoCutout = false;
	psViewBuffer->bPremultiplyAlpha = (blendMode == AlphaBlendPremultiplied) && !m_bUsingDepth;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvPSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvPSHandle.ptr += (INDEX_CBV_PS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(1, cbvPSHandle);

	// Extra draw if we need to preadjust the alpha.
	if (blendMode != Masked && ((blendMode != AlphaBlendPremultiplied && blendMode != AlphaBlendUnpremultiplied) || m_configManager->GetConfig_Main().PassthroughOpacity < 1.0f || m_bUsingDepth))
	{
		m_commandList->SetPipelineState(m_psoPrepass.Get());
		m_commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
	}


	// Draw main pass
	m_commandList->SetPipelineState(m_psoMainPass.Get());
	m_commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);



	// Draw the other stereo camera on occluded areas
	if (stereoConf.StereoCutoutEnabled)
	{
		float secondaryWidthFactor = 0.6f;
		int scissorStart = (eye == LEFT_EYE) ? (int)(rect.extent.width * (1.0f - secondaryWidthFactor)) : 0;
		int scissorEnd = (eye == LEFT_EYE) ? rect.extent.width : (int)(rect.extent.width * secondaryWidthFactor);
		D3D12_RECT crossScissor = { rect.offset.x + scissorStart, rect.offset.y, rect.offset.x + scissorEnd, rect.offset.y + rect.extent.height };
		m_commandList->RSSetScissorRects(1, &crossScissor);

		int crossBufferIndex = NUM_SWAPCHAINS * 2 + bufferIndex;
		VSViewConstantBuffer* vsCrossViewBuffer = (VSViewConstantBuffer*)m_vsViewConstantBufferCPUData[crossBufferIndex];
		
		vsCrossViewBuffer->worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
		vsCrossViewBuffer->disparityUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, StereoHorizontalLayout);
		vsCrossViewBuffer->projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
		vsCrossViewBuffer->projectionDistance = mainConf.ProjectionDistanceFar;
		vsCrossViewBuffer->floorHeightOffset = mainConf.FloorHeightOffset;
		vsCrossViewBuffer->cameraViewIndex = (eye != LEFT_EYE) ? 0 : 1;

		D3D12_GPU_DESCRIPTOR_HANDLE cbvCrossVSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		cbvCrossVSHandle.ptr += (INDEX_CBV_VS_VIEW_0 + crossBufferIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(6, cbvCrossVSHandle);
		
		PSViewConstantBuffer* psCrossViewBuffer = (PSViewConstantBuffer*)m_psViewConstantBufferCPUData[crossBufferIndex];
		psCrossViewBuffer->frameUVBounds = GetFrameUVBounds(eye == LEFT_EYE ? RIGHT_EYE : LEFT_EYE, frame->frameLayout);
		psCrossViewBuffer->rtArrayIndex = m_frameIndex;
		psCrossViewBuffer->bDoCutout = true;
		psCrossViewBuffer->bPremultiplyAlpha = false;

		D3D12_GPU_DESCRIPTOR_HANDLE cbvCrossPSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
		cbvCrossPSHandle.ptr += (INDEX_CBV_PS_VIEW_0 + crossBufferIndex) * m_CBVSRVHeapDescSize;
		m_commandList->SetGraphicsRootDescriptorTable(1, cbvCrossPSHandle);

		m_commandList->SetPipelineState(m_psoCutoutPass.Get());
		m_commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
	}



	// Draw cylinder mesh to fill out any holes
	if (stereoConf.StereoDrawBackground && mainConf.ProjectionMode == Projection_StereoReconstruction && !stereoConf.StereoReconstructionFreeze)
	{
		m_commandList->RSSetScissorRects(1, &scissor);
		m_commandList->SetGraphicsRootDescriptorTable(6, cbvVSHandle);
		m_commandList->SetGraphicsRootDescriptorTable(1, cbvPSHandle);

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		D3D12_INDEX_BUFFER_VIEW indexBufferView{};

		vertexBufferView.BufferLocation = m_cylinderMeshVertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = (UINT)m_cylinderMesh.vertices.size() * sizeof(VertexFormatBasic);
		vertexBufferView.StrideInBytes = sizeof(VertexFormatBasic);

		indexBufferView.BufferLocation = m_cylinderMeshIndexBuffer->GetGPUVirtualAddress();
		indexBufferView.SizeInBytes = (UINT)m_cylinderMesh.triangles.size() * sizeof(MeshTriangle);
		indexBufferView.Format = DXGI_FORMAT_R32_UINT;

		m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		m_commandList->IASetIndexBuffer(&indexBufferView);


		m_commandList->SetPipelineState(m_psoHoleFillPass.Get());
		m_commandList->DrawIndexedInstanced((UINT)m_cylinderMesh.triangles.size() * 3, 1, 0, 0, 0);


		vertexBufferView.BufferLocation = m_gridMeshVertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = (UINT)m_gridMesh.vertices.size() * sizeof(VertexFormatBasic);
		vertexBufferView.StrideInBytes = sizeof(VertexFormatBasic);

		indexBufferView.BufferLocation = m_gridMeshIndexBuffer->GetGPUVirtualAddress();
		indexBufferView.SizeInBytes = (UINT)m_gridMesh.triangles.size() * sizeof(MeshTriangle);
		indexBufferView.Format = DXGI_FORMAT_R32_UINT;

		m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		m_commandList->IASetIndexBuffer(&indexBufferView);
	}
}


void PassthroughRendererDX12::RenderMaskedPrepassView(const ERenderEye eye, const int32_t imageIndex, const XrCompositionLayerProjection* layer, CameraFrame* frame, UINT numIndices)
{
	if (imageIndex < 0) { return; }

	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + imageIndex;

	ID3D12Resource* rendertarget = m_renderTargets[bufferIndex].Get();

	if (!rendertarget) { return; }

	bool bCompositeDepth = m_bUsingDepth && m_depthStencils[bufferIndex].Get() != nullptr;

	XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D12_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D12_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	Config_Main& mainConf = m_configManager->GetConfig_Main();

	VSViewConstantBuffer* vsViewBuffer = (VSViewConstantBuffer*)m_vsViewConstantBufferCPUData[bufferIndex];
	vsViewBuffer->worldToHMDProjection = (eye == LEFT_EYE) ? frame->worldToHMDProjectionLeft : frame->worldToHMDProjectionRight;
	vsViewBuffer->disparityUVBounds = GetFrameUVBounds(eye, StereoHorizontalLayout);
	vsViewBuffer->projectionOriginWorld = (eye == LEFT_EYE) ? frame->projectionOriginWorldLeft : frame->projectionOriginWorldRight;
	vsViewBuffer->projectionDistance = mainConf.ProjectionDistanceFar;
	vsViewBuffer->floorHeightOffset = mainConf.FloorHeightOffset;
	vsViewBuffer->cameraViewIndex = (eye == LEFT_EYE) ? 0 : 1;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvVSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvVSHandle.ptr += (INDEX_CBV_VS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(6, cbvVSHandle);

	bool bSingleStereoRenderTarget = false;

	PSViewConstantBuffer* psViewBuffer = (PSViewConstantBuffer*)m_psViewConstantBufferCPUData[bufferIndex];
	// Draw the correct half for single framebuffer views.
	if (abs(layer->views[0].subImage.imageRect.offset.x - layer->views[1].subImage.imageRect.offset.x) > layer->views[0].subImage.imageRect.extent.width / 2)
	{
		psViewBuffer->prepassUVBounds = { (eye == LEFT_EYE) ? 0.0f : 0.5f, 0.0f,
			(eye == LEFT_EYE) ? 0.5f : 1.0f, 1.0f };
		bSingleStereoRenderTarget = true;
	}
	else
	{
		psViewBuffer->prepassUVBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
	}
	psViewBuffer->frameUVBounds = GetFrameUVBounds(eye, frame->frameLayout);
	psViewBuffer->rtArrayIndex = layer->views[viewIndex].subImage.imageArrayIndex;
	psViewBuffer->bDoCutout = false;
	psViewBuffer->bPremultiplyAlpha = false;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvPSHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	cbvPSHandle.ptr += (INDEX_CBV_PS_VIEW_0 + bufferIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(1, cbvPSHandle);

	int32_t intermediateRTIndex = bSingleStereoRenderTarget ? imageIndex : bufferIndex;
	int32_t rtWidth = bSingleStereoRenderTarget ? rect.extent.width * 2 : rect.extent.width;

	// Recreate the intermediate rendertarget if it can't hold the entire viewport.
	if ((!bSingleStereoRenderTarget || eye == LEFT_EYE) &&
		(!m_intermediateRenderTargets[intermediateRTIndex].Get()
		|| (int32_t)m_intermediateRenderTargets[intermediateRTIndex].Get()->GetDesc().Width < rtWidth
		|| (int32_t)m_intermediateRenderTargets[intermediateRTIndex].Get()->GetDesc().Height < rect.extent.height))
	{
		SetupIntermediateRenderTarget(intermediateRTIndex, rtWidth, rect.extent.height);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_intermediateRTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += intermediateRTIndex * m_RTVHeapDescSize;

	if (eye == LEFT_EYE || !bSingleStereoRenderTarget)
	{
		float clearColor[4] = { m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage ? 1.0f : 0, 0, 0, 0 };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE cameraFrameSRVHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();

	if (m_configManager->GetConfig_Main().DebugTexture != DebugTexture_None)
	{
		cameraFrameSRVHandle.ptr += INDEX_SRV_DEBUG_TEXTURE * m_CBVSRVHeapDescSize;
	}
	else if(frame->header.eFrameType == vr::VRTrackedCameraFrameType_Distorted)
	{
		cameraFrameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
	}
	else
	{
		cameraFrameSRVHandle.ptr += (INDEX_SRV_CAMERAFRAME_UNDISTORTED_0 + m_frameIndex) * m_CBVSRVHeapDescSize;
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

	

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
	dsvHandle.ptr += bufferIndex * m_DSVHeapDescSize;

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);


	if (bCompositeDepth || m_configManager->GetConfig_Core().CoreForceMaskedUseCameraImage)
	{
		m_commandList->SetPipelineState(m_psoPrepass.Get());
		m_commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
	}
	else
	{
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_commandList->SetPipelineState(m_psoMaskedPrepassFullscreen.Get());
		m_commandList->DrawInstanced(3, 1, 0, 0);
	}


	rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bufferIndex * m_RTVHeapDescSize;
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	TransitionResource(m_commandList.Get(), m_intermediateRenderTargets[intermediateRTIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);


	D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (INDEX_SRV_MASKED_INTERMEDIATE_0 + intermediateRTIndex) * m_CBVSRVHeapDescSize;
	m_commandList->SetGraphicsRootDescriptorTable(5, srvHandle);


	// Copy alpha to main render target
	m_commandList->SetPipelineState(m_psoMaskedAlphaCopy.Get());
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_commandList->DrawInstanced(3, 1, 0, 0);


	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->SetGraphicsRootDescriptorTable(3, cameraFrameSRVHandle);

	TransitionResource(m_commandList.Get(), m_intermediateRenderTargets[intermediateRTIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
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