//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGCharacter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Character::Character(const Device &device, const CommandList &commandList) :
	Model(device, commandList),
	m_linkedMeshes(nullptr),
	m_meshLinks(nullptr),
	m_computePipelineCache(nullptr),
	m_transformedVBs(),
	m_cbLinkedMatrices(0),
	m_cbLinkedShadowMatrices(0),
	m_skinningPipelineLayout(nullptr),
	m_skinningPipeline(nullptr),
	m_srvSkinningTables(),
	m_uavSkinningTables()
{
}

Character::~Character(void)
{
}

bool Character::Init(const InputLayout &inputLayout,
	const shared_ptr<SDKMesh> &mesh,
	const shared_ptr<ShaderPool> &shaderPool,
	const shared_ptr<Graphics::PipelineCache> &graphicsPipelineCache,
	const shared_ptr<Compute::PipelineCache> &computePipelineCache,
	const shared_ptr<PipelineLayoutCache> &pipelineLayoutCache,
	const shared_ptr<DescriptorTableCache> &descriptorTableCache,
	const shared_ptr<vector<SDKMesh>> &linkedMeshes,
	const shared_ptr<vector<MeshLink>> &meshLinks)
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
	createPipelineLayout();
	createPipelines(inputLayout);
	createDescriptorTables();

	return true;
}

void Character::InitPosition(const XMFLOAT4 &posRot)
{
	m_vPosRot = posRot;
}

void Character::FrameMove(double time)
{
	Model::FrameMove();
	m_time = time;
}

void Character::FrameMove(double time, CXMMATRIX viewProj, FXMMATRIX *pWorld,
	FXMMATRIX *pShadow, bool isTemporal)
{
	Model::FrameMove();

	// Set the bone matrices
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_mesh->TransformMesh(XMMatrixIdentity(), time);
	setSkeletalMatrices(numMeshes);

	SetMatrices(viewProj, pWorld, pShadow, isTemporal);
	m_time = -1.0;
}

void Character::SetMatrices(CXMMATRIX viewProj, FXMMATRIX *pWorld, FXMMATRIX *pShadow, bool isTemporal)
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

	Model::SetMatrices(world, viewProj, pShadow, isTemporal);

	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		for (auto m = 0ui8; m < numLinks; ++m)
			setLinkedMatrices(m, world, viewProj, pShadow, isTemporal);
	}
}

void Character::SetSkinningPipeline()
{
	m_commandList.SetComputePipelineLayout(m_skinningPipelineLayout);
	m_commandList.SetPipelineState(m_skinningPipeline);
}

void Character::Skinning(bool reset)
{
	if (m_time >= 0.0) m_mesh->TransformMesh(XMMatrixIdentity(), m_time);
	skinning(reset);
}

void Character::RenderTransformed(SubsetFlags subsetFlags, bool isShadow, bool reset)
{
	renderTransformed(subsetFlags, isShadow, reset);
	if (m_meshLinks)
	{
		const auto numLinks = static_cast<uint8_t>(m_meshLinks->size());
		//for (auto m = 0ui8; m < numLinks; ++m) renderLinked(uVS, uGS, uPS, m, bReset);
	}
}

const XMFLOAT4 &Character::GetPosition() const
{
	return m_vPosRot;
}

FXMMATRIX Character::GetWorldMatrix() const
{
	return XMLoadFloat4x4(&m_mWorld);
}

shared_ptr<SDKMesh> Character::LoadSDKMesh(const Device &device, const wstring &meshFileName,
	const wstring &animFileName, const TextureCache &textureCache,
	const shared_ptr<vector<MeshLink>> &meshLinks,
	vector<SDKMesh> *linkedMeshes)
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
			auto &meshInfo = meshLinks->at(m);
			meshInfo.uBone = mesh->FindFrameIndex(meshInfo.szBoneName.c_str());
			N_RETURN(linkedMeshes->at(m).Create(device.get(), meshInfo.szMeshName.c_str(),
				textureCache), nullptr);
		}
	}

	return mesh;
}

bool Character::createTransformedStates()
{
	for (auto &vertexBuffers : m_transformedVBs)
		N_RETURN(createTransformedVBs(vertexBuffers), false);

	return true;
}

