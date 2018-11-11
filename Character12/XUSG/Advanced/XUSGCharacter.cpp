//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGCharacter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Character::Character(const Device &device, const GraphicsCommandList &commandList) :
	Model(device, commandList),
	m_linkedMeshes(nullptr),
	m_meshLinks(nullptr),
	m_transformedVBs(),
	m_boneWorlds(0),
	m_cbLinkedMatrices(0),
	m_cbLinkedShadowMatrices(0),
	m_skinningPipelineLayout(nullptr),
	m_skinningPipeline(nullptr),
	m_srvSkinningTables(0),
	m_uavSkinningTables()
{
}

Character::~Character(void)
{
}

void Character::Init(const InputLayout &inputLayout,
	const shared_ptr<SDKMesh> &mesh,
	const shared_ptr<Shader::Pool> &shaderPool,
	const shared_ptr<Graphics::Pipeline::Pool> &pipelinePool,
	const shared_ptr<DescriptorTablePool> &descriptorTablePool,
	const shared_ptr<vector<SDKMesh>> &linkedMeshes,
	const shared_ptr<vector<MeshLink>> &meshLinks)
{
	// Set the Linked Meshes
	m_meshLinks = meshLinks;
	m_linkedMeshes = linkedMeshes;

	// Get SDKMesh
	Model::Init(inputLayout, mesh, shaderPool, pipelinePool, descriptorTablePool);

	// Create buffers
	createBuffers();

	// Create VBs that will hold all of the skinned vertices that need to be transformed output
	createTransformedStates();

	// Create pipeline layouts, pipelines, and descriptor tables
	createPipelineLayout();
	createPipelines();
	createDescriptorTables();
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
	for (auto m = 0u; m < numMeshes; ++m) setBoneMatrices(m);

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

void Character::Skinning(bool reset)
{
	if (m_time >= 0.0) m_mesh->TransformMesh(XMMatrixIdentity(), m_time);
	skinning(reset);
}

void Character::RenderTransformed(SubsetFlag subsetFlags, bool isShadow, bool reset)
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
	const wstring &animFileName, const shared_ptr<vector<MeshLink>> &meshLinks,
	vector<SDKMesh> *linkedMeshes)
{
	// Load the animated mesh
	const auto mesh = Model::LoadSDKMesh(device, meshFileName);
	ThrowIfFailed(mesh->LoadAnimation(animFileName.c_str()));
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
			ThrowIfFailed(linkedMeshes->at(m).Create(device.Get(), meshInfo.szMeshName.c_str()));
		}
	}

	return mesh;
}

void Character::createTransformedStates()
{
#if	TEMPORAL
	for (auto &vertexBuffers : m_transformedVBs)
		createTransformedVBs(vertexBuffers);
#else
	createTransformedVBs(m_transformedVBs[0]);
#endif
}

