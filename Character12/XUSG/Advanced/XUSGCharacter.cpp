//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGCharacter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Character::Character(const Device& device, const wchar_t* name) :
	Model(device, name),
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

Character::~Character(void)
{
}

bool Character::Init(const InputLayout& inputLayout,
	const shared_ptr<SDKMesh>& mesh,
	const shared_ptr<ShaderPool>& shaderPool,
	const shared_ptr<Graphics::PipelineCache>& graphicsPipelineCache,
	const shared_ptr<Compute::PipelineCache>& computePipelineCache,
	const shared_ptr<PipelineLayoutCache>& pipelineLayoutCache,
	const shared_ptr<DescriptorTableCache>& descriptorTableCache,
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
	N_RETURN(Model::Init(inputLayout, mesh, shaderPool, graphicsPipelineCache,
		pipelineLayoutCache, descriptorTableCache), false);

	// Create buffers
	N_RETURN(createBuffers(), false);

	// Create VBs that will hold all of the skinned vertices that need to be transformed output
	N_RETURN(createTransformedStates(), false);

	// Create pipeline layout, pipelines, and descriptor tables
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(inputLayout, rtvFormats, numRTVs, dsvFormat, shadowFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void Character::InitPosition(const XMFLOAT4& posRot)
{
	m_vPosRot = posRot;
}

void Character::Update(uint8_t frameIndex, double time)
{
	Model::Update(frameIndex);
	m_time = time;
}

void Character::Update(uint8_t frameIndex, double time, CXMMATRIX viewProj, FXMMATRIX* pWorld,
	FXMMATRIX* pShadowView, FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
{
	Model::Update(frameIndex);

	// Set the bone matrices
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_mesh->TransformMesh(XMMatrixIdentity(), time);
	setSkeletalMatrices(numMeshes);

	SetMatrices(viewProj, pWorld, pShadowView, pShadows, numShadows, isTemporal);
	m_time = -1.0;
}

void Character::SetMatrices(CXMMATRIX viewProj, FXMMATRIX* pWorld,
	FXMMATRIX* pShadowView, FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
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

	Model::SetMatrices(viewProj, world, pShadowView, pShadows, numShadows, isTemporal);

	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		for (auto m = 0ui8; m < numLinks; ++m)
			setLinkedMatrices(m, viewProj, world, pShadowView, pShadows, numShadows, isTemporal);
	}
}

void Character::SetSkinningPipeline(const CommandList& commandList)
{
	commandList.SetComputePipelineLayout(m_skinningPipelineLayout);
	commandList.SetPipelineState(m_skinningPipeline);
}

void Character::Skinning(const CommandList& commandList, uint32_t& numBarriers,
	ResourceBarrier* pBarriers, bool reset)
{
	if (m_time >= 0.0) m_mesh->TransformMesh(XMMatrixIdentity(), m_time);
	m_transformedVBs[m_currentFrame].SetBarrier(pBarriers, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	skinning(commandList, reset);

	// Prepare VBV | SRV states for the vertex buffer
	numBarriers = m_transformedVBs[m_currentFrame].SetBarrier(pBarriers, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, numBarriers);
}

void Character::RenderTransformed(const CommandList& commandList, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances)
{
	renderTransformed(commandList, layout, subsetFlags, matrixTableIndex, numInstances);
	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		//for (auto m = 0ui8; m < numLinks; ++m)
		//	renderLinked(m, matrixTableIndex, layout, numInstances);
	}
}

const XMFLOAT4& Character::GetPosition() const
{
	return m_vPosRot;
}

FXMMATRIX Character::GetWorldMatrix() const
{
	return XMLoadFloat4x4(&m_mWorld);
}

shared_ptr<SDKMesh> Character::LoadSDKMesh(const Device& device, const wstring& meshFileName,
	const wstring& animFileName, const TextureCache& textureCache,
	const shared_ptr<vector<MeshLink>>& meshLinks,
	vector<SDKMesh>* linkedMeshes)
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
			N_RETURN(linkedMeshes->at(m).Create(device.get(), meshInfo.MeshName.c_str(),
				textureCache), nullptr);
		}
	}

	return mesh;
}

