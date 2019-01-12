//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGModel.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;
using namespace XUSG::Graphics;

Model::Model(const Device &device, const CommandList &commandList, const wchar_t *name) :
	m_device(device),
	m_commandList(commandList),
	m_currentFrame(0),
	m_mesh(nullptr),
	m_shaderPool(nullptr),
	m_pipelineCache(nullptr),
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

Model::~Model()
{
}

bool Model::Init(const InputLayout &inputLayout, const shared_ptr<SDKMesh> &mesh,
	const shared_ptr<ShaderPool> &shaderPool, const shared_ptr<PipelineCache> &pipelineCache,
	const shared_ptr<PipelineLayoutCache> &pipelineLayoutCache,
	const shared_ptr <DescriptorTableCache> &descriptorTableCache)
{
	// Set shader pool and states
	m_shaderPool = shaderPool;
	m_pipelineCache = pipelineCache;
	m_pipelineLayoutCache = pipelineLayoutCache;
	m_descriptorTableCache = descriptorTableCache;

	// Get SDKMesh
	m_mesh = mesh;

	// Create buffers and descriptor tables
	N_RETURN(createConstantBuffers(), false);
	createDescriptorTables();

	return true;
}

void Model::Update(uint8_t frameIndex)
{
	m_currentFrame = frameIndex;
	m_previousFrame = (frameIndex + FrameCount - 1) % FrameCount;
}

void Model::SetMatrices(CXMMATRIX viewProj, CXMMATRIX world, FXMMATRIX *pShadowView,
	FXMMATRIX *pShadows, uint8_t numShadows, bool isTemporal)
{
	// Set World-View-Proj matrix
	const auto worldViewProj = XMMatrixMultiply(world, viewProj);

	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbMatrices.Map(m_currentFrame));
	pCBData->WorldViewProj = XMMatrixTranspose(worldViewProj);
	pCBData->World = XMMatrixTranspose(world);
	pCBData->Normal = XMMatrixInverse(nullptr, world);
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose once
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose twice
	
	if (pShadowView)
	{
		const auto shadow = XMMatrixMultiply(world, *pShadowView);
		pCBData->Shadow = XMMatrixTranspose(shadow);
	}

	if (pShadows)
	{
		for (auto i = 0ui8; i < numShadows; ++i)
		{
			const auto shadow = XMMatrixMultiply(world, pShadows[i]);
			auto &cbData = *reinterpret_cast<XMMATRIX*>(m_cbShadowMatrices.Map(m_currentFrame * MAX_SHADOW_CASCADES + i));
			cbData = XMMatrixTranspose(shadow);
		}
	}

#if	TEMPORAL
	if (isTemporal)
	{
		XMStoreFloat4x4(&m_worldViewProjs[m_currentFrame], worldViewProj);
		const auto worldViewProjPrev = XMLoadFloat4x4(&m_worldViewProjs[m_previousFrame]);
		pCBData->WorldViewProjPrev = XMMatrixTranspose(worldViewProjPrev);
	}
#endif
}

void Model::SetPipelineLayout(PipelineLayoutIndex layout)
{
	m_commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
	m_commandList.SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);
}

void Model::SetPipeline(PipelineIndex pipeline)
{
	m_commandList.SetPipelineState(m_pipelines[pipeline]);
}

void Model::SetPipelineState(SubsetFlags subsetFlags, PipelineLayoutIndex layout)
{
	subsetFlags = subsetFlags & SUBSET_FULL;
	assert(subsetFlags != SUBSET_FULL);

	switch (subsetFlags)
	{
	case SUBSET_ALPHA_TEST:
		m_commandList.SetPipelineState(m_pipelines[layout ? DEPTH_TWO_SIDED : OPAQUE_TWO_SIDED]);
		break;
	case SUBSET_ALPHA:
		m_commandList.SetPipelineState(m_pipelines[ALPHA_TWO_SIDED]);
		break;
	default:
		m_commandList.SetPipelineState(m_pipelines[layout ? DEPTH_FRONT : OPAQUE_FRONT]);
	}
	//if (subsetFlags == SUBSET_REFLECTED)
		//m_commandList->SetPipelineState(m_pipelines[REFLECTED]); 
}