void Character::createTransformedVBs(vector<VertexBuffer> &vertexBuffers)
{
	// Create VBs that will hold all of the skinned vertices that need to be output
	const auto numMeshes = m_mesh->GetNumMeshes();
	vertexBuffers.resize(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		const auto vertexCount = static_cast<uint32_t>(m_mesh->GetNumVertices(m, 0));
		const auto byteWidth = vertexCount * static_cast<uint32_t>(sizeof(Vertex));
		vertexBuffers[m].Create(m_device, byteWidth, sizeof(Vertex),
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	}
}

void Character::createBuffers()
{
	// Bone world matrices
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_boneWorlds.resize(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		const auto numBones = m_mesh->GetNumInfluences(m);
		m_boneWorlds[m].Create(m_device, numBones, sizeof(XMFLOAT4X3),
			D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// Linked meshes
	if (m_meshLinks) m_cbLinkedMatrices.resize(m_meshLinks->size());
	for (auto &cbLinkedMatrices : m_cbLinkedMatrices)
		cbLinkedMatrices.Create(m_device, 512 * 128, sizeof(CBMatrices));

	if (m_meshLinks) m_cbLinkedShadowMatrices.resize(m_meshLinks->size());
	for (auto &cbLinkedMatrix : m_cbLinkedShadowMatrices)
		cbLinkedMatrix.Create(m_device, 256 * 128, sizeof(XMFLOAT4));
}

void Character::createPipelineLayout()
{
	auto rwVertices = 0u;
	auto roVertices = 0u;
	auto roBoneWorld = roVertices + 1;

	// Get shader resource slots
	auto desc = D3D12_SHADER_INPUT_BIND_DESC();
	const auto pReflector = m_shaderPool->GetReflector(Shader::Stage::CS, CS_SKINNING);
	if (pReflector)
	{
		auto hr = pReflector->GetResourceBindingDescByName("g_rwVertices", &desc);
		if (SUCCEEDED(hr)) rwVertices = desc.BindPoint;

		hr = pReflector->GetResourceBindingDescByName("g_roVertices", &desc);
		if (SUCCEEDED(hr)) roVertices = desc.BindPoint;

		hr = pReflector->GetResourceBindingDescByName("g_roDualQuat", &desc);
		if (SUCCEEDED(hr)) roBoneWorld = desc.BindPoint;
	}

	// Get pipeline layout
	Util::PipelineLayout utilPipelineLayout;

	// Input vertices and bone matrices
	if (roBoneWorld == roVertices + 1)
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 2, roVertices);
	else
	{
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roVertices);
		utilPipelineLayout.SetRange(INPUT, DescriptorType::SRV, 1, roBoneWorld);
	}
	utilPipelineLayout.SetShaderStage(INPUT, Shader::Stage::CS);

	// Output vertices
	utilPipelineLayout.SetRange(OUTPUT, DescriptorType::UAV, 1, rwVertices);
	utilPipelineLayout.SetShaderStage(OUTPUT, Shader::Stage::CS);

	m_skinningPipelineLayout = utilPipelineLayout.GetPipelineLayout(*m_pipelinePool,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);
}

void Character::createPipelines()
{
	Compute::PipelineDesc desc = {};
	desc.pRootSignature = m_skinningPipelineLayout.Get();
	desc.CS = Shader::ByteCode(m_shaderPool->GetShader(Shader::Stage::CS, CS_SKINNING).Get());
	ThrowIfFailed(m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_skinningPipeline)));
}

