//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "XUSGModel.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;
using namespace XUSG::Graphics;

//--------------------------------------------------------------------------------------
// Create interfaces
//--------------------------------------------------------------------------------------
Model::uptr Model::MakeUnique(const Device::sptr& device, const wchar_t* name)
{
	return make_unique<Model_Impl>(device, name);
}

Model::sptr Model::MakeShared(const Device::sptr& device, const wchar_t* name)
{
	return make_shared<Model_Impl>(device, name);
}

Model* Model::AsModel()
{
	return static_cast<Model*>(this);
}

//--------------------------------------------------------------------------------------
// Static interface functions
//--------------------------------------------------------------------------------------
const InputLayout* Model::CreateInputLayout(PipelineCache* pPipelineCache)
{
	// Define vertex data layout for post-transformed objects
	const InputElement inputElements[] =
	{
		{ "POSITION",	0, Format::R32G32B32_FLOAT,		0, 0,						InputClassification::PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, Format::R16G16B16A16_FLOAT,	0, APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, Format::R16G16_FLOAT,		0, APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 },
#if HAS_TANGENTS
		{ "TANGENT",	0, Format::R16G16B16A16_FLOAT,	0, APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, Format::R16G16B16A16_FLOAT,	0, APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 }
#endif
	};

	return pPipelineCache->CreateInputLayout(inputElements, static_cast<uint32_t>(size(inputElements)));
}

SDKMesh::sptr Model::LoadSDKMesh(const Device::sptr& device, const wstring& meshFileName,
	const TextureCache& textureCache, bool isStaticMesh)
{
	// Load the mesh
	const auto mesh = SDKMesh::MakeShared();
	N_RETURN(mesh->Create(device, meshFileName.c_str(), textureCache, isStaticMesh), nullptr);

	return mesh;
}

//--------------------------------------------------------------------------------------
// Model implementations
//--------------------------------------------------------------------------------------
Model_Impl::Model_Impl(const Device::sptr& device, const wchar_t* name) :
	m_device(device),
	m_currentFrame(0),
	m_variableSlot(VARIABLE_SLOT),
	m_mesh(nullptr),
	m_shaderPool(nullptr),
	m_graphicsPipelineCache(nullptr),
	m_descriptorTableCache(nullptr),
	m_pipelineLayouts(),
	m_pipelines(),
	m_cbvTables(),
	m_samplerTable(nullptr),
	m_srvTables(0)
{
	if (name) m_name = name;
	else m_name = L"";
}

Model_Impl::~Model_Impl()
{
}

bool Model_Impl::Init(const InputLayout* pInputLayout, const SDKMesh::sptr& mesh,
	const ShaderPool::sptr& shaderPool, const PipelineCache::sptr& pipelineCache,
	const PipelineLayoutCache::sptr& pipelineLayoutCache,
	const DescriptorTableCache::sptr& descriptorTableCache)
{
	// Set shader pool and states
	m_shaderPool = shaderPool;
	m_graphicsPipelineCache = pipelineCache;
	m_pipelineLayoutCache = pipelineLayoutCache;
	m_descriptorTableCache = descriptorTableCache;

	// Get SDKMesh
	m_mesh = mesh;

	// Create buffers and descriptor tables
	N_RETURN(createConstantBuffers(), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void Model_Impl::Update(uint8_t frameIndex)
{
	m_currentFrame = frameIndex;
	m_previousFrame = (frameIndex + FrameCount - 1) % FrameCount;
}

void Model_Impl::SetMatrices(CXMMATRIX world, bool isTemporal)
{
	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbMatrices->Map(m_currentFrame));
	XMStoreFloat3x4(&pCBData->World, world); // XMStoreFloat3x4 includes transpose.
	XMStoreFloat3x4(&pCBData->WorldIT, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));

#if TEMPORAL
	if (isTemporal)
	{
		pCBData->WorldPrev = m_world;
		XMStoreFloat3x4(&m_world, world);
	}
#endif
}