bool Character::createTransformedVBs(VertexBuffer &vertexBuffer)
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
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, PoolType(1), ResourceState(0),
		numMeshes, firstVertices.data(), numMeshes, firstVertices.data(),
		numMeshes, firstVertices.data()), false);

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

	for (auto &boneWorlds : m_boneWorlds)
		N_RETURN(boneWorlds.Create(m_device, numElements, sizeof(XMFLOAT4X3),
			D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_STATE_GENERIC_READ, numMeshes,
			firstElements.data()), false);

	// Linked meshes
	if (m_meshLinks) m_cbLinkedMatrices.resize(m_meshLinks->size());
	for (auto &cbLinkedMatrices : m_cbLinkedMatrices)
		N_RETURN(cbLinkedMatrices.Create(m_device, sizeof(CBMatrices) * FrameCount, FrameCount), false);

	if (m_meshLinks) m_cbLinkedShadowMatrices.resize(m_meshLinks->size());
	for (auto &cbLinkedMatrix : m_cbLinkedShadowMatrices)
		N_RETURN(cbLinkedMatrix.Create(m_device, sizeof(XMFLOAT4) * FrameCount, FrameCount), false);

	return true;
}

void Character::createPipelineLayout()
{
	// Skinning
	{
		auto roBoneWorld = 0u;
		auto rwVertices = 0u;
		auto roVertices = roBoneWorld + 1;

		// Get shader resource slots
		auto desc = D3D12_SHADER_INPUT_BIND_DESC();
		const auto reflector = m_shaderPool->GetReflector(Shader::Stage::CS, CS_SKINNING);
		if (reflector)
		{
			auto hr = reflector->GetResourceBindingDescByName("g_rwVertices", &desc);
			if (SUCCEEDED(hr)) rwVertices = desc.BindPoint;

			hr = reflector->GetResourceBindingDescByName("g_roDualQuat", &desc);
			if (SUCCEEDED(hr)) roBoneWorld = desc.BindPoint;

			hr = reflector->GetResourceBindingDescByName("g_roVertices", &desc);
			if (SUCCEEDED(hr)) roVertices = desc.BindPoint;
		}

		// Get pipeline layout
		Util::PipelineLayout utilPipelineLayout;

		// Input vertices and bone matrices
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roBoneWorld,
				0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout.SetShaderStage(INPUT, Shader::Stage::CS);

		// Output vertices
		utilPipelineLayout.SetRange(OUTPUT, DescriptorType::UAV, 1, rwVertices);
		utilPipelineLayout.SetShaderStage(OUTPUT, Shader::Stage::CS);

		m_skinningPipelineLayout = utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_NONE);
	}

	// Rendering
	{
#if	TEMPORAL
		auto roVertices = 0u;

		// Get shader resource slots
		auto desc = D3D12_SHADER_INPUT_BIND_DESC();
		const auto reflector = m_shaderPool->GetReflector(Shader::Stage::VS, VS_BASE_PASS);
		if (reflector)
		{
			auto hr = reflector->GetResourceBindingDescByName("g_roVertices", &desc);
			if (SUCCEEDED(hr)) roVertices = desc.BindPoint;
		}
#endif

		auto utilPipelineLayout = initPipelineLayout(VS_BASE_PASS, PS_BASE_PASS);

#if	TEMPORAL
		utilPipelineLayout.SetRange(HISTORY, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout.SetShaderStage(HISTORY, Shader::Stage::VS);
#endif

		m_pipelineLayout = utilPipelineLayout.GetPipelineLayout(*m_pipelineLayoutCache,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}
}

void Character::createPipelines(const InputLayout &inputLayout, const Format *rtvFormats,
	uint32_t numRTVs, Format dsvFormat)
{
	// Skinning
	{
		Compute::State state;
		state.SetPipelineLayout(m_skinningPipelineLayout);
		state.SetShader(m_shaderPool->GetShader(Shader::Stage::CS, CS_SKINNING));
		m_skinningPipeline = state.GetPipeline(*m_computePipelineCache);
	}

	// Rendering
	{
		const Format defaultRtvFormats[] =
		{
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R16G16_FLOAT
		};

		rtvFormats = rtvFormats ? rtvFormats : defaultRtvFormats;
		numRTVs = numRTVs > 0 ? numRTVs : static_cast<uint32_t>(size(defaultRtvFormats));

		Graphics::State state;

		// Get opaque pipeline
		state.IASetInputLayout(inputLayout);
		state.IASetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		state.SetPipelineLayout(m_pipelineLayout);
		state.SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, VS_BASE_PASS));
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_BASE_PASS));
		state.OMSetRTVFormats(rtvFormats, numRTVs);
		state.OMSetDSVFormat(dsvFormat ? dsvFormat : DXGI_FORMAT_D24_UNORM_S8_UINT);
		m_pipelines[OPAQUE_FRONT] = state.GetPipeline(*m_pipelineCache);

		// Get transparent pipeline
		state.RSSetState(Graphics::RasterizerPreset::CULL_NONE, *m_pipelineCache);
		state.DSSetState(Graphics::DepthStencilPreset::DEPTH_READ_LESS_EQUAL, *m_pipelineCache);
		state.OMSetBlendState(Graphics::BlendPreset::AUTO_NON_PREMUL, *m_pipelineCache);
		m_pipelines[ALPHA_TWO_SIDE] = state.GetPipeline(*m_pipelineCache);

		// Get alpha-test pipeline
		state.SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, VS_ALPHA_TEST));
		state.SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_ALPHA_TEST));
		state.DSSetState(Graphics::DepthStencilPreset::DEFAULT_LESS, *m_pipelineCache);
		state.OMSetBlendState(Graphics::DEFAULT_OPAQUE, *m_pipelineCache);
		m_pipelines[OPAQUE_TWO_SIDE] = state.GetPipeline(*m_pipelineCache);

		// Get reflected pipeline
		//state.RSSetState(Graphics::RasterizerPreset::CULL_FRONT, *m_pipelineCache);
		//state.OMSetBlendState(Graphics::BlendPreset::DEFAULT_OPAQUE, *m_pipelineCache);
		//m_pipelines[REFLECTED] = state.GetPipeline(*m_pipelineCache);
	}
}