void Character::createDescriptorTables()
{
	const auto numMeshes = m_mesh->GetNumMeshes();
	m_srvSkinningTables.resize(numMeshes);
	for (auto &uavSkinningTables : m_uavSkinningTables)
		uavSkinningTables.resize(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		Util::DescriptorTable srvTable;
		const Descriptor srvs[] = { m_mesh->GetVertexBuffer(m, 0)->GetSRV(), m_boneWorlds[m].GetSRV() };
		srvTable.SetDescriptors(0, _countof(srvs), srvs);
		m_srvSkinningTables[m] = srvTable.GetCbvSrvUavTable(*m_descriptorTablePool);

		Util::DescriptorTable uavTables[_countof(m_transformedVBs)];
		for (auto i = 0u; i < _countof(uavTables); ++i)
		{
			if (!m_transformedVBs[i].empty())
			{
				uavTables[i].SetDescriptors(0, 1, &m_transformedVBs[i][m].GetUAV());
				m_uavSkinningTables[i][m] = uavTables[i].GetCbvSrvUavTable(*m_descriptorTablePool);
			}
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
		XMStoreFloat4x4(&m_linkedWorldViewProjs[m_temporalIndex][mesh], worldViewProj);
		const auto worldViewProjPrev = XMLoadFloat4x4(&m_linkedWorldViewProjs[!m_temporalIndex][mesh]);
		pCBDataMatrices->WorldViewProjPrev = XMMatrixTranspose(worldViewProjPrev);
	}
#endif
}

void Character::skinning(bool reset)
{
	if (reset)
	{
		m_commandList->SetDescriptorHeaps(1, m_descriptorTablePool->GetCbvSrvUavPool().GetAddressOf());
		m_commandList->SetComputeRootSignature(m_skinningPipelineLayout.Get());
		m_commandList->SetPipelineState(m_skinningPipeline.Get());
	}

	const auto numMeshes = m_mesh->GetNumMeshes();

	// Set the bone matrices
	if (m_time >= 0.0)
		for (auto m = 0u; m < numMeshes; ++m) setBoneMatrices(m);

	// Skin the vertices and output them to buffers
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Setup descriptor tables
		m_transformedVBs[m_temporalIndex][m].Barrier(m_commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_commandList->SetComputeRootDescriptorTable(INPUT, *m_srvSkinningTables[m]);
		m_commandList->SetComputeRootDescriptorTable(OUTPUT, *m_uavSkinningTables[m_temporalIndex][m]);
		
		// Skinning
		const auto numGroups = ceilf(m_mesh->GetNumVertices(m, 0) / 64.0f);
		m_commandList->Dispatch(static_cast<uint32_t>(numGroups), 1, 1);
	}
}

void Character::renderTransformed(SubsetFlag subsetFlags, bool isShadow, bool reset)
{
	if (reset)
	{
		DescriptorPool::InterfaceType* heaps[] =
		{
			m_descriptorTablePool->GetCbvSrvUavPool().Get(),
			m_descriptorTablePool->GetSamplerPool().Get()
		};
		m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
		m_commandList->SetGraphicsRootSignature(m_pipelineLayout.Get());
		m_commandList->SetGraphicsRootDescriptorTable(SAMPLERS, *m_samplerTable);
	}

	// Set matrices
	m_commandList->SetGraphicsRootDescriptorTable(MATRICES,
		*m_cbvTables[isShadow ? CBV_SHADOW_MATRIX : CBV_MATRICES]);

	const auto numMeshes = m_mesh->GetNumMeshes();
	for (auto m = 0u; m < numMeshes; ++m)
	{
		// Set IA parameters
		auto &vertexBuffer = m_transformedVBs[m_temporalIndex][m];
		vertexBuffer.Barrier(m_commandList, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		m_commandList->IASetVertexBuffers(0, 1, &vertexBuffer.GetVBV());

		// Set historical motion states, if neccessary
#if	TEMPORAL
		m_pDXContext->VSSetShaderResources(m_uSRVertices, 1,
			m_pvpTransformedVBs[!m_temporalIndex][m]->GetSRV().GetAddressOf());
#endif

		// Render mesh
		render(m, subsetFlags, reset);
	}

	// Clear out the vb bindings for the next pass
#if	TEMPORAL
	const auto pNullSRV = LPDXShaderResourceView(nullptr);	// Helper to Clear SRVs
	m_pDXContext->VSSetShaderResources(m_uSRVertices, 1, &pNullSRV);
#endif

	// Clear out the vb bindings for the next pass
	if (reset) m_commandList->IASetVertexBuffers(0, 1, nullptr);
}

#if 0
void Character::renderLinked(uint32_t mesh, bool isShadow, bool reset)
{
	// Set constant buffers
	m_pDXContext->VSSetConstantBuffers(m_uCBMatrices, 1, m_vpCBLinkedMats[uMesh].GetAddressOf());

	// Set Shaders
	setShaders(uVS, uGS, uPS, bReset);
	m_pDXContext->RSSetState(m_pState->CullCounterClockwise().Get());
	m_pDXContext->OMSetBlendState(m_pState->AutoAlphaBlend().Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);

	m_pDXContext->IASetInputLayout(m_pVertexLayout.Get());
	m_pvLinkedMeshes->at(uMesh).Render(m_pDXContext.Get(), m_uSRDiffuse, m_uSRNormal);

	resetShaders(uVS, uGS, uPS, SUBSET_FULL, bReset);
}
#endif

//--------------------------------------------------------------------------------------
// SetBoneMatrices
//
// This function handles the various ways of sending bone matrices to the shader.
//		FT_CONSTANTBUFFER:
//			With this approach, the bone matrices are stored in a constant buffer.
//			The shader will index into the constant buffer to grab the correct
//			transformation matrices for each vertex.
//--------------------------------------------------------------------------------------
void Character::setBoneMatrices(uint32_t mesh)
{
	XMFLOAT4X3 *pDataBoneWorld;
	CD3DX12_RANGE readRange(0, 0);	// We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_boneWorlds[mesh].GetResource()->Map(0, &readRange,
		reinterpret_cast<void**>(&pDataBoneWorld)));

	const auto numBones = m_mesh->GetNumInfluences(mesh);
	for (auto i = 0u; i < numBones; ++i)
	{
		const auto qMat = getDualQuat(mesh, i);
		XMStoreFloat4x3(&pDataBoneWorld[i], XMMatrixTranspose(qMat));
	}

	m_boneWorlds[mesh].GetResource()->Unmap(0, nullptr);
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