#if TEMPORAL_AA
void Model_Impl::SetTemporalBias(const XMFLOAT2& temporalBias)
{
	const auto pCBData = reinterpret_cast<XMFLOAT2*>(m_cbTemporalBias->Map(m_currentFrame));
	*pCBData = temporalBias;
}
#endif

void Model_Impl::SetPipelineLayout(const CommandList* pCommandList, PipelineLayoutIndex layout)
{
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
	if (layout != DEPTH_PASS)
		pCommandList->SetGraphicsDescriptorTable(m_variableSlot + SAMPLERS_OFFSET, m_samplerTable);
}

void Model_Impl::SetPipeline(const CommandList* pCommandList, PipelineIndex pipeline)
{
	pCommandList->SetPipelineState(m_pipelines[pipeline]);
}

void Model_Impl::SetPipeline(const CommandList* pCommandList, SubsetFlags subsetFlags, PipelineLayoutIndex layout)
{
	subsetFlags = subsetFlags & SUBSET_FULL;
	assert(subsetFlags != SUBSET_FULL);

	switch (subsetFlags)
	{
	case SUBSET_ALPHA_TEST:
		SetPipeline(pCommandList, layout ? DEPTH_TWO_SIDED : OPAQUE_TWO_SIDED);
		break;
	case SUBSET_ALPHA:
		SetPipeline(pCommandList, ALPHA_TWO_SIDED);
		break;
	default:
		SetPipeline(pCommandList, layout ? DEPTH_FRONT : OPAQUE_FRONT);
	}
}

void Model_Impl::Render(const CommandList* pCommandList, SubsetFlags subsetFlags, uint8_t matrixTableIndex,
	PipelineLayoutIndex layout, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances)
{
	if (pCbvPerFrameTable)
	{
		const DescriptorPool descriptorPools[] =
		{
			m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
			m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
		};
		pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
		pCommandList->SetGraphicsDescriptorTable(MATRICES, m_cbvTables[m_currentFrame][matrixTableIndex]);
		pCommandList->SetGraphicsDescriptorTable(PER_FRAME, *pCbvPerFrameTable);
		pCommandList->SetGraphicsDescriptorTable(m_variableSlot + SAMPLERS_OFFSET, m_samplerTable);
#if TEMPORAL_AA
		pCommandList->SetGraphicsDescriptorTable(TEMPORAL_BIAS, m_cbvTables[m_currentFrame][CBV_LOCAL_TEMPORAL_BIAS]);
#endif
	}

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Set IA parameters
		pCommandList->IASetVertexBuffers(0, 1, &m_mesh->GetVertexBufferView(m, 0));

		// Render mesh
		render(pCommandList, m, layout, subsetFlags, pCbvPerFrameTable, numInstances);
	}

	// Clear out the vb bindings for the next pass
	if (pCbvPerFrameTable) pCommandList->IASetVertexBuffers(0, 1, nullptr);
}

bool Model_Impl::createConstantBuffers()
{
	m_cbMatrices = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbMatrices->Create(m_device.get(), sizeof(CBMatrices[FrameCount]), FrameCount, nullptr,
		MemoryType::UPLOAD, MemoryFlag::NONE, m_name.empty() ? nullptr : (m_name + L".CBMatrices").c_str()), false);

	// Initialize constant buffers
	const auto identity = XMMatrixIdentity();
	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbMatrices->Map(i));
		XMStoreFloat3x4(&pCBData->World, identity);
		XMStoreFloat3x4(&pCBData->WorldIT, identity);
#if TEMPORAL
		XMStoreFloat3x4(&pCBData->WorldPrev, identity);
#endif
	}

