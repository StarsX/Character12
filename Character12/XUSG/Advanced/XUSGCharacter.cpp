//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "XUSGCharacter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

//--------------------------------------------------------------------------------------
// Create interfaces
//--------------------------------------------------------------------------------------
Character::uptr Character::MakeUnique(const wchar_t* name, API api)
{
	return make_unique<Character_Impl>(name, api);
}

Character::sptr Character::MakeShared(const wchar_t* name, API api)
{
	return make_shared<Character_Impl>(name, api);
}

//--------------------------------------------------------------------------------------
// Static interface function
//--------------------------------------------------------------------------------------
SDKMesh::sptr Character::LoadSDKMesh(const Device* pDevice, const wstring& meshFileName,
	const wstring& animFileName, const TextureCache& textureCache,
	const shared_ptr<vector<MeshLink>>& meshLinks,
	vector<SDKMesh::sptr>* linkedMeshes, API api)
{
	// Load the animated mesh
	const auto mesh = Model::LoadSDKMesh(pDevice, meshFileName, textureCache, false, api);
	XUSG_N_RETURN(mesh->LoadAnimation(animFileName.c_str()), nullptr);
	mesh->TransformBindPose(XMMatrixIdentity());

	// Fix the frame name to avoid space
	const auto numFrames = mesh->GetNumFrames();
	for (auto i = 0u; i < numFrames; ++i)
	{
		auto szName = mesh->GetFrame(i)->Name;
		for (uint8_t j = 0; szName[j] != '\0'; ++j)
			if (szName[j] == ' ') szName[j] = '_';
	}

	// Load the linked meshes
	if (meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(meshLinks->size());
		linkedMeshes->resize(numLinks);

		for (uint8_t m = 0; m < numLinks; ++m)
		{
			auto& meshInfo = meshLinks->at(m);
			meshInfo.BoneIndex = mesh->FindFrameIndex(meshInfo.BoneName.c_str());
			linkedMeshes->at(m) = SDKMesh::MakeShared(api);
			XUSG_N_RETURN(linkedMeshes->at(m)->Create(pDevice, meshInfo.MeshName.c_str(),
				textureCache), nullptr);
		}
	}

	return mesh;
}

//--------------------------------------------------------------------------------------
// Character implementations
//--------------------------------------------------------------------------------------
Character_Impl::Character_Impl(const wchar_t* name, API api) :
	Model_Impl(name, api),
	m_computePipelineCache(nullptr),
	m_skinningPipelineLayout(nullptr),
	m_skinningPipeline(nullptr),
	m_srvSkinningTables(),
	m_uavSkinningTables(),
#if XUSG_TEMPORAL
	m_srvSkinnedTables(),
	//m_linkedWorld,
#endif
	m_linkedMeshes(nullptr),
	m_meshLinks(nullptr),
	m_cbLinkedMatrices(0)
{
}

Character_Impl::~Character_Impl(void)
{
}

bool Character_Impl::Init(const Device* pDevice, const InputLayout* pInputLayout,
	const shared_ptr<SDKMesh>& mesh, const ShaderPool::sptr& shaderPool,
	const Graphics::PipelineCache::sptr& graphicsPipelineCache,
	const Compute::PipelineCache::sptr& computePipelineCache,
	const PipelineLayoutCache::sptr& pipelineLayoutCache,
	const DescriptorTableCache::sptr& descriptorTableCache,
	const shared_ptr<vector<SDKMesh>>& linkedMeshes,
	const shared_ptr<vector<MeshLink>>& meshLinks,
	const Format* rtvFormats, uint32_t numRTVs,
	Format dsvFormat, Format shadowFormat,
	bool twoSidedAll, bool useZEqual)
{
	m_computePipelineCache = computePipelineCache;

	// Set the Linked Meshes
	m_meshLinks = meshLinks;
	m_linkedMeshes = linkedMeshes;

	// Get SDKMesh
	XUSG_N_RETURN(Model_Impl::Init(pDevice, pInputLayout, mesh, shaderPool, graphicsPipelineCache,
		pipelineLayoutCache, descriptorTableCache, twoSidedAll), false);

	// Create buffers
	XUSG_N_RETURN(createBuffers(pDevice), false);

	// Create VBs that will hold all of the skinned vertices that need to be transformed output
	XUSG_N_RETURN(createTransformedStates(pDevice), false);

	// Create pipeline layout, pipelines, and descriptor tables
	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(pInputLayout, rtvFormats,
		numRTVs, dsvFormat, shadowFormat, useZEqual), false);
	XUSG_N_RETURN(createDescriptorTables(), false);

	return true;
}

