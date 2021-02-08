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
Character::uptr Character::MakeUnique(const Device& device, const wchar_t* name)
{
	return make_unique<Character_Impl>(device, name);
}

Character::sptr Character::MakeShared(const Device& device, const wchar_t* name)
{
	return make_shared<Character_Impl>(device, name);
}

//--------------------------------------------------------------------------------------
// Static interface function
//--------------------------------------------------------------------------------------
SDKMesh::sptr Character::LoadSDKMesh(const Device& device, const wstring& meshFileName,
	const wstring& animFileName, const TextureCache& textureCache,
	const shared_ptr<vector<MeshLink>>& meshLinks,
	vector<SDKMesh::sptr>* linkedMeshes)
{
	// Load the animated mesh
	const auto mesh = Model::LoadSDKMesh(device, meshFileName, textureCache, false);
	N_RETURN(mesh->LoadAnimation(animFileName.c_str()), nullptr);
	mesh->TransformBindPose(XMMatrixIdentity());

	// Fix the frame name to avoid space
	const auto numFrames = mesh->GetNumFrames();
	for (auto i = 0u; i < numFrames; ++i)
	{
		auto szName = mesh->GetFrame(i)->Name;
		for (auto j = 0ui8; szName[j] != '\0'; ++j)
			if (szName[j] == ' ') szName[j] = '_';
	}

	// Load the linked meshes
	if (meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(meshLinks->size());
		linkedMeshes->resize(numLinks);

		for (auto m = 0ui8; m < numLinks; ++m)
		{
			auto& meshInfo = meshLinks->at(m);
			meshInfo.BoneIndex = mesh->FindFrameIndex(meshInfo.BoneName.c_str());
			linkedMeshes->at(m) = SDKMesh::MakeShared();
			N_RETURN(linkedMeshes->at(m)->Create(device, meshInfo.MeshName.c_str(),
				textureCache), nullptr);
		}
	}

	return mesh;
}

//--------------------------------------------------------------------------------------
// Character implementations
//--------------------------------------------------------------------------------------
Character_Impl::Character_Impl(const Device& device, const wchar_t* name) :
	Model_Impl(device, name),
	m_computePipelineCache(nullptr),
	m_skinningPipelineLayout(nullptr),
	m_skinningPipeline(nullptr),
	m_srvSkinningTables(),
	m_uavSkinningTables(),
#if TEMPORAL
	m_srvSkinnedTables(),
	m_linkedWorldViewProjs(),
#endif
	m_linkedMeshes(nullptr),
	m_meshLinks(nullptr),
	m_cbLinkedMatrices(0),
	m_cbLinkedShadowMatrices(0)
{
}

Character_Impl::~Character_Impl(void)
{
}

bool Character_Impl::Init(const InputLayout* pInputLayout,
	const shared_ptr<SDKMesh>& mesh,
	const ShaderPool::sptr& shaderPool,
	const Graphics::PipelineCache::sptr& graphicsPipelineCache,
	const Compute::PipelineCache::sptr& computePipelineCache,
	const PipelineLayoutCache::sptr& pipelineLayoutCache,
	const DescriptorTableCache::sptr& descriptorTableCache,
	const shared_ptr<vector<SDKMesh>>& linkedMeshes,
	const shared_ptr<vector<MeshLink>>& meshLinks,
	const Format* rtvFormats, uint32_t numRTVs,
	Format dsvFormat, Format shadowFormat)
{
	m_computePipelineCache = computePipelineCache;

	// Set the Linked Meshes
	m_meshLinks = meshLinks;
	m_linkedMeshes = linkedMeshes;

	// Get SDKMesh
	N_RETURN(Model_Impl::Init(pInputLayout, mesh, shaderPool, graphicsPipelineCache,
		pipelineLayoutCache, descriptorTableCache), false);

	// Create buffers
	N_RETURN(createBuffers(), false);

	// Create VBs that will hold all of the skinned vertices that need to be transformed output
	N_RETURN(createTransformedStates(), false);

	// Create pipeline layout, pipelines, and descriptor tables
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(pInputLayout, rtvFormats, numRTVs, dsvFormat, shadowFormat), false);
	N_RETURN(createDescriptorTables(), false);

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