bool Character::createTransformedStates()
{
	for (auto i = 0u; i < FrameCount; ++i)
		N_RETURN(createTransformedVBs(m_transformedVBs[i],
			i + 1 < FrameCount ? D3D12_RESOURCE_STATE_COMMON :
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE), false);

	return true;
}

bool Character::createTransformedVBs(VertexBuffer& vertexBuffer, ResourceState state)
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
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_TYPE_DEFAULT, state,
		numMeshes, firstVertices.data(), numMeshes, firstVertices.data(),
		numMeshes, firstVertices.data(), m_name.empty() ? nullptr : (m_name + L".TransformedVB").c_str()), false);

	return true;
}

bool Character::createBuffers()
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
		N_RETURN(m_boneWorlds[i].Create(m_device, numElements, sizeof(XMFLOAT4X3), D3D12_RESOURCE_FLAG_NONE,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, numMeshes, firstElements.data(), 1, nullptr,
			m_name.empty() ? nullptr : (m_name + L".BoneWorld" + to_wstring(i)).c_str()), false);
	}

	// Linked meshes
	if (m_meshLinks) m_cbLinkedMatrices.resize(m_meshLinks->size());
	for (auto& cbLinkedMatrices : m_cbLinkedMatrices)
		N_RETURN(cbLinkedMatrices.Create(m_device, sizeof(CBMatrices[FrameCount]), FrameCount), false);

	if (m_meshLinks) m_cbLinkedShadowMatrices.resize(m_meshLinks->size());
	for (auto& cbLinkedMatrix : m_cbLinkedShadowMatrices)
		N_RETURN(cbLinkedMatrix.Create(m_device, sizeof(XMMATRIX[FrameCount][MAX_SHADOW_CASCADES]),
			MAX_SHADOW_CASCADES * FrameCount), false);

	return true;
}

bool Character::createPipelineLayouts()
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
		Util::PipelineLayout utilPipelineLayout;

		// Input vertices and bone matrices
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roBoneWorld,
			0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roVertices,
			0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		utilPipelineLayout.SetShaderStage(INPUT, Shader::Stage::CS);

		// Output vertices
		utilPipelineLayout.SetRange(OUTPUT, DescriptorType::UAV, 1, rwVertices,
			0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
		utilPipelineLayout.SetShaderStage(OUTPUT, Shader::Stage::CS);

		// Get pipeline layout
		X_RETURN(m_skinningPipelineLayout, utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_NONE, m_name.empty() ? nullptr : (m_name + L".SkinningLayout").c_str()), false);
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

		auto utilPipelineLayout = initPipelineLayout(VS_BASE_PASS, PS_BASE_PASS);

#if TEMPORAL
		utilPipelineLayout.SetRange(HISTORY, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout.SetShaderStage(HISTORY, Shader::Stage::VS);
#endif

		if (cbPerFrame != UINT32_MAX)
		{
			utilPipelineLayout.SetRange(PER_FRAME, DescriptorType::CBV, 1, cbPerFrame,
				0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			utilPipelineLayout.SetShaderStage(PER_FRAME, Shader::Stage::PS);
		}

		X_RETURN(m_pipelineLayouts[BASE_PASS], utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".BasePassLayout").c_str()), false);
	}

	// Depth pass
	{
		auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_NULL_INDEX);

		X_RETURN(m_pipelineLayouts[DEPTH_PASS], utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthPassLayout").c_str()), false);
	}

	// Depth alpha pass
	{
		auto utilPipelineLayout = initPipelineLayout(VS_DEPTH, PS_DEPTH);

		X_RETURN(m_pipelineLayouts[DEPTH_ALPHA_PASS], utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
			m_name.empty() ? nullptr : (m_name + L".DepthAlphaPassLayout").c_str()), false);
	}

	return true;
}

