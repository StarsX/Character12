//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "XUSGAdvanced.h"
#include "XUSGPostprocess.h"

#include "PSBlit2D.h"
#include "PSBlit2DSpv.h"
#include "PSPostprocess.h"
#include "PSPostprocessSpv.h"
#include "PSTemporalAA.h"
#include "PSTemporalAASpv.h"
#include "PSToneMap.h"
#include "PSToneMapSpv.h"
#include "CSLumAdapt.h"
#include "CSLumAdaptSpv.h"
#include "VSScreenQuad.h"
#include "VSScreenQuadSpv.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

//--------------------------------------------------------------------------------------
// Create interfaces
//--------------------------------------------------------------------------------------
Postprocess::uptr Postprocess::MakeUnique()
{
	Device::LockApi();
	return make_unique<Postprocess_Impl>();
}

Postprocess::sptr Postprocess::MakeShared()
{
	Device::LockApi();
	return make_shared<Postprocess_Impl>();
}

//--------------------------------------------------------------------------------------
// Postprocess implementations
//--------------------------------------------------------------------------------------
Postprocess_Impl::Postprocess_Impl() :
	m_frameIndex(0),
	m_timeStep(-FLT_MAX),
	m_shaderLib(nullptr),
	m_graphicsPipelineLib(nullptr),
	m_computePipelineLib(nullptr),
	m_pipelineLayoutLib(nullptr),
	m_descriptorTableLib(nullptr),
	m_viewport(),
	m_scissorRect(),
	m_pipelineLayouts(),
	m_pipelines(),
	m_cbvTables()
{
}

Postprocess_Impl::~Postprocess_Impl()
{
}

bool Postprocess_Impl::Init(Device* pDevice, const ShaderLib::sptr& shaderLib,
	const Graphics::PipelineLib::sptr& graphicsPipelineLib,
	const Compute::PipelineLib::sptr& computePipelineLib,
	const PipelineLayoutLib::sptr& pipelineLayoutLib,
	const DescriptorTableLib::sptr& descriptorTableLib,
	Format hdrFormat, Format ldrFormat)
{
	// Set shader lib and states
	m_shaderLib = shaderLib;
	m_graphicsPipelineLib = graphicsPipelineLib;
	m_computePipelineLib = computePipelineLib;
	m_pipelineLayoutLib = pipelineLayoutLib;
	m_descriptorTableLib = descriptorTableLib;

	// Create pipelines
	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(hdrFormat, ldrFormat), false);

	// Create constant buffer
	m_cbTimeStep = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbTimeStep->Create(pDevice, sizeof(float[FrameCount]), FrameCount,
		nullptr, MemoryType::GPU_UPLOAD, MemoryFlag::NONE, L"cbTimeStep"), false);

	return true;
}

