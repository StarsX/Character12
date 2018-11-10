//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "CharacterX.h"

using namespace std;
using namespace XUSG;

enum VertexShaderX
{
	VS_TRIANGLE,

	NUM_VS
};

enum PixelShaderX
{
	PS_TRIANGLE,

	NUM_PS
};

CharacterX::CharacterX(uint32_t width, uint32_t height, std::wstring name) :
	DXFramework(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<long>(width), static_cast<long>(height))//,
	//m_rtvDescriptorSize(0)
{
}

void CharacterX::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void CharacterX::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
			));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
			));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
		));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_descriptorTablePool = make_shared<DescriptorTablePool>();
	m_descriptorTablePool->SetDevice(m_device);

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvPool)));
	}

	// Create frame resources.
	{
		const auto strideRtv = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		Descriptor rtv(m_rtvPool->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV and a command allocator for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtv);

			Util::DescriptorTable rtvTable;
			rtvTable.SetDescriptors(0, 1, &rtv);
			m_rtvTables[n] = rtvTable.GetRtvTable(*m_descriptorTablePool);

			rtv.Offset(strideRtv);

			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
		}
	}

	// Create a DSV
	m_depth.Create(m_device, m_width, m_height, DXGI_FORMAT_D24_UNORM_S8_UINT,
			D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
}

// Load the sample assets.
void CharacterX::LoadAssets()
{
	m_shaderPool = make_shared<Shader::Pool>();
	m_pipelinePool = make_shared<Graphics::Pipeline::Pool>(m_device);
	
	// Create the root signature.
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::CBV, 1, 0);
		pipelineLayout.SetRange(1, DescriptorType::SRV, _countof(m_textures), 0);
		pipelineLayout.SetRange(2, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout.SetShaderStage(0, Shader::Stage::VS);
		pipelineLayout.SetShaderStage(1, Shader::Stage::PS);
		pipelineLayout.SetShaderStage(2, Shader::Stage::PS);
		m_pipelineLayout = pipelineLayout.GetPipelineLayout(*m_pipelinePool, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		m_shaderPool->CreateShader(Shader::Stage::VS, VS_TRIANGLE, L"VertexShader.cso");
		m_shaderPool->CreateShader(Shader::Stage::PS, PS_TRIANGLE, L"PixelShader.cso");

		m_shaderPool->CreateShader(Shader::Stage::VS, VS_BASE_PASS, L"VSBasePass.cso");
		m_shaderPool->CreateShader(Shader::Stage::PS, PS_BASE_PASS, L"PSBasePass.cso");
		m_shaderPool->CreateShader(Shader::Stage::CS, CS_SKINNING, L"CSSkinning.cso");

		// Define the vertex input layout.
		InputElementTable inputElementDescs =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		m_inputLayout = m_pipelinePool->CreateInputLayout(inputElementDescs);

		// Describe and create the graphics pipeline state object (PSO).
		Graphics::State state;
		state.IASetInputLayout(m_inputLayout);
		state.SetPipelineLayout(m_pipelineLayout);
		state.SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, VS_TRIANGLE));
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_TRIANGLE));
		//state.DSSetState(Graphics::DepthStencilPreset::DEPTH_STENCIL_NONE, m_pipelinePool);
		state.IASetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		state.OMSetNumRenderTargets(1);
		state.OMSetRTVFormat(0, DXGI_FORMAT_R8G8B8A8_UNORM);
		state.OMSetDSVFormat(DXGI_FORMAT_D24_UNORM_S8_UINT);
		m_pipelineState = state.GetPipeline(*m_pipelinePool);
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

	// Load character asset
	m_charInputLayout = Character::InitLayout(*m_pipelinePool);
	const auto characterMesh = Character::LoadSDKMesh(m_device, L"Media/Bright/Stars.sdkmesh", L"Media/Bright/Stars.sdkmesh_anim");
	m_character = make_unique<Character>(m_device, m_commandList);
	m_character->Init(m_charInputLayout, characterMesh, m_shaderPool, m_pipelinePool, m_descriptorTablePool);

	// Create the vertex buffer.
	Resource vertexUpload;
	{
		// Define the geometry for a triangle.
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 0.5f, 0.0f } },
			{ { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f } },
			{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f } }
		};
		const uint32_t vertexBufferSize = sizeof(triangleVertices);

		m_vertexBuffer.Create(m_device, vertexBufferSize, sizeof(Vertex), D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
		m_vertexBuffer.Upload(m_commandList, vertexUpload, triangleVertices);
	}

	// Create the index buffer.
	Resource indexUpload;
	{
		// Define the geometry for a triangle.
		uint16_t triangleIndices[] = { 0, 1, 2 };
		const uint32_t indexBufferSize = sizeof(triangleIndices);

		m_indexBuffer.Create(m_device, indexBufferSize, DXGI_FORMAT_R16_UINT);
		m_indexBuffer.Upload(m_commandList, indexUpload, triangleIndices);
	}

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	Resource textureUploads[_countof(m_textures)];

	// Create the constant buffer.
	{
		m_constantBuffer.Create(m_device, 1024 * 64, sizeof(XMFLOAT4));

		Util::DescriptorTable cbvTable;
		cbvTable.SetDescriptors(0, 1, &m_constantBuffer.GetCBV());
		m_cbvTable = cbvTable.GetCbvSrvUavTable(*m_descriptorTablePool);
	}

	// Create the textures.
	{
		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the Texture2D.
		vector<Descriptor> srvs(_countof(m_textures));
		for (auto i = 0; i < _countof(m_textures); ++i)
		{
			const auto texture = GenerateTextureData(i);

			m_textures[i].Create(m_device, TextureWidth, TextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
			m_textures[i].Upload(m_commandList, textureUploads[i], texture.data(), TexturePixelSize);
			srvs[i] = m_textures[i].GetSRV();
		}
		
		Util::DescriptorTable srvTable;
		srvTable.SetDescriptors(0, _countof(m_textures), srvs.data());
		m_srvTable = srvTable.GetCbvSrvUavTable(*m_descriptorTablePool);
	}

	// Create the sampler
	{
		Util::DescriptorTable samplerTable;
		const auto samplerAnisoWrap = SamplerPreset::ANISOTROPIC_WRAP;
		samplerTable.SetSamplers(0, 1, &samplerAnisoWrap, *m_descriptorTablePool);
		m_samplerTable = samplerTable.GetSamplerTable(*m_descriptorTablePool);
	}
	
	// Close the command list and execute it to begin the initial GPU setup.
	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}
}