void Model::Render(SubsetFlags subsetFlags, uint8_t matrixTableIndex, PipelineLayoutIndex layout)
{
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	m_commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
	m_commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
	m_commandList.SetGraphicsDescriptorTable(MATRICES, m_cbvTables[m_currentFrame][matrixTableIndex]);
	m_commandList.SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Set IA parameters
		m_commandList.IASetVertexBuffers(0, 1, &m_mesh->GetVertexBufferView(m, 0));

		// Render mesh
		render(m, subsetFlags, layout);
	}

	// Clear out the vb bindings for the next pass
	if (layout != NUM_PIPE_LAYOUT) m_commandList.IASetVertexBuffers(0, 1, nullptr);
}

InputLayout Model::CreateInputLayout(PipelineCache &pipelineCache)
{
	// Define vertex data layout for post-transformed objects
	const auto offset = 0xffffffff;
	InputElementTable inputElementDescs =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 0,		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R16G16_FLOAT,		0, offset,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	return pipelineCache.CreateInputLayout(inputElementDescs);
}

shared_ptr<SDKMesh> Model::LoadSDKMesh(const Device &device, const wstring &meshFileName,
	const TextureCache &textureCache, bool isStaticMesh)
{
	// Load the mesh
	const auto mesh = make_shared<SDKMesh>();
	N_RETURN(mesh->Create(device, meshFileName.c_str(), textureCache, isStaticMesh), nullptr);

	return mesh;
}

bool Model::createConstantBuffers()
{
	N_RETURN(m_cbMatrices.Create(m_device, sizeof(CBMatrices[FrameCount]), FrameCount,
		nullptr, m_name.empty() ? nullptr : (m_name + L".CBMatrices").c_str()), false);
	N_RETURN(m_cbShadowMatrices.Create(m_device, sizeof(XMMATRIX[FrameCount][MAX_SHADOW_CASCADES]),
		MAX_SHADOW_CASCADES * FrameCount, nullptr, m_name.empty() ? nullptr :
		(m_name + L".CBShadowMatrices").c_str()), false);

	return true;
}