void Character_Impl::InitPosition(const XMFLOAT4& posRot)
{
	m_vPosRot = posRot;
}

void Character_Impl::Update(uint8_t frameIndex, double time)
{
	Model_Impl::Update(frameIndex);
	m_time = time;
}

void Character_Impl::Update(uint8_t frameIndex, double time, FXMMATRIX* pWorld, bool isTemporal)
{
	Model_Impl::Update(frameIndex);

	// Set the bone matrices
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_mesh->TransformMesh(XMMatrixIdentity(), time);
	setSkeletalMatrices(numMeshes);

	SetMatrices(pWorld, isTemporal);
	m_time = -1.0;
}

void Character_Impl::SetMatrices(FXMMATRIX* pWorld, bool isTemporal)
{
	XMMATRIX world;
	if (!pWorld)
	{
		const auto translation = XMMatrixTranslation(m_vPosRot.x, m_vPosRot.y, m_vPosRot.z);
		const auto rotation = XMMatrixRotationY(m_vPosRot.w);
		world = rotation * translation;
		XMStoreFloat4x4(&m_mWorld, world);
	}
	else world = *pWorld;

	Model_Impl::SetMatrices(world, isTemporal);

	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		for (uint8_t m = 0; m < numLinks; ++m)
			setLinkedMatrices(m, world, isTemporal);
	}
}

void Character_Impl::SetSkinningPipeline(const CommandList* pCommandList)
{
	pCommandList->SetComputePipelineLayout(m_skinningPipelineLayout);
	pCommandList->SetPipelineState(m_skinningPipeline);
}

void Character_Impl::Skinning(const CommandList* pCommandList, uint32_t& numBarriers,
	ResourceBarrier* pBarriers, bool reset)
{
	if (m_time >= 0.0) m_mesh->TransformMesh(XMMatrixIdentity(), m_time);
	m_transformedVBs[m_currentFrame]->SetBarrier(pBarriers, ResourceState::UNORDERED_ACCESS); // Implicit state promotion
	skinning(pCommandList, reset);

	// Prepare VBV | SRV states for the vertex buffer
	numBarriers = m_transformedVBs[m_currentFrame]->SetBarrier(pBarriers, ResourceState::VERTEX_AND_CONSTANT_BUFFER |
		ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
}

void Character_Impl::RenderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances)
{
	renderTransformed(pCommandList, layout, subsetFlags, pCbvPerFrameTable, numInstances);
	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		//for (uint8_t m = 0; m < numLinks; ++m)
		//	renderLinked(m, layout, numInstances);
	}
}

const XMFLOAT4& Character_Impl::GetPosition() const
{
	return m_vPosRot;
}

FXMMATRIX Character_Impl::GetWorldMatrix() const
{
	return XMLoadFloat4x4(&m_mWorld);
}

bool Character_Impl::createTransformedStates(const Device* pDevice)
{
	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		m_transformedVBs[i] = VertexBuffer::MakeUnique(m_api);
		XUSG_N_RETURN(createTransformedVBs(pDevice, m_transformedVBs[i].get()), false);
	}

	return true;
}

bool Character_Impl::createTransformedVBs(const Device* pDevice, VertexBuffer* pVertexBuffer)
{
	// Create VBs that will hold all of the skinned vertices that need to be output
	auto numVertices = 0u;
	const auto numMeshes = m_mesh->GetNumMeshes();
	vector<uint32_t> firstVertices(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		firstVertices[m] = numVertices;
		numVertices += static_cast<uint32_t>(m_mesh->GetNumVertices(m, 0));
	}

	XUSG_N_RETURN(pVertexBuffer->Create(pDevice, numVertices, sizeof(Vertex),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		numMeshes, firstVertices.data(), numMeshes, firstVertices.data(),
		numMeshes, firstVertices.data(),MemoryFlag::NONE, m_name.empty() ?
		nullptr : (m_name + L".TransformedVB").c_str()), false);

	return true;
}