bool Postprocess_Impl::ChangeWindowSize(Device* pDevice, const Texture* pReference)
{
	// Create CBV tables
	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_cbTimeStep->GetCBV(i));
		XUSG_X_RETURN(m_cbvTables[CBV_TIME_STEP + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	return createGBuffers(pDevice, pReference);
}

void Postprocess_Impl::Update(const DescriptorTable& cbvImmutable, const DescriptorTable& cbvPerFrameTable,
	uint8_t frameIndex, float timeStep)
{
	m_cbvTables[CBV_IMMUTABLE] = cbvImmutable;
	m_cbvTables[CBV_PER_FRAME] = cbvPerFrameTable;

	m_frameIndex = frameIndex;
	m_timeStep = timeStep > -2.0f ? timeStep : -1.0f;
}

void Postprocess_Impl::Render(CommandList* pCommandList, RenderTarget* pDst, Texture* pSrc,
	const DescriptorTable& srvTable, bool clearRT) const
{
	// Post-process effects
	ResourceBarrier barriers[3];
	auto numBarriers = pSrc->SetBarrier(barriers, ResourceState::PIXEL_SHADER_RESOURCE);
	numBarriers = m_postImage->SetBarrier(barriers, ResourceState::RENDER_TARGET, numBarriers);
	numBarriers = m_logLum->SetBarrier(barriers, ResourceState::RENDER_TARGET, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Full-screen post overwrite - avoid VK LOAD decompress/RMW.
	pCommandList->DiscardResource(m_postImage.get(), 0, nullptr, 0, 1);
	pCommandList->DiscardResource(m_logLum.get(), 0, nullptr, 0, 1);

	// Set render targets
	const Descriptor rtvs[] =
	{
		m_postImage->GetRTV(),
		m_logLum->GetRTV()
	};
	pCommandList->OMSetRenderTargets(static_cast<uint32_t>(size(rtvs)), rtvs);

	if (clearRT)
	{
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		const float clearLum[] = { 0.0f };
		pCommandList->ClearRenderTargetView(clearColor, 0);
		pCommandList->ClearRenderTargetView(clearLum, 1);
	}

	ScreenRender(pCommandList, POST_EFFECTS, srvTable, true, true);

	// Generate Mips
	numBarriers = m_avgLum->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS,
		0, XUSG_BARRIER_ALL_SUBRESOURCES, BarrierFlag::NONE,
		Device::GetApi() == API::VULKAN ? ResourceState::AUTO : ResourceState::COMMON);
	numBarriers = m_logLum->GenerateMips(pCommandList, barriers, ResourceState::ALL_SHADER_RESOURCE,
		m_pipelineLayouts[BLIT_LOG_LUM], m_pipelines[BLIT_LOG_LUM],
		&m_uavSrvTables[SRV_LOG_LUM + 1], TEXTURES, XUSG_NULL, 0, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Luminance adaptation
	LumAdaption(pCommandList, m_uavSrvTables[UAV_SRV_LUM]);

	// Tone mapping
	numBarriers = pDst->SetBarrier(barriers, ResourceState::RENDER_TARGET);
	numBarriers = m_postImage->SetBarrier(barriers, ResourceState::PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_avgLum->SetBarrier(barriers, ResourceState::PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);
	pCommandList->DiscardResource(pDst, 0, nullptr, 0, 1);
	pCommandList->OMSetRenderTargets(1, &pDst->GetRTV());
	if (clearRT)
	{
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		pCommandList->ClearRenderTargetView(clearColor, 0);
	}

	pCommandList->RSSetViewports(1, &m_viewport);
	pCommandList->RSSetScissorRects(1, &m_scissorRect);
	ScreenRender(pCommandList, TONE_MAP, m_uavSrvTables[SRV_COLOR_AVG_LUM], false, false);
}

void Postprocess_Impl::ScreenRender(CommandList* pCommandList, PipelineIndex pipelineIndex,
	const DescriptorTable& srvTable, bool hasImmutableCB, bool hasPerFrameCB) const
{
	// Set pipeline layout and descriptor tables
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[pipelineIndex]);
	pCommandList->SetGraphicsDescriptorTable(TEXTURES, srvTable);
	if (hasImmutableCB) pCommandList->SetGraphicsDescriptorTable(IMMUTABLE, m_cbvTables[CBV_IMMUTABLE]);
	if (hasPerFrameCB) pCommandList->SetGraphicsDescriptorTable(PER_FRAME, m_cbvTables[CBV_PER_FRAME]);

	// Set pipeline
	pCommandList->SetPipelineState(m_pipelines[pipelineIndex]);

	// Draw quad
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->Draw(3, 1, 0, 0);
}

void Postprocess_Impl::LumAdaption(CommandList* pCommandList, const DescriptorTable& uavSrvTable) const
{
	// Set shader constants
	const auto pCBTimeStep = static_cast<float*>(m_cbTimeStep->Map(m_frameIndex));
	*pCBTimeStep = m_timeStep;

	// Set pipeline layout and descriptor tables
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[LUM_ADAPT]);
	pCommandList->SetComputeDescriptorTable(TEXTURES, uavSrvTable);
	pCommandList->SetComputeDescriptorTable(TIME_STEP, m_cbvTables[CBV_TIME_STEP + m_frameIndex]);

	// Set pipeline
	pCommandList->SetPipelineState(m_pipelines[LUM_ADAPT]);

	// Dispatch
	pCommandList->Dispatch(1, 1, 1);
}

void Postprocess_Impl::Antialias(CommandList* pCommandList, uint8_t numRTVs, RenderTarget** ppDsts,
	uint8_t numSRVs, Texture** ppSrcs, const DescriptorTable& srvTable) const
{
	// Set barriers
	vector<ResourceBarrier> barriers(numRTVs + numSRVs);
	auto numBarriers = 0u;
	for (uint8_t i = 0; i < numRTVs; ++i)
		numBarriers = ppDsts[i]->SetBarrier(barriers.data(), ResourceState::RENDER_TARGET, numBarriers);
	for (uint8_t i = 0; i < numSRVs; ++i)
		numBarriers = ppSrcs[i]->SetBarrier(barriers.data(), ResourceState::PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers.data());

	// TAA full-screen overwrite of history / meta destinations.
	for (uint8_t i = 0; i < numRTVs; ++i)
		pCommandList->DiscardResource(ppDsts[i], 0, nullptr, 0, 1);

	// Set render targets
	vector<Descriptor> rtvs(numRTVs);
	for (uint8_t i = 0; i < numRTVs; ++i) rtvs[i] = ppDsts[i]->GetRTV();
	pCommandList->OMSetRenderTargets(numRTVs, rtvs.data());

	ScreenRender(pCommandList, ANTIALIAS, srvTable, true, false);
}

void Postprocess_Impl::Unsharp(CommandList* pCommandList, uint8_t numRTVs,
	const Descriptor* pRTVs, const DescriptorTable& srvTable) const
{
	// Set render target
	pCommandList->OMSetRenderTargets(numRTVs, pRTVs);

	// Set pipeline layout and descriptor tables
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[UNSHARP]);
	pCommandList->SetGraphicsDescriptorTable(TEXTURES, srvTable);

	// Set pipeline
	pCommandList->SetPipelineState(m_pipelines[UNSHARP]);

	// Draw quad
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->Draw(3, 1, 0, 0);
}