void Model::createPipelines(bool isStatic, const InputLayout &inputLayout, const Format *rtvFormats,
	uint32_t numRTVs, Format dsvFormat, Format shadowFormat)
{
	const auto defaultRtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	numRTVs = numRTVs > 0 ? numRTVs : 1;
	rtvFormats = rtvFormats ? rtvFormats : &defaultRtvFormat;
	dsvFormat = dsvFormat ? dsvFormat : DXGI_FORMAT_D24_UNORM_S8_UINT;
	shadowFormat = shadowFormat ? shadowFormat : DXGI_FORMAT_D16_UNORM;

	Graphics::State state;

	// Base pass
	{
		const auto vsBasePass = isStatic ? VS_BASE_PASS_STATIC : VS_BASE_PASS;
		const auto psBasePass = isStatic ? PS_BASE_PASS_STATIC : PS_BASE_PASS;
		const auto psAlphaTest = isStatic ? PS_ALPHA_TEST_STATIC : PS_ALPHA_TEST;

		// Get opaque pipeline
		state.IASetInputLayout(inputLayout);
		state.IASetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		state.SetPipelineLayout(m_pipelineLayouts[BASE_PASS]);
		state.SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsBasePass));
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psBasePass));
		state.OMSetRTVFormats(rtvFormats, numRTVs);
		state.OMSetDSVFormat(dsvFormat);
		m_pipelines[OPAQUE_FRONT] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".OpaqueFront").c_str());

		state.DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_EQUAL, *m_pipelineCache);
		m_pipelines[OPAQUE_FRONT_EQUAL] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".OpaqueFrontZEqual").c_str());

		// Get transparent pipeline
		state.RSSetState(Graphics::RasterizerPreset::CULL_NONE, *m_pipelineCache);
		state.DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_LESS_EQUAL, *m_pipelineCache);
		state.OMSetBlendState(Graphics::BlendPreset::AUTO_NON_PREMUL, *m_pipelineCache);
		m_pipelines[ALPHA_TWO_SIDED] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".AlphaTwoSided").c_str());

		// Get alpha-test pipeline
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psAlphaTest));
		state.DSSetState(Graphics::DepthStencilPreset::DEFAULT_LESS, *m_pipelineCache);
		state.OMSetBlendState(Graphics::DEFAULT_OPAQUE, *m_pipelineCache);
		m_pipelines[OPAQUE_TWO_SIDED] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".OpaqueTwoSided").c_str());

		state.DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_EQUAL, *m_pipelineCache);
		m_pipelines[OPAQUE_TWO_SIDED_EQUAL] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".OpaqueTwoSidedZEqual").c_str());
	}

	// Depth pass
	{
		const Format nullRtvFormats[8] = {};
		const auto vsDepthPass = isStatic ? VS_DEPTH_STATIC : VS_DEPTH;

		// Get depth pipeline
		state.SetPipelineLayout(m_pipelineLayouts[DEPTH_PASS]);
		state.SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsDepthPass));
		state.SetShader(Shader::Stage::PS, nullptr);
		state.RSSetState(Graphics::RasterizerPreset::CULL_BACK, *m_pipelineCache);
		state.DSSetState(Graphics::DepthStencilPreset::DEFAULT_LESS, *m_pipelineCache);
		state.OMSetRTVFormats(nullRtvFormats, 8);
		state.OMSetNumRenderTargets(0);
		m_pipelines[DEPTH_FRONT] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".DepthFront").c_str());

		state.OMSetDSVFormat(shadowFormat);
		m_pipelines[SHADOW_FRONT] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".ShadowFront").c_str());

		// Get depth alpha-test pipeline
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_DEPTH));
		state.RSSetState(Graphics::RasterizerPreset::CULL_NONE, *m_pipelineCache);
		state.OMSetDSVFormat(dsvFormat);
		m_pipelines[DEPTH_TWO_SIDED] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".DepthTwoSided").c_str());

		state.OMSetDSVFormat(shadowFormat);
		m_pipelines[SHADOW_TWO_SIDED] = state.GetPipeline(*m_pipelineCache,
			m_name.empty() ? nullptr : (m_name + L".ShadowTwoSided").c_str());
	}

	// Get reflected pipeline
	//state.RSSetState(Graphics::RasterizerPreset::CULL_FRONT, *m_pipelineCache);
	//state.OMSetBlendState(Graphics::BlendPreset::DEFAULT_OPAQUE, *m_pipelineCache);
	//m_pipelines[REFLECTED] = state.GetPipeline(*m_pipelineCache);
}