void Character_Impl::Update(uint8_t frameIndex, double time, CXMMATRIX viewProj,
	FXMMATRIX* pWorld, FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
{
	Model_Impl::Update(frameIndex);

	// Set the bone matrices
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_mesh->TransformMesh(XMMatrixIdentity(), time);
	setSkeletalMatrices(numMeshes);

	SetMatrices(viewProj, pWorld, pShadows, numShadows, isTemporal);
	m_time = -1.0;
}

void Character_Impl::SetMatrices(CXMMATRIX viewProj, FXMMATRIX* pWorld,
	FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
{
	XMMATRIX world;
	if (!pWorld)
	{
		const auto translation = XMMatrixTranslation(m_vPosRot.x, m_vPosRot.y, m_vPosRot.z);
		const auto rotation = XMMatrixRotationY(m_vPosRot.w);
		world = XMMatrixMultiply(rotation, translation);
		XMStoreFloat4x4(&m_mWorld, world);
	}
	else world = *pWorld;

	Model_Impl::SetMatrices(viewProj, world, pShadows, numShadows, isTemporal);

	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		for (auto m = 0ui8; m < numLinks; ++m)
			setLinkedMatrices(m, viewProj, world, pShadows, numShadows, isTemporal);
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
	SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances)
{
	renderTransformed(pCommandList, layout, subsetFlags, matrixTableIndex, numInstances);
	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		//for (auto m = 0ui8; m < numLinks; ++m)
		//	renderLinked(m, matrixTableIndex, layout, numInstances);
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

bool Character_Impl::createTransformedStates()
{
	for (auto i = 0u; i < FrameCount; ++i)
	{
		m_transformedVBs[i] = VertexBuffer::MakeUnique();
		N_RETURN(createTransformedVBs(*m_transformedVBs[i]), false);
	}

	return true;
}

bool Character_Impl::createTransformedVBs(VertexBuffer& vertexBuffer)
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

	N_RETURN(vertexBuffer.Create(m_device, numVertices, sizeof(Vertex),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		numMeshes, firstVertices.data(), numMeshes, firstVertices.data(),
		numMeshes, firstVertices.data(), m_name.empty() ? nullptr :
		(m_name + L".TransformedVB").c_str()), false);

	return true;
}

bool Character_Impl::createBuffers()
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

	for (auto i = 0ui8; i < FrameCount; ++i)
	{
		m_boneWorlds[i] = StructuredBuffer::MakeUnique();
		N_RETURN(m_boneWorlds[i]->Create(m_device, numElements, sizeof(XMFLOAT4X3), ResourceFlag::NONE,
			MemoryType::UPLOAD, numMeshes, firstElements.data(), 1, nullptr, m_name.empty() ?
			nullptr : (m_name + L".BoneWorld" + to_wstring(i)).c_str()), false);
	}

	// Linked meshes
	if (m_meshLinks) m_cbLinkedMatrices.resize(m_meshLinks->size());
	for (auto& cbLinkedMatrices : m_cbLinkedMatrices)
	{
		cbLinkedMatrices = ConstantBuffer::MakeUnique();
		N_RETURN(cbLinkedMatrices->Create(m_device, sizeof(CBMatrices[FrameCount]), FrameCount), false);
	}

	if (m_meshLinks) m_cbLinkedShadowMatrices.resize(m_meshLinks->size());
	for (auto& cbLinkedMatrix : m_cbLinkedShadowMatrices)
	{
		cbLinkedMatrix = ConstantBuffer::MakeUnique();
		N_RETURN(cbLinkedMatrix->Create(m_device, sizeof(XMFLOAT4X4[FrameCount][MAX_SHADOW_CASCADES]),
			MAX_SHADOW_CASCADES * FrameCount), false);
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
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();

		// Input vertices and bone matrices
		utilPipelineLayout->SetRange(INPUT, DescriptorType::SRV, 1, roBoneWorld, 0, DescriptorFlag::DATA_STATIC);
		utilPipelineLayout->SetRange(INPUT, DescriptorType::SRV, 1, roVertices, 0, DescriptorFlag::DESCRIPTORS_VOLATILE);
		utilPipelineLayout->SetShaderStage(INPUT, Shader::Stage::CS);

		// Output vertices
		utilPipelineLayout->SetRange(OUTPUT, DescriptorType::UAV, 1, rwVertices, 0,
			DescriptorFlag::DESCRIPTORS_VOLATILE | DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		utilPipelineLayout->SetShaderStage(OUTPUT, Shader::Stage::CS);

		// Get pipeline layout
		X_RETURN(m_skinningPipelineLayout, utilPipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, m_name.empty() ? nullptr : (m_name + L".SkinningLayout").c_str()), false);
	}

	// Base pass
	{
		auto cbPerFrame = 1u;
#if TEMPORAL
		auto roVertices = 0u;

		// Get vertex shader slots
		auto reflector = m_shaderPool->GetReflector(Shader::Stage::VS, VS_BASE_PASS);
		if (reflector && reflector->IsValid())
			// Get shader resource slot
			roVertices = reflector->GetResourceBindingPointByName("g_roVertices", roVertices);
#endif

		// Get pixel shader slots
		reflector = m_shaderPool->GetReflector(Shader::Stage::PS, PS_BASE_PASS);
		if (reflector && reflector->IsValid())
			// Get constant buffer slot
			cbPerFrame = reflector->GetResourceBindingPointByName("cbPerFrame");

		const auto utilPipelineLayout = initPipelineLayout(VS_BASE_PASS, PS_BASE_PASS);

#if TEMPORAL
		utilPipelineLayout->SetRange(HISTORY, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout->SetShaderStage(HISTORY, Shader::Stage::VS);
#endif

		if (cbPerFrame != UINT32_MAX)
		{
			utilPipelineLayout->SetRange(PER_FRAME, DescriptorType::CBV, 1, cbPerFrame, 0, DescriptorFlag::DATA_STATIC);
			utilPipelineLayout->SetShaderStage(PER_FRAME, Shader::Stage::PS);
		}

		X_RETURN(m_pipelineLayouts[BASE_PASS], utilPipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".BasePassLayout").c_str()), false);
	}

	// Depth pass
	{
		const auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_NULL_INDEX);

		X_RETURN(m_pipelineLayouts[DEPTH_PASS], utilPipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthPassLayout").c_str()), false);
	}

	// Depth alpha pass
	{
		const auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_DEPTH);

		X_RETURN(m_pipelineLayouts[DEPTH_ALPHA_PASS], utilPipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthAlphaPassLayout").c_str()), false);
	}

	return true;
}