DescriptorTable Postprocess_Impl::CreateTAASrvTable(const Descriptor& srvCurrent, const Descriptor& srvPrevious,
	const Descriptor& srvVelocity, const Descriptor& srvShadeAmt, const Descriptor& srvMeta)
{
	const auto descriptorTable = Util::DescriptorTable::MakeUnique();
	const Descriptor descriptors[] = { srvCurrent, srvPrevious, srvVelocity, srvShadeAmt, srvMeta };
	descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);

	return descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get());
}

bool Postprocess_Impl::createGBuffers(Device* pDevice, const Texture* pReference)
{
	const auto width = static_cast<uint32_t>(pReference->GetWidth());
	const auto height = pReference->GetHeight();

	const auto numLogLumMips = Log2((max)(width, height));

	m_postImage = RenderTarget::MakeUnique();
	XUSG_N_RETURN(m_postImage->Create(pDevice, width, height, pReference->GetFormat(), 1,
		ResourceFlag::NONE, 1, 1, nullptr, false, MemoryFlag::NONE, L"PostImage"), false);

	m_logLum = RenderTarget::MakeUnique();
	XUSG_N_RETURN(m_logLum->Create(pDevice, width, height, Format::R16_FLOAT, 1, ResourceFlag::NONE,
		numLogLumMips, 1, nullptr, false, MemoryFlag::NONE, L"LogLuminance"), false);

	m_avgLum = StructuredBuffer::MakeUnique();
	XUSG_N_RETURN(m_avgLum->Create(pDevice, 1, sizeof(float), ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, MemoryFlag::NONE, L"AverageLogLuminance"), false);

	// Create viewport
	m_viewport = Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
	m_scissorRect = RectRange(0, 0, width, height);

	// Create SRV tables
	m_uavSrvTables.resize(SRV_LOG_LUM + numLogLumMips);

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] = { m_postImage->GetSRV(), m_avgLum->GetSRV() };
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		XUSG_X_RETURN(m_uavSrvTables[SRV_COLOR_AVG_LUM], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] = { m_avgLum->GetUAV(), m_logLum->GetSRV(numLogLumMips - 1) };
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		XUSG_X_RETURN(m_uavSrvTables[UAV_SRV_LUM], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	for (uint8_t i = 1; i < numLogLumMips; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_logLum->GetSRV(i - 1, true));
		XUSG_X_RETURN(m_uavSrvTables[SRV_LOG_LUM + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	return true;
}

bool Postprocess_Impl::createPipelineLayouts()
{
	const auto cbvSpace = GetCBVSpace();
	const auto srvSpace = GetSRVSpace();
	const auto samplerSpace = GetSamplerSpace();
	const auto uavSpace = GetUAVSpace();
	const auto pSampler = m_descriptorTableLib->GetSampler(LINEAR_CLAMP);

	// Temporal AA
	{
		auto cbImmutable = 0u;
		auto txImage = 0u;
		auto txHistory = txImage + 1;
		auto txVelocity = txHistory + 1;
		auto txMasks = txVelocity + 1;
		auto txHistMeta = txMasks + 1;
		auto smpLinearClamp = 0u;

		// Load shader
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, PS_TEMPORAL_AA,
			PSTemporalAA, sizeof(PSTemporalAA), PSTemporalAASpv, sizeof(PSTemporalAASpv)), false);

		// Get pixel shader slots
		auto reflector = m_shaderLib->GetReflector(Shader::Stage::PS, PS_TEMPORAL_AA);
		if (reflector && reflector->IsValid())
		{
			// Get constant buffer slot
			cbImmutable = reflector->ReflectBinding("cbImmutable", cbImmutable, cbvSpace);

			// Get shader resource slots
			txImage = reflector->ReflectBinding("g_txImage", txImage, srvSpace);
			txHistory = reflector->ReflectBinding("g_txHistory", txHistory, srvSpace);
			txVelocity = reflector->ReflectBinding("g_txVelocity", txVelocity, srvSpace);
			txMasks = reflector->ReflectBinding("g_txMask", txMasks, srvSpace);
			txHistMeta = reflector->ReflectBinding("g_txHistMeta", txHistMeta, srvSpace);

			// Get sampler slot
			smpLinearClamp = reflector->ReflectBinding("g_smpLinear", smpLinearClamp, samplerSpace);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Constant buffers
		utilPipelineLayout->SetRange(IMMUTABLE, DescriptorType::CBV, 1, cbImmutable, cbvSpace, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetShaderStage(IMMUTABLE, Shader::Stage::PS);

		// Textures
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txImage, srvSpace);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txHistory, srvSpace);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txVelocity, srvSpace);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txMasks, srvSpace);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txHistMeta, srvSpace);
		utilPipelineLayout->SetShaderStage(TEXTURES, Shader::Stage::PS);

		// Sampler
		utilPipelineLayout->SetStaticSamplers(&pSampler, 1, smpLinearClamp, samplerSpace, Shader::Stage::PS);

		XUSG_X_RETURN(m_pipelineLayouts[ANTIALIAS], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"TemporalAALayout"), false);
	}

	// Post effects
	{
		auto cbImmutable = 0u;
		auto cbPerFrame = cbImmutable + 1;
		auto txImage = 0u;
		auto txDepth = txImage + 1;
		auto smpLinearClamp = 0u;

		// Load shader
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, PS_POST_PROC,
			PSPostprocess, sizeof(PSPostprocess), PSPostprocessSpv, sizeof(PSPostprocessSpv)), false);

		// Get pixel shader slots
		auto reflector = m_shaderLib->GetReflector(Shader::Stage::PS, PS_POST_PROC);
		if (reflector && reflector->IsValid())
		{
			// Get constant buffer slots
			cbImmutable = reflector->ReflectBinding("cbImmutable", cbImmutable, cbvSpace);
			cbPerFrame = reflector->ReflectBinding("cbPerFrame", cbPerFrame, cbvSpace);

			// Get shader resource slots
			txImage = reflector->ReflectBinding("g_txImage", txImage, srvSpace);
			txDepth = reflector->ReflectBinding("g_txDepth", txDepth, srvSpace);

			// Get sampler slot
			smpLinearClamp = reflector->ReflectBinding("g_smpLinear", smpLinearClamp, samplerSpace);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Constant buffers
		utilPipelineLayout->SetRange(IMMUTABLE, DescriptorType::CBV, 1, cbImmutable, cbvSpace, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetShaderStage(IMMUTABLE, Shader::Stage::PS);

		utilPipelineLayout->SetRange(PER_FRAME, DescriptorType::CBV, 1, cbPerFrame, cbvSpace, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetShaderStage(PER_FRAME, Shader::Stage::PS);

		// Texture
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txImage, srvSpace);
		utilPipelineLayout->SetShaderStage(TEXTURES, Shader::Stage::PS);

		// Sampler
		utilPipelineLayout->SetStaticSamplers(&pSampler, 1, smpLinearClamp, samplerSpace, Shader::Stage::PS);

		XUSG_X_RETURN(m_pipelineLayouts[POST_EFFECTS], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"PostEffectsLayout"), false);
	}

	// Blit for log luminance MIP-map generation
	{
		auto txSource = 0u;
		auto smpLinearClamp = 0u;

		// Load shader if neccessary
		if (!m_shaderLib->GetShader(Shader::Stage::PS, PS_BLIT_2D))
			XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, PS_BLIT_2D,
				PSBlit2D, sizeof(PSBlit2D), PSBlit2DSpv, sizeof(PSBlit2DSpv)), false);

		// Get pixel shader slots
		auto reflector = m_shaderLib->GetReflector(Shader::Stage::PS, PS_BLIT_2D);
		if (reflector && reflector->IsValid())
		{
			// Get shader resource slot
			txSource = reflector->ReflectBinding("g_txSource", txSource, srvSpace);

			// Get sampler slot
			smpLinearClamp = reflector->ReflectBinding("g_smpLinear", smpLinearClamp, samplerSpace);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Texture
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txSource, srvSpace);
		utilPipelineLayout->SetShaderStage(TEXTURES, Shader::Stage::PS);

		// Samplers
		utilPipelineLayout->SetStaticSamplers(&pSampler, 1, smpLinearClamp, samplerSpace, Shader::Stage::PS);

		XUSG_X_RETURN(m_pipelineLayouts[BLIT_LOG_LUM], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"BlitLogLuminanceLayout"), false);
	}

	// Luminance adaptation
	{
		auto cbPerFrame = 1u;
		auto rwAvgLum = 0u;
		auto txLogLum = 0u;
		auto smpLinearClamp = 0u;

		// Load shader
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, CS_LUM_ADAPT,
			CSLumAdapt, sizeof(CSLumAdapt), CSLumAdaptSpv, sizeof(CSLumAdaptSpv)), false);

		// Get compute shader slots
		auto reflector = m_shaderLib->GetReflector(Shader::Stage::CS, CS_LUM_ADAPT);
		if (reflector && reflector->IsValid())
		{
			// Get constant buffer slot
			cbPerFrame = reflector->ReflectBinding("cbPerFrame", cbPerFrame, cbvSpace);

			// Get UAV slot
			rwAvgLum = reflector->ReflectBinding("g_rwAvgLum", rwAvgLum, uavSpace);

			// Get shader resource slot
			txLogLum = reflector->ReflectBinding("g_txLogLum", txLogLum, srvSpace);

			// Get sampler slot
			smpLinearClamp = reflector->ReflectBinding("g_smpLinear", smpLinearClamp, samplerSpace);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Constant buffers
		utilPipelineLayout->SetRange(TIME_STEP, DescriptorType::CBV, 1, cbPerFrame, cbvSpace, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetShaderStage(TIME_STEP, Shader::Stage::CS);

		// Textures
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::UAV, 1, rwAvgLum, uavSpace,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txLogLum, srvSpace);
		utilPipelineLayout->SetShaderStage(TEXTURES, Shader::Stage::CS);

		// Sampler
		utilPipelineLayout->SetStaticSamplers(&pSampler, 1, smpLinearClamp, samplerSpace, Shader::Stage::CS);

		XUSG_X_RETURN(m_pipelineLayouts[LUM_ADAPT], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"LuminanceAdaptationLayout"), false);
	}

	// Tone mapping
	{
		auto txImage = 0u;
		auto roAvgLum = txImage + 1;

		// Load shader
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, PS_TONE_MAP,
			PSToneMap, sizeof(PSToneMap), PSToneMapSpv, sizeof(PSToneMapSpv)), false);

		// Get pixel shader slots
		auto reflector = m_shaderLib->GetReflector(Shader::Stage::PS, PS_TONE_MAP);
		if (reflector && reflector->IsValid())
		{
			// Get shader resource slots
			txImage = reflector->ReflectBinding("g_txImage", txImage, srvSpace);
			roAvgLum = reflector->ReflectBinding("g_roAvgLum", roAvgLum, srvSpace);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Textures
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, txImage, srvSpace);
		utilPipelineLayout->SetRange(TEXTURES, DescriptorType::SRV, 1, roAvgLum, srvSpace);
		utilPipelineLayout->SetShaderStage(TEXTURES, Shader::Stage::PS);

		XUSG_X_RETURN(m_pipelineLayouts[TONE_MAP], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"ToneMappingLayout"), false);
	}

	return true;
}

