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

CharacterX::CharacterX(uint32_t width, uint32_t height, std::wstring name) :
	DXFramework(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<long>(width), static_cast<long>(height)),
	m_pausing(false),
	m_tracking(false)
{
#if defined (_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONIN$", "r+t", stdin);
	freopen_s(&stream, "CONOUT$", "w+t", stdout);
	freopen_s(&stream, "CONOUT$", "w+t", stderr);
#endif
}

CharacterX::~CharacterX()
{
#if defined (_DEBUG)
	FreeConsole();
#endif
}

void CharacterX::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void CharacterX::LoadPipeline()
{
	auto dxgiFactoryFlags = 0u;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		com_ptr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			//debugController->SetEnableGPUBasedValidation(TRUE);

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	com_ptr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	DXGI_ADAPTER_DESC1 dxgiAdapterDesc;
	com_ptr<IDXGIAdapter1> dxgiAdapter;
	auto hr = DXGI_ERROR_UNSUPPORTED;
	for (auto i = 0u; hr == DXGI_ERROR_UNSUPPORTED; ++i)
	{
		dxgiAdapter = nullptr;
		ThrowIfFailed(factory->EnumAdapters1(i, &dxgiAdapter));

		m_device = Device::MakeShared();
		hr = m_device->Create(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0);
	}

	dxgiAdapter->GetDesc1(&dxgiAdapterDesc);
	if (dxgiAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		m_title += dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c ? L" (WARP)" : L" (Software)";
	ThrowIfFailed(hr);

	// Create the command queue.
	m_commandQueue = CommandQueue::MakeUnique();
	N_RETURN(m_commandQueue->Create(m_device.get(), CommandListType::DIRECT, CommandQueueFlag::NONE,
		0, 0, L"CommandQueue"), ThrowIfFailed(E_FAIL));

	// Describe and create the swap chain.
	m_swapChain = SwapChain::MakeUnique();
	N_RETURN(m_swapChain->Create(factory.get(), Win32Application::GetHwnd(), m_commandQueue.get(),
		FrameCount, m_width, m_height, Format::B8G8R8A8_UNORM), ThrowIfFailed(E_FAIL));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_descriptorTableCache = DescriptorTableCache::MakeShared(m_device.get(), L"DescriptorTableCache");

	// Create frame resources.
	// Create a RTV and a command allocator for each frame.
	for (uint8_t n = 0; n < FrameCount; ++n)
	{
		m_renderTargets[n] = RenderTarget::MakeUnique();
		N_RETURN(m_renderTargets[n]->CreateFromSwapChain(m_device.get(), m_swapChain.get(), n), ThrowIfFailed(E_FAIL));

		m_commandAllocators[n] = CommandAllocator::MakeUnique();
		N_RETURN(m_commandAllocators[n]->Create(m_device.get(), CommandListType::DIRECT,
			(L"CommandAllocator" + to_wstring(n)).c_str()), ThrowIfFailed(E_FAIL));
	}

	// Create a DSV
	m_depth = DepthStencil::MakeUnique();
	N_RETURN(m_depth->Create(m_device.get(), m_width, m_height, Format::D24_UNORM_S8_UINT,
		ResourceFlag::DENY_SHADER_RESOURCE), ThrowIfFailed(E_FAIL));
}

// Load the sample assets.
void CharacterX::LoadAssets()
{
	m_shaderPool = ShaderPool::MakeShared();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeShared(m_device.get());
	m_computePipelineCache = Compute::PipelineCache::MakeShared(m_device.get());
	m_pipelineLayoutCache = PipelineLayoutCache::MakeShared(m_device.get());

	// Create the shaders.
	{
		m_shaderPool->CreateShader(Shader::Stage::VS, VS_BASE_PASS, L"VSBasePass.cso");
		m_shaderPool->CreateShader(Shader::Stage::PS, PS_BASE_PASS, L"PSBasePass.cso");
		m_shaderPool->CreateShader(Shader::Stage::PS, PS_ALPHA_TEST, L"PSAlphaTest.cso");
		m_shaderPool->CreateShader(Shader::Stage::CS, CS_SKINNING, L"CSSkinning.cso");
	}

	// Create the command list.
	m_commandList = CommandList::MakeUnique();
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Create(m_device.get(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));

	// Load character asset
	{
		m_pInputLayout = Character::CreateInputLayout(m_graphicsPipelineCache.get());
		const auto textureCache = make_shared<TextureCache::element_type>();
		const auto characterMesh = Character::LoadSDKMesh(m_device, L"Assets/Bright/Stars.sdkmesh",
			L"Assets/Bright/Stars.sdkmesh_anim", textureCache);
		if (!characterMesh) ThrowIfFailed(E_FAIL);
		m_character = Character::MakeUnique(m_device, L"Stars");
		if (!m_character) ThrowIfFailed(E_FAIL);
		if (!m_character->Init(m_pInputLayout, characterMesh, m_shaderPool,
			m_graphicsPipelineCache, m_computePipelineCache,
			m_pipelineLayoutCache, m_descriptorTableCache))
			ThrowIfFailed(E_FAIL);
	}

	// Create per-frame constant buffer
	m_cbPerFrame = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbPerFrame->Create(m_device.get(), sizeof(XMFLOAT4X4[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBPerFrame"), ThrowIfFailed(E_FAIL));

	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_cbPerFrame->GetCBV(i));
		X_RETURN(m_cbvTables[i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), ThrowIfFailed(E_FAIL));
	}

	// Close the command list and execute it to begin the initial GPU setup.
	N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(pCommandList);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if (!m_fence)
		{
			m_fence = Fence::MakeUnique();
			N_RETURN(m_fence->Create(m_device.get(), m_fenceValues[m_frameIndex]++, FenceFlag::NONE, L"Fence"), ThrowIfFailed(E_FAIL));
		}

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

	// Projection
	{
		const auto aspectRatio = m_width / static_cast<float>(m_height);
		const auto proj = XMMatrixPerspectiveFovLH(g_FOVAngleY, aspectRatio, g_zNear, g_zFar);
		XMStoreFloat4x4(&m_proj, proj);
	}

	// View initialization
	{
		m_focusPt = XMFLOAT3(0.0f, 8.0f, 0.0f);
		m_eyePt = XMFLOAT3(m_focusPt.x, m_focusPt.y, m_focusPt.z - 25.0f);
		const auto focusPt = XMLoadFloat3(&m_focusPt);
		const auto eyePt = XMLoadFloat3(&m_eyePt);
		const auto view = XMMatrixLookAtLH(eyePt, focusPt, XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f));
		XMStoreFloat4x4(&m_view, view);
	}
}

// Update frame-based values.
void CharacterX::OnUpdate()
{
	// Timer
	static auto time = 0.0, pauseTime = 0.0;

	m_timer.Tick();
	const auto totalTime = CalculateFrameStats();
	pauseTime = m_pausing ? totalTime - time : pauseTime;
	time = totalTime - pauseTime;

	// View
	const auto eyePt = XMLoadFloat3(&m_eyePt);
	const auto view = XMLoadFloat4x4(&m_view);
	const auto proj = XMLoadFloat4x4(&m_proj);
	const auto viewProj = view * proj;
	const auto world = XMMatrixIdentity();

	// Update constant buffers
	const auto pCBData = reinterpret_cast<XMFLOAT4X4*>(m_cbPerFrame->Map(m_frameIndex));
	XMStoreFloat4x4(pCBData, XMMatrixTranspose(viewProj)); // XMStoreFloat3x4 includes transpose.

	// Character
	m_character->Update(m_frameIndex, time, &world, false);
}

// Render the scene.
void CharacterX::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	// Present the frame.
	N_RETURN(m_swapChain->Present(0, 0), ThrowIfFailed(E_FAIL));

	MoveToNextFrame();
}