bool Character_Impl::createPipelines(const InputLayout* pInputLayout, const Format* rtvFormats,
	uint32_t numRTVs, Format dsvFormat, Format shadowFormat)
{
	// Skinning
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_skinningPipelineLayout);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, CS_SKINNING));
		X_RETURN(m_skinningPipeline, state->GetPipeline(*m_computePipelineCache,
			m_name.empty() ? nullptr : (m_name + L".SkinningPipe").c_str()), false);
	}

	// Rendering
	return Model_Impl::createPipelines(false, pInputLayout, rtvFormats, numRTVs, dsvFormat, shadowFormat);
}

bool Character_Impl::createDescriptorTables()
{
	const auto numMeshes = m_mesh->GetNumMeshes();

	for (auto i = 0ui8; i < FrameCount; ++i)
	{
		m_srvSkinningTables[i].resize(numMeshes);
		m_uavSkinningTables[i].resize(numMeshes);
#if TEMPORAL
		m_srvSkinnedTables[i].resize(numMeshes);
#endif

		for (auto m = 0u; m < numMeshes; ++m)
		{
			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique();
				const Descriptor descriptors[] = { m_boneWorlds[i]->GetSRV(m), m_mesh->GetVertexBufferSRV(m, 0) };
				descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
				X_RETURN(m_srvSkinningTables[i][m], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
			}

			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique();
				descriptorTable->SetDescriptors(0, 1, &m_transformedVBs[i]->GetUAV(m));
				X_RETURN(m_uavSkinningTables[i][m], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
			}

#if TEMPORAL
			{
				const auto descriptorTable = Util::DescriptorTable::MakeUnique();
				descriptorTable->SetDescriptors(0, 1, &m_transformedVBs[i]->GetSRV(m));
				X_RETURN(m_srvSkinnedTables[i][m], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
			}
#endif
		}
	}

	return true;
}

void Character_Impl::setLinkedMatrices(uint32_t mesh, CXMMATRIX viewProj, CXMMATRIX world,
	FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
{
	// Set World-View-Proj matrix
	const auto influenceMatrix = m_mesh->GetInfluenceMatrix(m_meshLinks->at(mesh).BoneIndex);
	const auto worldViewProj = influenceMatrix * world * viewProj;

	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbLinkedMatrices[mesh]->Map());
	XMStoreFloat4x4(&pCBData->WorldViewProj, XMMatrixTranspose(worldViewProj));
	XMStoreFloat3x4(&pCBData->World, world); // XMStoreFloat3x4 includes transpose.
	XMStoreFloat3x4(&pCBData->WorldIT, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));

	const auto model = XMMatrixMultiply(influenceMatrix, world);

	if (pShadows)
	{
		for (auto i = 0ui8; i < numShadows; ++i)
		{
			const auto shadow = XMMatrixMultiply(model, pShadows[i]);
			auto& cbData = *reinterpret_cast<XMMATRIX*>(m_cbLinkedShadowMatrices[mesh]->Map(m_currentFrame * MAX_SHADOW_CASCADES + i));
			cbData = XMMatrixTranspose(shadow);
		}
	}

#if TEMPORAL
	if (isTemporal)
	{
		XMStoreFloat4x4(&m_linkedWorldViewProjs[m_currentFrame][mesh], worldViewProj);
		const auto worldViewProjPrev = XMLoadFloat4x4(&m_linkedWorldViewProjs[m_previousFrame][mesh]);
		XMStoreFloat4x4(&pCBData->WorldViewProjPrev, XMMatrixTranspose(worldViewProjPrev));
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
		pCommandList->Dispatch(DIV_UP(numVertices, 64), 1, 1);
	}
}