bool Postprocess_Impl::createPipelines(Format hdrFormat, Format ldrFormat)
{
	const auto state = Graphics::State::MakeUnique();

	// Load shader if neccessary
	if (!m_shaderLib->GetShader(Shader::Stage::VS, VS_SCREEN_QUAD))
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, VS_SCREEN_QUAD,
			VSScreenQuad, sizeof(VSScreenQuad), VSScreenQuadSpv, sizeof(VSScreenQuadSpv)), false);

	// Common pipeline settings
	state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
	state->DSSetState(Graphics::DepthStencilPreset::DEPTH_STENCIL_NONE, m_graphicsPipelineLib.get());

	state->OMSetNumRenderTargets(2);
	state->OMSetRTVFormat(0, hdrFormat);

	// Temporal AA
	{
		// Get AA pipeline
		state->SetPipelineLayout(m_pipelineLayouts[ANTIALIAS]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, VS_SCREEN_QUAD));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, PS_TEMPORAL_AA));
		state->OMSetRTVFormat(1, Format::R8_UNORM);
		XUSG_X_RETURN(m_pipelines[ANTIALIAS], state->GetPipeline(m_graphicsPipelineLib.get(), L"TemporalAA"), false);
	}

	// Post effects
	{
		// Get post-effects pipeline
		state->SetPipelineLayout(m_pipelineLayouts[POST_EFFECTS]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, VS_SCREEN_QUAD));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, PS_POST_PROC));
		state->OMSetRTVFormat(1, Format::R16_FLOAT);
		XUSG_X_RETURN(m_pipelines[POST_EFFECTS], state->GetPipeline(m_graphicsPipelineLib.get(), L"PostEffects"), false);
	}

	state->OMSetNumRenderTargets(1);

	// Blit
	{
		// Get blit pipelines
		state->SetPipelineLayout(m_pipelineLayouts[BLIT_LOG_LUM]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, VS_SCREEN_QUAD));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, PS_BLIT_2D));
		state->OMSetRTVFormat(0, Format::R16_FLOAT);
		XUSG_X_RETURN(m_pipelines[BLIT_LOG_LUM], state->GetPipeline(m_graphicsPipelineLib.get(), L"BlitLum"), false);
	}

	// Luminance adaptation
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[LUM_ADAPT]);
		state->SetShader(m_shaderLib->GetShader(Shader::Stage::CS, CS_LUM_ADAPT));
		XUSG_X_RETURN(m_pipelines[LUM_ADAPT], state->GetPipeline(m_computePipelineLib.get(), L"LuminanceAdaptation"), false);
	}

	// Tone mapping
	{
		// Get tone-mapping pipeline
		state->SetPipelineLayout(m_pipelineLayouts[TONE_MAP]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, VS_SCREEN_QUAD));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, PS_TONE_MAP));
		state->OMSetRTVFormat(0, ldrFormat);
		XUSG_X_RETURN(m_pipelines[TONE_MAP], state->GetPipeline(m_graphicsPipelineLib.get(), L"ToneMapping"), false);
	}

	return true;
}