void Character::createDescriptorTables()
{
	const auto numMeshes = m_mesh->GetNumMeshes();

	for (auto i = 0ui8; i < FrameCount; ++i)
	{
		m_srvSkinningTables[i].resize(numMeshes);
		m_uavSkinningTables[i].resize(numMeshes);
#if	TEMPORAL
		m_srvSkinnedTables[i].resize(numMeshes);
#endif

		for (auto m = 0u; m < numMeshes; ++m)
		{
			Util::DescriptorTable srvSkinningTable;
			const Descriptor srvs[] = { m_boneWorlds[i].GetSRV(m), m_mesh->GetVertexBufferSRV(m, 0) };
			srvSkinningTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
			m_srvSkinningTables[i][m] = srvSkinningTable.GetCbvSrvUavTable(*m_descriptorTableCache);

			Util::DescriptorTable uavSkinningTable;
			uavSkinningTable.SetDescriptors(0, 1, &m_transformedVBs[i].GetUAV(m));
			m_uavSkinningTables[i][m] = uavSkinningTable.GetCbvSrvUavTable(*m_descriptorTableCache);

#if	TEMPORAL
			Util::DescriptorTable srvSkinnedTable;
			srvSkinnedTable.SetDescriptors(0, 1, &m_transformedVBs[i].GetSRV(m));
			m_srvSkinnedTables[i][m] = srvSkinnedTable.GetCbvSrvUavTable(*m_descriptorTableCache);
#endif
		}
	}
}

void Character::setLinkedMatrices(uint32_t mesh, CXMMATRIX world,
	CXMMATRIX viewProj, FXMMATRIX *pShadow, bool isTemporal)
{
	// Set World-View-Proj matrix
	const auto influenceMatrix = m_mesh->GetInfluenceMatrix(m_meshLinks->at(mesh).uBone);
	const auto worldViewProj = influenceMatrix * world * viewProj;

	// Update constant buffers
	const auto pCBData = reinterpret_cast<CBMatrices*>(m_cbLinkedMatrices[mesh].Map());
	pCBData->WorldViewProj = XMMatrixTranspose(worldViewProj);
	pCBData->World = XMMatrixTranspose(world);
	pCBData->Normal = XMMatrixInverse(nullptr, world);
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose once
	//pCBData->Normal = XMMatrixTranspose(&, &pCBData->Normal);	// transpose twice

	if (pShadow)
	{
		const auto model = XMMatrixMultiply(influenceMatrix, world);
		const auto shadow = XMMatrixMultiply(model, *pShadow);
		pCBData->ShadowProj = XMMatrixTranspose(shadow);

		auto &cbData = *reinterpret_cast<XMMATRIX*>(m_cbLinkedShadowMatrices[mesh].Map());
		cbData = pCBData->ShadowProj;
	}

#if	TEMPORAL
	if (isTemporal)
	{
		XMStoreFloat4x4(&m_linkedWorldViewProjs[m_currentFrame][mesh], worldViewProj);
		const auto worldViewProjPrev = XMLoadFloat4x4(&m_linkedWorldViewProjs[m_previousFrame][mesh]);
		pCBData->WorldViewProjPrev = XMMatrixTranspose(worldViewProjPrev);
	}
#endif
}