void Model::createDescriptorTables()
{
	for (auto i = 0ui8; i < FrameCount; ++i)
	{
		Util::DescriptorTable cbMatricesTable;
		cbMatricesTable.SetDescriptors(0, 1, &m_cbMatrices.GetCBV(i));
		m_cbvTables[i][CBV_MATRICES] = cbMatricesTable.GetCbvSrvUavTable(*m_descriptorTableCache);

		for (auto j = 0ui8; j < MAX_SHADOW_CASCADES; ++j)
		{
			Util::DescriptorTable cbShadowTable;
			cbShadowTable.SetDescriptors(0, 1, &m_cbShadowMatrices.GetCBV(i * MAX_SHADOW_CASCADES + j));
			m_cbvTables[i][CBV_SHADOW_MATRIX + j] = cbShadowTable.GetCbvSrvUavTable(*m_descriptorTableCache);
		}
	}

	Util::DescriptorTable samplerTable;
	const SamplerPreset samplers[] = { ANISOTROPIC_WRAP, POINT_WRAP, LINEAR_LESS_EQUAL };
	samplerTable.SetSamplers(0, static_cast<uint32_t>(size(samplers)), samplers, *m_descriptorTableCache);
	m_samplerTable = samplerTable.GetSamplerTable(*m_descriptorTableCache);
	
	// Materials
	const auto numMaterials = m_mesh->GetNumMaterials();
	m_srvTables.resize(numMaterials);
	for (auto m = 0u; m < numMaterials; ++m)
	{
		const auto pMaterial = m_mesh->GetMaterial(m);

		if (pMaterial && pMaterial->pAlbedo && pMaterial->pNormal)
		{
			Util::DescriptorTable srvTable;
			const Descriptor srvs[] = { pMaterial->pAlbedo->GetSRV(), pMaterial->pNormal->GetSRV() };
			srvTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
			m_srvTables[m] = srvTable.GetCbvSrvUavTable(*m_descriptorTableCache);
		}
		else m_srvTables[m] = nullptr;
	}
}

void Model::render(uint32_t mesh, SubsetFlags subsetFlags, PipelineLayoutIndex layout)
{
	assert((subsetFlags & SUBSET_FULL) != SUBSET_FULL);

	// Set IA parameters
	m_commandList.IASetIndexBuffer(m_mesh->GetIndexBufferView(mesh));

	// Set pipeline state
	if (layout != NUM_PIPE_LAYOUT) SetPipelineState(subsetFlags, layout);

	const auto materialType = subsetFlags & SUBSET_OPAQUE ? SUBSET_OPAQUE : SUBSET_ALPHA;
	const auto numSubsets = m_mesh->GetNumSubsets(mesh, materialType);
	for (auto subset = 0u; subset < numSubsets; ++subset)
	{
		// Get subset
		const auto pSubset = m_mesh->GetSubset(mesh, subset, materialType);
		const auto primType = m_mesh->GetPrimitiveType(SDKMeshPrimitiveType(pSubset->PrimitiveType));
		m_commandList.IASetPrimitiveTopology(primType);

		// Set material
		if (m_mesh->GetMaterial(pSubset->MaterialID) && m_srvTables[pSubset->MaterialID])
			m_commandList.SetGraphicsDescriptorTable(MATERIAL, m_srvTables[pSubset->MaterialID]);

		// Draw
		m_commandList.DrawIndexed(static_cast<uint32_t>(pSubset->IndexCount), 1,
			static_cast<uint32_t>(pSubset->IndexStart), static_cast<int32_t>(pSubset->VertexStart), 0);
	}
}