bool Character_Impl::createBuffers(const Device* pDevice)
{
	// Bone world matrices
	auto numElements = 0u;
	const auto numMeshes = m_mesh->GetNumMeshes();
	vector<uint32_t> firstElements(numMeshes);
	for (auto m = 0u; m < numMeshes; ++m)
	{
		firstElements[m] = numElements;
		numElements += m_mesh->GetNumInfluences(m);
	}

	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		m_boneWorlds[i] = StructuredBuffer::MakeUnique(m_api);
		XUSG_N_RETURN(m_boneWorlds[i]->Create(pDevice, numElements, sizeof(XMFLOAT3X4), ResourceFlag::NONE,
			MemoryType::UPLOAD, numMeshes, firstElements.data(), 1, nullptr, MemoryFlag::NONE,
			m_name.empty() ? nullptr : (m_name + L".BoneWorld" + to_wstring(i)).c_str()), false);
	}

	// Linked meshes
	if (m_meshLinks) m_cbLinkedMatrices.resize(m_meshLinks->size());
	for (auto& cbLinkedMatrices : m_cbLinkedMatrices)
	{
		cbLinkedMatrices = ConstantBuffer::MakeUnique(m_api);
		XUSG_N_RETURN(cbLinkedMatrices->Create(pDevice, sizeof(CBMatrices[FrameCount]), FrameCount), false);
	}

	return true;
}

bool Character_Impl::createPipelineLayouts()
{
	// Skinning
	{
		auto roBoneWorld = 0u;
		auto rwVertices = 0u;
		auto roVertices = roBoneWorld + 1;

		// Get compute shader slots
		const auto reflector = m_shaderPool->GetReflector(Shader::Stage::CS, CS_SKINNING);
		if (reflector && reflector->IsValid())
		{
			// Get shader resource slots
			rwVertices = reflector->GetResourceBindingPointByName("g_rwVertices", rwVertices);
			roBoneWorld = reflector->GetResourceBindingPointByName("g_roDualQuat", roBoneWorld);
			roVertices = reflector->GetResourceBindingPointByName("g_roVertices", roVertices);
		}

		// Pipeline layout utility
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique(m_api);

		// Input vertices and bone matrices
		utilPipelineLayout->SetRange(INPUT, DescriptorType::SRV, 1, roBoneWorld, 0, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetRange(INPUT, DescriptorType::SRV, 1, roVertices, 0, DescriptorFlag::DESCRIPTORS_VOLATILE);
		utilPipelineLayout->SetShaderStage(INPUT, Shader::Stage::CS);

		// Output vertices
		utilPipelineLayout->SetRange(OUTPUT, DescriptorType::UAV, 1, rwVertices, 0,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE);
		utilPipelineLayout->SetShaderStage(OUTPUT, Shader::Stage::CS);

		// Get pipeline layout
		XUSG_X_RETURN(m_skinningPipelineLayout, utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, m_name.empty() ? nullptr : (m_name + L".SkinningLayout").c_str()), false);
	}

	// Base pass
	{
		auto cbPerFrame = 1u;
#if XUSG_TEMPORAL
		auto roVertices = 0u;

		// Get vertex shader slots
		auto reflector = m_shaderPool->GetReflector(Shader::Stage::VS, VS_BASE_PASS);
		if (reflector && reflector->IsValid())
			// Get shader resource slot
			roVertices = reflector->GetResourceBindingPointByName("g_roVertices", roVertices);
#endif

		const auto utilPipelineLayout = initPipelineLayout(VS_BASE_PASS, PS_BASE_PASS);

#if XUSG_TEMPORAL
		utilPipelineLayout->SetRange(HISTORY, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout->SetShaderStage(HISTORY, Shader::Stage::VS);
#endif

		XUSG_X_RETURN(m_pipelineLayouts[BASE_PASS], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".BasePassLayout").c_str()), false);
	}

	// Depth pass
	{
		const auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_NULL_INDEX);

		XUSG_X_RETURN(m_pipelineLayouts[DEPTH_PASS], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthPassLayout").c_str()), false);
	}

	// Depth alpha pass
	{
		const auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_DEPTH);

		XUSG_X_RETURN(m_pipelineLayouts[DEPTH_ALPHA_PASS], utilPipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthAlphaPassLayout").c_str()), false);
	}

	return true;
}