// Generate a simple black and white checkerboard texture.
vector<uint8_t> CharacterX::GenerateTextureData(uint32_t subDivLevel)
{
	const auto rowPitch = TextureWidth * TexturePixelSize;
	const auto cellPitch = rowPitch >> subDivLevel;			// The width of a cell in the checkboard texture.
	const auto cellHeight = TextureWidth >> subDivLevel;	// The height of a cell in the checkerboard texture.
	const auto textureSize = rowPitch * TextureHeight;

	vector<uint8_t> data(textureSize);
	uint8_t* pData = &data[0];

	for (auto n = 0u; n < textureSize; n += TexturePixelSize)
	{
		auto x = n % rowPitch;
		auto y = n / rowPitch;
		auto i = x / cellPitch;
		auto j = y / cellHeight;

		if (i % 2 == j % 2)
		{
			pData[n] = 0x00;		// R
			pData[n + 1] = 0x00;	// G
			pData[n + 2] = 0x00;	// B
			pData[n + 3] = 0xff;	// A
		}
		else
		{
			pData[n] = 0xff;		// R
			pData[n + 1] = 0xff;	// G
			pData[n + 2] = 0xff;	// B
			pData[n + 3] = 0xff;	// A
		}
	}

	return data;
}

// Update frame-based values.
void CharacterX::OnUpdate()
{
	const float translationSpeed = 0.001f;
	const float offsetBounds = 1.25f;

	m_cbData_Offset.x += translationSpeed;
	if (m_cbData_Offset.x > offsetBounds)
	{
		m_cbData_Offset.x = -offsetBounds;
	}

	// Map and initialize the constant buffer. We don't unmap this until the
	// app closes. Keeping things mapped for the lifetime of the resource is okay.
	const auto pCbData = reinterpret_cast<XMFLOAT4*>(m_constantBuffer.Map());
	*pCbData = m_cbData_Offset;

	m_character->FrameMove(0.0);
}

// Render the scene.
void CharacterX::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(0, 0));

	MoveToNextFrame();
}

void CharacterX::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);
}

void CharacterX::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

	// Skinning
	m_character->Skinning(true);

	// Set necessary state.
	m_commandList->SetPipelineState(m_pipelineState.Get());
	m_commandList->SetGraphicsRootSignature(m_pipelineLayout.Get());

	DescriptorPool::InterfaceType* ppHeaps[] =
	{
		m_descriptorTablePool->GetCbvSrvUavPool().Get(),
		m_descriptorTablePool->GetSamplerPool().Get()
	};
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_commandList->SetGraphicsRootDescriptorTable(0, *m_cbvTable);
	m_commandList->SetGraphicsRootDescriptorTable(1, *m_srvTable);
	m_commandList->SetGraphicsRootDescriptorTable(2, *m_samplerTable);
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	m_commandList->OMSetRenderTargets(1, m_rtvTables[m_frameIndex].get(), FALSE, &m_depth.GetDSV());
	
	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(*m_rtvTables[m_frameIndex], clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(m_depth.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBuffer.GetVBV());
	m_commandList->IASetIndexBuffer(&m_indexBuffer.GetIBV());
	m_commandList->DrawIndexedInstanced(3, 2, 0, 0, 0);

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void CharacterX::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void CharacterX::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