#if TEMPORAL_AA
	m_cbTemporalBias = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbTemporalBias->Create(m_device.get(), sizeof(XMFLOAT2[FrameCount]), FrameCount, nullptr,
		MemoryType::UPLOAD, MemoryFlag::NONE, m_name.empty() ? nullptr : (m_name + L".CBTemporalBias").c_str()), false);
#endif

	return true;
}

bool Model_Impl::createPipelines(bool isStatic, const InputLayout* pInputLayout, const Format* rtvFormats,
	uint32_t numRTVs, Format dsvFormat, Format shadowFormat)
{
	const auto defaultRtvFormat = Format::B8G8R8A8_UNORM;
	numRTVs = numRTVs > 0 ? numRTVs : 1;
	rtvFormats = rtvFormats ? rtvFormats : &defaultRtvFormat;
	dsvFormat = dsvFormat != Format::UNKNOWN ? dsvFormat : Format::D24_UNORM_S8_UINT;
	shadowFormat = shadowFormat != Format::UNKNOWN ? shadowFormat : Format::D16_UNORM;

	const auto state = Graphics::State::MakeUnique();

	// Base pass
	{
		const auto vsBasePass = isStatic ? VS_BASE_PASS_STATIC : VS_BASE_PASS;
		const auto psBasePass = isStatic ? PS_BASE_PASS_STATIC : PS_BASE_PASS;
		const auto psAlphaTest = isStatic ? PS_ALPHA_TEST_STATIC : PS_ALPHA_TEST;

		// Get opaque pipelines
		state->IASetInputLayout(pInputLayout);
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->SetPipelineLayout(m_pipelineLayouts[BASE_PASS]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsBasePass));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psBasePass));
		state->OMSetRTVFormats(rtvFormats, numRTVs);
		state->OMSetDSVFormat(dsvFormat);
		X_RETURN(m_pipelines[OPAQUE_FRONT], state->GetPipeline(m_graphicsPipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".OpaqueFront").c_str()), false);

		state->DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_EQUAL, m_graphicsPipelineCache.get());
		X_RETURN(m_pipelines[OPAQUE_FRONT_EQUAL], state->GetPipeline(m_graphicsPipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".OpaqueFrontZEqual").c_str()), false);

		// Get transparent pipeline
		state->RSSetState(Graphics::RasterizerPreset::CULL_NONE, m_graphicsPipelineCache.get());
		state->DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_LESS, m_graphicsPipelineCache.get());
		state->OMSetBlendState(Graphics::BlendPreset::AUTO_NON_PREMUL, m_graphicsPipelineCache.get());
		X_RETURN(m_pipelines[ALPHA_TWO_SIDED], state->GetPipeline(m_graphicsPipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".AlphaTwoSided").c_str()), false);

		// Get alpha-test pipelines
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psAlphaTest));
		state->DSSetState(Graphics::DepthStencilPreset::DEFAULT_LESS, m_graphicsPipelineCache.get());
		state->OMSetBlendState(Graphics::DEFAULT_OPAQUE, m_graphicsPipelineCache.get());
		X_RETURN(m_pipelines[OPAQUE_TWO_SIDED], state->GetPipeline(m_graphicsPipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".OpaqueTwoSided").c_str()), false);

		state->DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_EQUAL, m_graphicsPipelineCache.get());
		X_RETURN(m_pipelines[OPAQUE_TWO_SIDED_EQUAL], state->GetPipeline(m_graphicsPipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".OpaqueTwoSidedZEqual").c_str()), false);
	}

	// Depth and shadow passes
	const auto vsDepthPass = isStatic ? VS_DEPTH_STATIC : VS_DEPTH;
	const auto vsShadowPass = isStatic ? VS_SHADOW_STATIC : VS_SHADOW;
	const auto vsDepth = m_shaderPool->GetShader(Shader::Stage::VS, vsDepthPass);
	const auto vsShadow = m_shaderPool->GetShader(Shader::Stage::VS, vsShadowPass);
	const auto psDepth = m_shaderPool->GetShader(Shader::Stage::PS, PS_DEPTH);
	if (vsDepth || vsShadow)
	{
		const Format nullRtvFormats[8] = {};

		// Get depth pipelines
		state->RSSetState(Graphics::RasterizerPreset::CULL_BACK, m_graphicsPipelineCache.get());
		state->DSSetState(Graphics::DepthStencilPreset::DEFAULT_LESS, m_graphicsPipelineCache.get());
		state->OMSetRTVFormats(nullRtvFormats, 8);
		state->OMSetNumRenderTargets(0);

		if (vsDepth)
		{
			// Get depth opaque pipeline
			state->SetPipelineLayout(m_pipelineLayouts[DEPTH_PASS]);
			state->SetShader(Shader::Stage::VS, vsDepth);
			state->SetShader(Shader::Stage::PS, nullptr);
			X_RETURN(m_pipelines[DEPTH_FRONT], state->GetPipeline(m_graphicsPipelineCache.get(),
				m_name.empty() ? nullptr : (m_name + L".DepthFront").c_str()), false);

			// Get depth alpha-test pipeline
			state->SetPipelineLayout(m_pipelineLayouts[DEPTH_ALPHA_PASS]);
			state->SetShader(Shader::Stage::PS, psDepth);
			state->RSSetState(Graphics::RasterizerPreset::CULL_NONE, m_graphicsPipelineCache.get());
			X_RETURN(m_pipelines[DEPTH_TWO_SIDED], state->GetPipeline(m_graphicsPipelineCache.get(),
				m_name.empty() ? nullptr : (m_name + L".DepthTwoSided").c_str()), false);
		}

		if (vsShadow)
		{
			// Get shadow opaque pipeline
			state->SetPipelineLayout(m_pipelineLayouts[DEPTH_PASS]);
			state->SetShader(Shader::Stage::VS, vsShadow);
			state->SetShader(Shader::Stage::PS, nullptr);
			state->OMSetDSVFormat(shadowFormat);
			X_RETURN(m_pipelines[SHADOW_FRONT], state->GetPipeline(m_graphicsPipelineCache.get(),
				m_name.empty() ? nullptr : (m_name + L".ShadowFront").c_str()), false);

			// Get shadow alpha-test pipeline
			state->SetPipelineLayout(m_pipelineLayouts[DEPTH_ALPHA_PASS]);
			state->SetShader(Shader::Stage::PS, psDepth);
			state->RSSetState(Graphics::RasterizerPreset::CULL_NONE, m_graphicsPipelineCache.get());
			X_RETURN(m_pipelines[SHADOW_TWO_SIDED], state->GetPipeline(m_graphicsPipelineCache.get(),
				m_name.empty() ? nullptr : (m_name + L".ShadowTwoSided").c_str()), false);
		}
	}

	// Get reflected pipeline
	//state->RSSetState(Graphics::RasterizerPreset::CULL_FRONT, m_pipelineCache.get());
	//state->OMSetBlendState(Graphics::BlendPreset::DEFAULT_OPAQUE, m_pipelineCache.get());
	//m_pipelines[REFLECTED] = state->GetPipeline(m_pipelineCache.get());

	return true;
}