bool Character_Impl::createPipelines(const InputLayout* pInputLayout, const Format* rtvFormats,
	uint32_t numRTVs, Format dsvFormat, Format shadowFormat, bool useZEqual)
{
	// Skinning
	{
		const auto state = Compute::State::MakeUnique(m_api);
		state->SetPipelineLayout(m_skinningPipelineLayout);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, CS_SKINNING));
		XUSG_X_RETURN(m_skinningPipeline, state->GetPipeline(m_computePipelineCache.get(),
			m_name.empty() ? nullptr : (m_name + L".SkinningPipe").c_str()), false);
	}

	// Rendering
	return Model_Impl::createPipelines(pInputLayout, rtvFormats,
		numRTVs, dsvFormat, shadowFormat, false, useZEqual);
}

bool Character_Impl::createDescriptorTables()
{
	const auto numMeshes = m_mesh->GetNumMeshes();

	for (uint8_t i = 0; i < FrameCount; ++i)
	{
		m_srvSkinningTables[i].resize(numMeshes);
		m_uavSkinningTables[i].resize(numMeshes);
#if XUSG_TEMPORAL
		m_srvSkinnedTables[i].resize(numMeshes);
#endif

		for (auto m = 0u; m < numMeshes; ++m)
		{
			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique(m_api);
				const Descriptor descriptors[] = { m_boneWorlds[i]->GetSRV(m), m_mesh->GetVertexBufferSRV(m, 0) };
				descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
				XUSG_X_RETURN(m_srvSkinningTables[i][m], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
			}

			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique(m_api);
				descriptorTable->SetDescriptors(0, 1, &m_transformedVBs[i]->GetUAV(m));
				XUSG_X_RETURN(m_uavSkinningTables[i][m], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
			}

#if XUSG_TEMPORAL
			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique(m_api);
				descriptorTable->SetDescriptors(0, 1, &m_transformedVBs[i]->GetSRV(m));
				XUSG_X_RETURN(m_srvSkinnedTables[i][m], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
			}
#endif
		}
	}

	return true;
}

void Character_Impl::setLinkedMatrices(uint32_t mesh, CXMMATRIX world, bool isTemporal)
{
	// Set World-View-Proj matrix
	const auto influenceMatrix = m_mesh->GetInfluenceMatrix(m_meshLinks->at(mesh).BoneIndex);
	const auto linkedWorld = influenceMatrix * world;

	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbLinkedMatrices[mesh]->Map());
	XMStoreFloat3x4(&pCBData->World, linkedWorld); // XMStoreFloat3x4 includes transpose.
	XMStoreFloat3x4(&pCBData->WorldIT, XMMatrixTranspose(XMMatrixInverse(nullptr, linkedWorld)));

#if XUSG_TEMPORAL
	if (isTemporal)
	{
		pCBData->WorldPrev = m_linkedWorlds[mesh];
		XMStoreFloat3x4(&m_linkedWorlds[mesh], linkedWorld);
	}
#endif
}

void Character_Impl::skinning(const CommandList* pCommandList, bool reset)
{
	if (reset)
	{
		const DescriptorPool descriptorPools[] =
		{ m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL) };
		pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		pCommandList->SetComputePipelineLayout(m_skinningPipelineLayout);
		pCommandList->SetPipelineState(m_skinningPipeline);
	}

	const auto numMeshes = m_mesh->GetNumMeshes();

	// Set the bone matrices
	if (m_time >= 0.0) setSkeletalMatrices(numMeshes);

	// Skin the vertices and output them to buffers
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Setup descriptor tables
		pCommandList->SetComputeDescriptorTable(INPUT, m_srvSkinningTables[m_currentFrame][m]);
		pCommandList->SetComputeDescriptorTable(OUTPUT, m_uavSkinningTables[m_currentFrame][m]);

		// Skinning
		const auto numVertices = static_cast<uint32_t>(m_mesh->GetNumVertices(m, 0));
		pCommandList->Dispatch(XUSG_DIV_UP(numVertices, 64), 1, 1);
	}
}