void CharacterX::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);
}

// User hot-key interactions.
void CharacterX::OnKeyUp(uint8_t key)
{
	switch (key)
	{
	case VK_SPACE:
		m_pausing = !m_pausing;
		break;
	}
}

// User camera interactions.
void CharacterX::OnLButtonDown(float posX, float posY)
{
	m_tracking = true;
	m_mousePt = XMFLOAT2(posX, posY);
}

void CharacterX::OnLButtonUp(float posX, float posY)
{
	m_tracking = false;
}

void CharacterX::OnMouseMove(float posX, float posY)
{
	if (m_tracking)
	{
		const auto dPos = XMFLOAT2(m_mousePt.x - posX, m_mousePt.y - posY);

		XMFLOAT2 radians;
		radians.x = XM_2PI * dPos.y / m_height;
		radians.y = XM_2PI * dPos.x / m_width;

		const auto focusPt = XMLoadFloat3(&m_focusPt);
		auto eyePt = XMLoadFloat3(&m_eyePt);

		const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
		auto transform = XMMatrixTranslation(0.0f, 0.0f, -len);
		transform *= XMMatrixRotationRollPitchYaw(radians.x, radians.y, 0.0f);
		transform *= XMMatrixTranslation(0.0f, 0.0f, len);

		const auto view = XMLoadFloat4x4(&m_view) * transform;
		const auto viewInv = XMMatrixInverse(nullptr, view);
		eyePt = viewInv.r[3];

		XMStoreFloat3(&m_eyePt, eyePt);
		XMStoreFloat4x4(&m_view, view);

		m_mousePt = XMFLOAT2(posX, posY);
	}
}