bool Model_Impl::createDescriptorTables()
{
	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		{
			const auto descriptorTable = Util::DescriptorTable::MakeUnique();
			descriptorTable->SetDescriptors(0, 1, &m_cbMatrices->GetCBV(i));
			X_RETURN(m_cbvTables[i][CBV_MATRICES], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
		}

#if TEMPORAL_AA
		{
			const auto descriptorTable = Util::DescriptorTable::MakeUnique();
			descriptorTable->SetDescriptors(0, 1, &m_cbTemporalBias->GetCBV(i));
			X_RETURN(m_cbvTables[i][CBV_LOCAL_TEMPORAL_BIAS], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
		}
#endif
	}

	const auto descriptorTable = Util::DescriptorTable::MakeUnique();
	const SamplerPreset samplers[] = { ANISOTROPIC_WRAP, POINT_WRAP, LINEAR_LESS_EQUAL };
	descriptorTable->SetSamplers(0, static_cast<uint32_t>(size(samplers)), samplers, m_descriptorTableCache.get());
	X_RETURN(m_samplerTable, descriptorTable->GetSamplerTable(m_descriptorTableCache.get()), false);

	// Materials
	const auto numMaterials = m_mesh->GetNumMaterials();
	m_srvTables.resize(numMaterials);
	for (auto m = 0u; m < numMaterials; ++m)
	{
		const auto pMaterial = m_mesh->GetMaterial(m);

		if (pMaterial && pMaterial->pAlbedo && pMaterial->pNormal)
		{
			const auto descriptorTable = Util::DescriptorTable::MakeUnique();
			const Descriptor descriptors[] = { pMaterial->pAlbedo->GetSRV(), pMaterial->pNormal->GetSRV() };
			descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
			X_RETURN(m_srvTables[m], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
		}
		else m_srvTables[m] = nullptr;
	}

	return true;
}

void Model_Impl::render(const CommandList* pCommandList, uint32_t mesh, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances)
{
	assert((subsetFlags & SUBSET_FULL) != SUBSET_FULL);

	// Set IA parameters
	pCommandList->IASetIndexBuffer(m_mesh->GetIndexBufferView(mesh));

	const auto materialType = subsetFlags & SUBSET_OPAQUE ? SUBSET_OPAQUE : SUBSET_ALPHA;

	// Set pipeline state
	if (pCbvPerFrameTable) SetPipeline(pCommandList, subsetFlags, layout);

	//const uint8_t materialSlot = psBaseSlot + MATERIAL;
	const auto numSubsets = m_mesh->GetNumSubsets(mesh, materialType);
	for (auto subset = 0u; subset < numSubsets; ++subset)
	{
		// Get subset
		const auto pSubset = m_mesh->GetSubset(mesh, subset, materialType);
		const auto primType = m_mesh->GetPrimitiveType(SDKMesh::PrimitiveType(pSubset->PrimitiveType));
		pCommandList->IASetPrimitiveTopology(primType);

		// Set material
		if (layout != DEPTH_PASS && m_mesh->GetMaterial(pSubset->MaterialID) && m_srvTables[pSubset->MaterialID])
			pCommandList->SetGraphicsDescriptorTable(m_variableSlot + MATERIAL_OFFSET, m_srvTables[pSubset->MaterialID]);

		// Draw
		pCommandList->DrawIndexed(static_cast<uint32_t>(pSubset->IndexCount), numInstances,
			static_cast<uint32_t>(pSubset->IndexStart), static_cast<int32_t>(pSubset->VertexStart), 0);
	}
}

Util::PipelineLayout::sptr Model_Impl::initPipelineLayout(VertexShader vs, PixelShader ps)
{
	auto cbMatrices = 0u;
	auto cbPerFrame = cbMatrices + 1;
	auto cbPerObject = cbPerFrame + 1;
#if TEMPORAL_AA
	auto cbTempBias = cbPerObject + 1;
#endif
	auto txDiffuse = 0u;
	auto txNormal = txDiffuse + 1;
	auto smpAnisoWrap = 0u;
	auto smpLinearCmp = smpAnisoWrap + 2;

	// Get vertex shader slots
	auto reflector = m_shaderPool->GetReflector(Shader::Stage::VS, vs);
	if (reflector && reflector->IsValid())
	{
		// Get constant buffer slots
		cbMatrices = reflector->GetResourceBindingPointByName("cbMatrices", cbMatrices);
		cbPerFrame = reflector->GetResourceBindingPointByName("cbPerFrame", cbPerFrame);
#if TEMPORAL_AA
		cbTempBias = reflector->GetResourceBindingPointByName("cbTempBias", cbTempBias);
#endif
	}

	// Get pixel shader slots
	auto cbImmutable = cbMatrices;
	if (ps != PS_NULL_INDEX)
	{
		reflector = m_shaderPool->GetReflector(Shader::Stage::PS, ps);
		if (reflector && reflector->IsValid())
		{
			// Get constant buffer slots
			cbImmutable = reflector->GetResourceBindingPointByName("cbImmutable");
			cbPerObject = reflector->GetResourceBindingPointByName("cbPerObject", cbPerObject);

			// Get shader resource slots
			txDiffuse = reflector->GetResourceBindingPointByName("g_txAlbedo", txDiffuse);
			txNormal = reflector->GetResourceBindingPointByName("g_txNormal", txNormal);
			
			// Get sampler slots
			smpAnisoWrap = reflector->GetResourceBindingPointByName("g_smpLinear", smpAnisoWrap);
			smpLinearCmp = reflector->GetResourceBindingPointByName("g_smpCmpLinear", smpLinearCmp);
		}
	}

	// Pipeline layout utility
	const auto utilPipelineLayout = Util::PipelineLayout::MakeShared();

	// Constant buffers
	utilPipelineLayout->SetRange(MATRICES, DescriptorType::CBV, 1, cbMatrices, 0, DescriptorFlag::DATA_STATIC);
	utilPipelineLayout->SetShaderStage(MATRICES, Shader::Stage::VS);

	utilPipelineLayout->SetRange(PER_FRAME, DescriptorType::CBV, 1, cbPerFrame, 0, DescriptorFlag::DATA_STATIC);
	utilPipelineLayout->SetShaderStage(PER_FRAME, Shader::Stage::VS);

#if TEMPORAL_AA
	utilPipelineLayout->SetRange(TEMPORAL_BIAS, DescriptorType::CBV, 1, cbTempBias, 0, DescriptorFlag::DATA_STATIC);
	utilPipelineLayout->SetShaderStage(TEMPORAL_BIAS, Shader::Stage::VS);
#endif

	if (ps != PS_NULL_INDEX)
	{
		if (ps != PS_DEPTH && cbImmutable != UINT32_MAX)
		{
			const uint8_t immutableSlot = m_variableSlot + IMMUTABLE_OFFSET;
			utilPipelineLayout->SetRange(immutableSlot, DescriptorType::CBV, 1, cbImmutable,
				0, DescriptorFlag::DATA_STATIC);
			utilPipelineLayout->SetShaderStage(immutableSlot, Shader::Stage::PS);
		}

		// Textures (material)
		const uint8_t materialSlot = m_variableSlot + MATERIAL_OFFSET;
		utilPipelineLayout->SetRange(materialSlot, DescriptorType::SRV, 1, txDiffuse, 0, DescriptorFlag::DATA_STATIC);
		if (ps != PS_DEPTH) utilPipelineLayout->SetRange(materialSlot, DescriptorType::SRV,
			1, txNormal, 0, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetShaderStage(materialSlot, Shader::Stage::PS);

		if (ps == PS_DEPTH)
			utilPipelineLayout->SetConstants(m_variableSlot + ALPHA_REF_OFFSET,
				SizeOfInUint32(XMFLOAT2), cbPerObject, 0, Shader::Stage::PS);

		// Samplers
		const uint8_t samplerSlot = m_variableSlot + SAMPLERS_OFFSET;
		utilPipelineLayout->SetRange(samplerSlot, DescriptorType::SAMPLER, 2, smpAnisoWrap);
		if (ps != PS_DEPTH) utilPipelineLayout->SetRange(samplerSlot, DescriptorType::SAMPLER, 1, smpLinearCmp);
		utilPipelineLayout->SetShaderStage(samplerSlot, Shader::Stage::PS);
	}

	return utilPipelineLayout;
}