bool Character::createPipelines(const InputLayout& inputLayout, const Format* rtvFormats,
	uint32_t numRTVs, Format dsvFormat, Format shadowFormat)
{
	// Skinning
	{
		Compute::State state;
		state.SetPipelineLayout(m_skinningPipelineLayout);
		state.SetShader(m_shaderPool->GetShader(Shader::Stage::CS, CS_SKINNING));
		X_RETURN(m_skinningPipeline, state.GetPipeline(*m_computePipelineCache,
			m_name.empty() ? nullptr : (m_name + L".SkinningPipe").c_str()), false);
	}

	// Rendering
	return Model::createPipelines(false, inputLayout, rtvFormats, numRTVs, dsvFormat, shadowFormat);
}

bool Character::createDescriptorTables()
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
			Util::DescriptorTable srvSkinningTable;
			const Descriptor srvs[] = { m_boneWorlds[i].GetSRV(m), m_mesh->GetVertexBufferSRV(m, 0) };
			srvSkinningTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
			X_RETURN(m_srvSkinningTables[i][m], srvSkinningTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);

			Util::DescriptorTable uavSkinningTable;
			uavSkinningTable.SetDescriptors(0, 1, &m_transformedVBs[i].GetUAV(m));
			X_RETURN(m_uavSkinningTables[i][m], uavSkinningTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);

#if TEMPORAL
			Util::DescriptorTable srvSkinnedTable;
			srvSkinnedTable.SetDescriptors(0, 1, &m_transformedVBs[i].GetSRV(m));
			X_RETURN(m_srvSkinnedTables[i][m], srvSkinnedTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
#endif
		}
	}

	return true;
}

void Character::setLinkedMatrices(uint32_t mesh, CXMMATRIX viewProj, CXMMATRIX world,
	FXMMATRIX* pShadowView, FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal)
{
	// Set World-View-Proj matrix
	const auto influenceMatrix = m_mesh->GetInfluenceMatrix(m_meshLinks->at(mesh).BoneIndex);
	const auto worldViewProj = influenceMatrix * world * viewProj;

	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbLinkedMatrices[mesh].Map());
	pCBData->WorldViewProj = XMMatrixTranspose(worldViewProj);
	pCBData->World = XMMatrixTranspose(world);
	pCBData->Normal = XMMatrixInverse(nullptr, world);
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose once
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose twice

	const auto model = XMMatrixMultiply(influenceMatrix, world);
	if (pShadowView)
	{
		const auto shadow = XMMatrixMultiply(model, *pShadowView);
		pCBData->Shadow = XMMatrixTranspose(shadow);
	}

	if (pShadows)
	{
		for (auto i = 0ui8; i < numShadows; ++i)
		{
			const auto shadow = XMMatrixMultiply(model, pShadows[i]);
			auto& cbData = *reinterpret_cast<XMMATRIX*>(m_cbLinkedShadowMatrices[mesh].Map(m_currentFrame * MAX_SHADOW_CASCADES + i));
			cbData = XMMatrixTranspose(shadow);
		}
	}

#if TEMPORAL
	if (isTemporal)
	{
		XMStoreFloat4x4(&m_linkedWorldViewProjs[m_currentFrame][mesh], worldViewProj);
		const auto worldViewProjPrev = XMLoadFloat4x4(&m_linkedWorldViewProjs[m_previousFrame][mesh]);
		pCBData->WorldViewProjPrev = XMMatrixTranspose(worldViewProjPrev);
	}
#endif
}