void Character_Impl::renderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances)
{
	if (pCbvPerFrameTable)
	{
		const auto descriptorPool = m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL);
		pCommandList->SetDescriptorPools(1, &descriptorPool);
		pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
		pCommandList->SetGraphicsDescriptorTable(PER_FRAME, *pCbvPerFrameTable);
#if XUSG_TEMPORAL_AA
		pCommandList->SetGraphicsDescriptorTable(TEMPORAL_BIAS, m_cbvTables[m_currentFrame][CBV_LOCAL_TEMPORAL_BIAS]);
#endif
	}

	// Set matrices
	pCommandList->SetGraphicsDescriptorTable(MATRICES, m_cbvTables[m_currentFrame][CBV_MATRICES]);

	const SubsetFlags subsetMasks[] = { SUBSET_OPAQUE, SUBSET_ALPHA_TEST, SUBSET_ALPHA };

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (const auto& subsetMask : subsetMasks)
	{
		if (subsetFlags & subsetMask)
		{
			for (auto m = 0u; m < numMeshes; ++m)
			{
				// Set IA parameters
				pCommandList->IASetVertexBuffers(0, 1, &m_transformedVBs[m_currentFrame]->GetVBV(m));

#if XUSG_TEMPORAL
				// Set historical motion states, if neccessary
				if (layout == BASE_PASS)
					pCommandList->SetGraphicsDescriptorTable(HISTORY, m_srvSkinnedTables[m_previousFrame][m]);
#endif

				// Render mesh
				render(pCommandList, m, layout, ~SUBSET_FULL & subsetFlags | subsetMask, pCbvPerFrameTable, numInstances);
			}
		}
	}

	// Clear out the vb bindings for the next pass
	//if (pCbvPerFrameTable) pCommandList->IASetVertexBuffers(0, 1, nullptr);
}

#if 0
void Character_Impl::renderLinked(uint32_t mesh, PipelineLayoutIndex layout, uint32_t numInstances)
{
	// Set constant buffers
	m_pDXContext->VSSetConstantBuffers(m_uCBMatrices, 1, m_vpCBLinkedMats[uMesh].GetAddressOf());

	// Set Shaders
	setShaders(uVS, uGS, uPS, bReset);
	m_pDXContext->RSSetState(m_pState->CullCounterClockwise());
	m_pDXContext->OMSetBlendState(m_pState->AutoAlphaBlend(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);

	m_pDXContext->IASetInputLayout(m_pVertexLayout);
	m_pvLinkedMeshes->at(uMesh).Render(m_pDXContext, m_uSRDiffuse, m_uSRNormal);

	resetShaders(uVS, uGS, uPS, SUBSET_FULL, bReset);
}
#endif

void Character_Impl::setSkeletalMatrices(uint32_t numMeshes)
{
	for (auto m = 0u; m < numMeshes; ++m) setBoneMatrices(m);
	//m_boneWorlds[m_currentFrame].Unmap();
}

void Character_Impl::setBoneMatrices(uint32_t mesh)
{
	const auto pDataBoneWorld = reinterpret_cast<XMFLOAT3X4*>(m_boneWorlds[m_currentFrame]->Map(mesh));

	const auto numBones = m_mesh->GetNumInfluences(mesh);
	for (auto i = 0u; i < numBones; ++i)
	{
		const auto qMat = getDualQuat(mesh, i);
		XMStoreFloat3x4(&pDataBoneWorld[i], XMMatrixTranspose(qMat));
	}
}

// Convert unit quaternion and translation to unit dual quaternion
void Character_Impl::convertToDQ(XMFLOAT4& dqTran, CXMVECTOR quat, const XMFLOAT3& tran) const
{
	XMFLOAT4 dqRot;
	XMStoreFloat4(&dqRot, quat);
	dqTran.x = 0.5f * (tran.x * dqRot.w + tran.y * dqRot.z - tran.z * dqRot.y);
	dqTran.y = 0.5f * (-tran.x * dqRot.z + tran.y * dqRot.w + tran.z * dqRot.x);
	dqTran.z = 0.5f * (tran.x * dqRot.y - tran.y * dqRot.x + tran.z * dqRot.w);
	dqTran.w = -0.5f * (tran.x * dqRot.x + tran.y * dqRot.y + tran.z * dqRot.z);
}

FXMMATRIX Character_Impl::getDualQuat(uint32_t mesh, uint32_t influence) const
{
	const auto influenceMatrix = m_mesh->GetMeshInfluenceMatrix(mesh, influence);

	XMMATRIX quatMatrix;
	XMMatrixDecompose(&quatMatrix.r[2], &quatMatrix.r[0], &quatMatrix.r[1], influenceMatrix);

	XMFLOAT3 translation;
	XMStoreFloat3(&translation, quatMatrix.r[1]);

	XMFLOAT4 dqTran;
	convertToDQ(dqTran, quatMatrix.r[0], translation);
	quatMatrix.r[1] = XMLoadFloat4(&dqTran);

	return quatMatrix;
}