Util::PipelineLayout Model::initPipelineLayout(VertexShader vs, PixelShader ps)
{
	auto cbMatrices = 0u;
	auto cbPerFrame = cbMatrices + 1;
	auto cbPerObject = cbPerFrame + 1;
#if TEMPORAL_AA
	auto cbTempBias = cbPerObject + 1;
#endif
	auto txDiffuse = 0u;
	auto txNormal = txDiffuse + 1;
	auto txShadow = txNormal + 1;
	auto smpAnisoWrap = 0u;
	auto smpLinearCmp = smpAnisoWrap + 2;

	// Get constant buffer slots
	auto desc = D3D12_SHADER_INPUT_BIND_DESC();
	auto reflector = m_shaderPool->GetReflector(Shader::Stage::VS, vs);
	if (reflector)
	{
		auto hr = reflector->GetResourceBindingDescByName("cbMatrices", &desc);
		if (SUCCEEDED(hr)) cbMatrices = desc.BindPoint;

#if TEMPORAL_AA
		hr = reflector->GetResourceBindingDescByName("cbTempBias", &desc);
		if (SUCCEEDED(hr)) cbTempBias = desc.BindPoint;
#endif
	}

	auto cbImmutable = cbMatrices;
	auto cbShadow = cbPerObject;
	reflector = m_shaderPool->GetReflector(Shader::Stage::PS, ps);
	if (reflector)
	{
		// Get shader resource slots
		auto hr = reflector->GetResourceBindingDescByName("g_txAlbedo", &desc);
		if (SUCCEEDED(hr)) txDiffuse = desc.BindPoint;
		hr = reflector->GetResourceBindingDescByName("g_txNormal", &desc);
		if (SUCCEEDED(hr)) txNormal = desc.BindPoint;
		hr = reflector->GetResourceBindingDescByName("g_txShadow", &desc);
		if (SUCCEEDED(hr)) txShadow = desc.BindPoint;

		// Get constant buffer slots
		hr = reflector->GetResourceBindingDescByName("cbImmutable", &desc);
		cbImmutable = SUCCEEDED(hr) ? desc.BindPoint : UINT32_MAX;

		hr = reflector->GetResourceBindingDescByName("cbShadow", &desc);
		cbShadow = SUCCEEDED(hr) ? desc.BindPoint : UINT32_MAX;

		hr = reflector->GetResourceBindingDescByName("cbPerObject", &desc);
		if (SUCCEEDED(hr)) cbPerObject = desc.BindPoint;

		// Get sampler slots
		hr = reflector->GetResourceBindingDescByName("g_smpLinear", &desc);
		if (SUCCEEDED(hr)) smpAnisoWrap = desc.BindPoint;
		hr = reflector->GetResourceBindingDescByName("g_smpCmpLinear", &desc);
		if (SUCCEEDED(hr)) smpLinearCmp = desc.BindPoint;
	}

	// Pipeline layout utility
	Util::PipelineLayout utilPipelineLayout;

	// Constant buffers
	utilPipelineLayout.SetRange(MATRICES, DescriptorType::CBV, 1, cbMatrices,
		0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	utilPipelineLayout.SetShaderStage(MATRICES, Shader::Stage::VS);

	if (ps != PS_DEPTH && cbImmutable != UINT32_MAX)
	{
		utilPipelineLayout.SetRange(IMMUTABLE, DescriptorType::CBV, 1, cbImmutable,
			0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		utilPipelineLayout.SetShaderStage(IMMUTABLE, Shader::Stage::PS);
	}

#if TEMPORAL_AA
	utilPipelineLayout.SetConstants(TEMPORAL_BIAS, 2, cbTempBias, 0, Shader::Stage::VS);
#endif

	// Textures (material and shadow)
	utilPipelineLayout.SetRange(MATERIAL, DescriptorType::SRV, 1, txDiffuse,
			0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	if (ps != PS_DEPTH) utilPipelineLayout.SetRange(MATERIAL, DescriptorType::SRV,
		1, txNormal, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	utilPipelineLayout.SetShaderStage(MATERIAL, Shader::Stage::PS);

	if (ps == PS_DEPTH) utilPipelineLayout.SetConstants(ALPHA_REF, 2, cbPerObject, 0, Shader::Stage::PS);
	else
	{
		utilPipelineLayout.SetRange(SHADOW_MAP, DescriptorType::SRV, 1, txShadow);
		if (cbShadow != UINT32_MAX) utilPipelineLayout.SetRange(SHADOW_MAP, DescriptorType::CBV,
			1, cbShadow, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		utilPipelineLayout.SetShaderStage(SHADOW_MAP, Shader::Stage::PS);
	}

	// Samplers
	utilPipelineLayout.SetRange(SAMPLERS, DescriptorType::SAMPLER, 2, smpAnisoWrap);
	if (ps != PS_DEPTH) utilPipelineLayout.SetRange(SAMPLERS, DescriptorType::SAMPLER, 1, smpLinearCmp);
	utilPipelineLayout.SetShaderStage(SAMPLERS, Shader::Stage::PS);

	return utilPipelineLayout;
}