void Character::skinning(const CommandList& commandList, bool reset)
{
	if (reset)
	{
		const DescriptorPool descriptorPools[] =
		{ m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL) };
		commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		commandList.SetComputePipelineLayout(m_skinningPipelineLayout);
		commandList.SetPipelineState(m_skinningPipeline);
	}

	const auto numMeshes = m_mesh->GetNumMeshes();

	// Set the bone matrices
	if (m_time >= 0.0) setSkeletalMatrices(numMeshes);

	// Skin the vertices and output them to buffers
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Setup descriptor tables
		commandList.SetComputeDescriptorTable(INPUT, m_srvSkinningTables[m_currentFrame][m]);
		commandList.SetComputeDescriptorTable(OUTPUT, m_uavSkinningTables[m_currentFrame][m]);

		// Skinning
		const auto numVertices = static_cast<uint32_t>(m_mesh->GetNumVertices(m, 0));
		commandList.Dispatch(DIV_UP(numVertices, 64), 1, 1);
	}
}

void Character::renderTransformed(const CommandList& commandList, PipelineLayoutIndex layout,
	SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances)
{
	if (layout < GLOBAL_BASE_PASS)
	{
		const DescriptorPool descriptorPools[] =
		{
			m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
			m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
		};
		commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[layout]);
		commandList.SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);
	}

	// Set matrices
	commandList.SetGraphicsDescriptorTable(MATRICES, m_cbvTables[m_currentFrame][matrixTableIndex]);

	const SubsetFlags subsetMasks[] = { SUBSET_OPAQUE, SUBSET_ALPHA_TEST, SUBSET_ALPHA };

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (const auto& subsetMask : subsetMasks)
	{
		if (layout < GLOBAL_BASE_PASS && subsetMask != SUBSET_OPAQUE)
			commandList.SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);

		if (subsetFlags & subsetMask)
		{
			for (auto m = 0u; m < numMeshes; ++m)
			{
				// Set IA parameters
				commandList.IASetVertexBuffers(0, 1, &m_transformedVBs[m_currentFrame].GetVBV(m));

#if TEMPORAL
				// Set historical motion states, if neccessary
				if (layout == BASE_PASS || layout == GLOBAL_BASE_PASS)
					commandList.SetGraphicsDescriptorTable(HISTORY, m_srvSkinnedTables[m_previousFrame][m]);
#endif

				// Render mesh
				render(commandList, m, layout, ~SUBSET_FULL & subsetFlags | subsetMask, numInstances);
			}
		}
	}

	// Clear out the vb bindings for the next pass
	//if (layout < GLOBAL_BASS_PASS) m_commandList->IASetVertexBuffers(0, 1, nullptr);
}

#if 0
void Character::renderLinked(uint32_t mesh, uint8_t matrixTableIndex,
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

void Character::setSkeletalMatrices(uint32_t numMeshes)
{
	for (auto m = 0u; m < numMeshes; ++m) setBoneMatrices(m);
	//m_boneWorlds[m_currentFrame].Unmap();
}

void Character::setBoneMatrices(uint32_t mesh)
{
	const auto pDataBoneWorld = reinterpret_cast<XMFLOAT4X3*>(m_boneWorlds[m_currentFrame].Map(mesh));

	const auto numBones = m_mesh->GetNumInfluences(mesh);
	for (auto i = 0u; i < numBones; ++i)
	{
		const auto qMat = getDualQuat(mesh, i);
		XMStoreFloat4x3(&pDataBoneWorld[i], XMMatrixTranspose(qMat));
	}
}

// Convert unit quaternion and translation to unit dual quaternion
void Character::convertToDQ(XMFLOAT4& dqTran, CXMVECTOR quat, const XMFLOAT3& tran) const
{
	XMFLOAT4 dqRot;
	XMStoreFloat4(&dqRot, quat);
	dqTran.x = 0.5f * (tran.x * dqRot.w + tran.y * dqRot.z - tran.z * dqRot.y);
	dqTran.y = 0.5f * (-tran.x * dqRot.z + tran.y * dqRot.w + tran.z * dqRot.x);
	dqTran.z = 0.5f * (tran.x * dqRot.y - tran.y * dqRot.x + tran.z * dqRot.w);
	dqTran.w = -0.5f * (tran.x * dqRot.x + tran.y * dqRot.y + tran.z * dqRot.z);
}

FXMMATRIX Character::getDualQuat(uint32_t mesh, uint32_t influence) const
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