void Character_Impl::renderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances)
{
	if (layout < GLOBAL_BASE_PASS)
	{
		const DescriptorPool descriptorPools[] =
		{
			m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
			m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
		};
		pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
		pCommandList->SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);
	}

	// Set matrices
	pCommandList->SetGraphicsDescriptorTable(MATRICES, m_cbvTables[m_currentFrame][matrixTableIndex]);

	const SubsetFlags subsetMasks[] = { SUBSET_OPAQUE, SUBSET_ALPHA_TEST, SUBSET_ALPHA };

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (const auto& subsetMask : subsetMasks)
	{
		if (layout < GLOBAL_BASE_PASS && subsetMask != SUBSET_OPAQUE)
			pCommandList->SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);

		if (subsetFlags & subsetMask)
		{
			for (auto m = 0u; m < numMeshes; ++m)
			{
				// Set IA parameters
				pCommandList->IASetVertexBuffers(0, 1, &m_transformedVBs[m_currentFrame]->GetVBV(m));

#if TEMPORAL
				// Set historical motion states, if neccessary
				if (layout == BASE_PASS || layout == GLOBAL_BASE_PASS)
					pCommandList->SetGraphicsDescriptorTable(HISTORY, m_srvSkinnedTables[m_previousFrame][m]);
#endif

				// Render mesh
				render(pCommandList, m, layout, ~SUBSET_FULL & subsetFlags | subsetMask, numInstances);
			}
		}
	}

	// Clear out the vb bindings for the next pass
	//if (layout < GLOBAL_BASS_PASS) m_commandList->IASetVertexBuffers(0, 1, nullptr);
}

#if 0
void Character_Impl::renderLinked(uint32_t mesh, uint8_t matrixTableIndex,
	PipelineLayoutIndex layout, uint32_t numInstances)
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
	const auto pDataBoneWorld = reinterpret_cast<XMFLOAT4X3*>(m_boneWorlds[m_currentFrame]->Map(mesh));

	const auto numBones = m_mesh->GetNumInfluences(mesh);
	for (auto i = 0u; i < numBones; ++i)
	{
		const auto qMat = getDualQuat(mesh, i);
		XMStoreFloat4x3(&pDataBoneWorld[i], XMMatrixTranspose(qMat));
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