void Character::skinning(bool reset)
{
	if (reset)
	{
		const DescriptorPool descriptorPools[] =
		{ m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL) };
		m_commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		m_commandList.SetComputePipelineLayout(m_skinningPipelineLayout);
		m_commandList.SetPipelineState(m_skinningPipeline);
	}

	const auto numMeshes = m_mesh->GetNumMeshes();

	// Set the bone matrices
	if (m_time >= 0.0) setSkeletalMatrices(numMeshes);

	// Prepare UAV state
	m_transformedVBs[m_currentFrame].Barrier(m_commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Skin the vertices and output them to buffers
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Setup descriptor tables
		m_commandList.SetComputeDescriptorTable(INPUT, m_srvSkinningTables[m_currentFrame][m]);
		m_commandList.SetComputeDescriptorTable(OUTPUT, m_uavSkinningTables[m_currentFrame][m]);
		
		// Skinning
		const auto numVertices = static_cast<uint32_t>(m_mesh->GetNumVertices(m, 0));
		const auto numGroups = ALIGN(numVertices, 64) / 64;
		m_commandList.Dispatch(static_cast<uint32_t>(numGroups), 1, 1);
	}
}

void Character::renderTransformed(SubsetFlags subsetFlags, bool isShadow, bool reset)
{
	if (reset)
	{
		const DescriptorPool descriptorPools[] =
		{
			m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
			m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
		};
		m_commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);
		m_commandList.SetGraphicsPipelineLayout(m_pipelineLayout);
		m_commandList.SetGraphicsDescriptorTable(SAMPLERS, m_samplerTable);
	}

	// Set matrices
	m_commandList.SetGraphicsDescriptorTable(MATRICES,
		m_cbvTables[m_currentFrame][isShadow ? CBV_SHADOW_MATRIX : CBV_MATRICES]);

	const SubsetFlags subsetMasks[] = { SUBSET_OPAQUE, SUBSET_ALPHA_TEST, SUBSET_ALPHA };

	// Prepare VBV state for the vertex buffer of the current frame
	auto &vertexBuffer = m_transformedVBs[m_currentFrame];
	vertexBuffer.Barrier(m_commandList, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

#if	TEMPORAL
	// Prepare SRV state for the vertex buffer of the previous frame, if neccessary
	auto &prevVertexBuffer = m_transformedVBs[m_previousFrame];
	prevVertexBuffer.Barrier(m_commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
#endif

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (const auto &subsetMask : subsetMasks)
	{
		if (subsetFlags & subsetMask)
		{
			for (auto m = 0u; m < numMeshes; ++m)
			{
				// Set IA parameters
				m_commandList.IASetVertexBuffers(0, 1, &vertexBuffer.GetVBV(m));

#if	TEMPORAL
				// Set historical motion states, if neccessary
				m_commandList.SetGraphicsDescriptorTable(HISTORY, m_srvSkinnedTables[m_previousFrame][m]);
#endif

				// Render mesh
				render(m, ~SUBSET_FULL & subsetFlags | subsetMask, reset);
			}
		}
	}

	// Clear out the vb bindings for the next pass
	//if (reset) m_commandList->IASetVertexBuffers(0, 1, nullptr);
}

#if 0
void Character::renderLinked(uint32_t mesh, bool isShadow, bool reset)
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
	m_boneWorlds[m_currentFrame].Unmap();
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
void Character::convertToDQ(XMFLOAT4 &dqTran, CXMVECTOR quat, const XMFLOAT3 &tran) const
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