void CharacterX::OnMouseWheel(float deltaZ, float posX, float posY)
{
	const auto focusPt = XMLoadFloat3(&m_focusPt);
	auto eyePt = XMLoadFloat3(&m_eyePt);

	const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
	const auto transform = XMMatrixTranslation(0.0f, 0.0f, -len * deltaZ / 16.0f);

	const auto view = XMLoadFloat4x4(&m_view) * transform;
	const auto viewInv = XMMatrixInverse(nullptr, view);
	eyePt = viewInv.r[3];

	XMStoreFloat3(&m_eyePt, eyePt);
	XMStoreFloat4x4(&m_view, view);
}

void CharacterX::OnMouseLeave()
{
	m_tracking = false;
}

void CharacterX::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	const auto pCommandAllocator = m_commandAllocators[m_frameIndex].get();
	N_RETURN(pCommandAllocator->Reset(), ThrowIfFailed(E_FAIL));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Reset(pCommandAllocator, nullptr), ThrowIfFailed(E_FAIL));

	// Skinning
	auto numBarriers = 0u;
	ResourceBarrier barriers[1];
	m_character->Skinning(pCommandList, numBarriers, barriers, true);
	pCommandList->Barrier(numBarriers, barriers);

	// Set necessary state.
	pCommandList->RSSetViewports(1, &m_viewport);
	pCommandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::RENDER_TARGET);
	pCommandList->Barrier(numBarriers, barriers);
	pCommandList->OMSetRenderTargets(1, &m_renderTargets[m_frameIndex]->GetRTV(), &m_depth->GetDSV());

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	pCommandList->ClearRenderTargetView(m_renderTargets[m_frameIndex]->GetRTV(), clearColor, 0, nullptr);
	pCommandList->ClearDepthStencilView(m_depth->GetDSV(), ClearFlag::DEPTH, 1.0f, 0, 0, nullptr);
	//m_commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_character->RenderTransformed(pCommandList, Character::BASE_PASS, SUBSET_FULL, &m_cbvTables[m_frameIndex]);

	// Indicate that the back buffer will now be used to present.
	numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::PRESENT);
	pCommandList->Barrier(numBarriers, barriers);

	N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
}

// Wait for pending GPU work to complete.
void CharacterX::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	N_RETURN(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]), ThrowIfFailed(E_FAIL));

	// Wait until the fence has been processed, and increment the fence value for the current frame.
	N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex]++, m_fenceEvent), ThrowIfFailed(E_FAIL));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
}

// Prepare to render the next frame.
void CharacterX::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const auto currentFenceValue = m_fenceValues[m_frameIndex];
	N_RETURN(m_commandQueue->Signal(m_fence.get(), currentFenceValue), ThrowIfFailed(E_FAIL));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

double CharacterX::CalculateFrameStats(float* pTimeStep)
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0;
	static double previousTime = 0.0;
	const auto totalTime = m_timer.GetTotalSeconds();
	++frameCnt;

	const auto timeStep = static_cast<float>(totalTime - elapsedTime);

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float fps = static_cast<float>(frameCnt) / timeStep;	// Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		wstringstream windowText;
		windowText << setprecision(2) << fixed << L"    fps: " << fps;
		SetCustomWindowText(windowText.str().c_str());
	}

	if (pTimeStep)* pTimeStep = static_cast<float>(totalTime - previousTime);
	previousTime = totalTime;

	return totalTime;
}
