//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGCharacter.h"

using namespace std;
using namespace DirectX;
//using namespace DX;
using namespace XUSG;

const D3D11_SO_DECLARATION_ENTRY Character::m_pSODecl[] =
{
	{ 0,	"POSITION",	0,	0,	3,	0 },	// output all components of position
	{ 0,	"NORMAL",	0,	0,	2,	0 },	// output the first 2 of the encoded normal
	{ 0,	"TEXCOORD",	0,	0,	1,	0 },	// output the first 1 encoded texture coordinates
	{ 0,	"TANGENT",	0,	0,	2,	0 },	// output the first 2 of the encoded tangent
	{ 0,	"BINORMAL",	0,	0,	2,	0 }		// output the first 2 of the encoded binormal
};
const uint8_t Character::m_uNumSODecls = ARRAYSIZE(Character::m_pSODecl);

bool Character::m_bDualQuat = true;

Character::Character(const CPDXDevice &pDXDevice) :
	Model(pDXDevice),
	m_uUAVertices(0),
	m_uSRVertices(0),
	m_uSRBoneWorld(1),
	m_pSkinnedVertexLayout(nullptr)
{
}

Character::~Character(void)
{
}

void Character::Init(const CPDXInputLayout &pVertexLayout, const spMesh &pSkinnedMesh,
	const spShader &pShader, const spState &pState, const bool bReadVB,
	const CPDXInputLayout pSkinnedVertexLayout)
{
	// Get SDKMesh
	m_pSkinnedVertexLayout = pSkinnedVertexLayout;
	Model::Init(pVertexLayout, pSkinnedMesh, pShader, pState);

	// Create variable slots
	createConstBuffers();
	setResourceSlots(bReadVB);

	// Create VBs that will hold all of the skinned vertices that need to be streamed out
	const auto &eBindFlag = pSkinnedVertexLayout ? D3D11_BIND_STREAM_OUTPUT : D3D11_BIND_UNORDERED_ACCESS;
	createTransformedStates(bReadVB ? eBindFlag | D3D11_BIND_SHADER_RESOURCE : eBindFlag);
}

void Character::Init(const CPDXInputLayout &pVertexLayout, const spMesh &pSkinnedMesh,
	const svMesh &pvLinkedMeshes, const svMeshLink &pvMeshLinks, const spShader &pShader,
	const spState &pState, const bool bReadVB, const CPDXInputLayout pSkinnedVertexLayout)
{
	// Set the Linked Meshes
	m_pvMeshLinks = pvMeshLinks;
	m_pvLinkedMeshes = pvLinkedMeshes;

	Init(pVertexLayout, pSkinnedMesh, pShader, pState, bReadVB, pSkinnedVertexLayout);
}

void Character::InitPosition(const XMFLOAT4 &vPosRot)
{
	m_vPosRot = vPosRot;
}

void Character::FrameMove(const double fTime)
{
	Model::FrameMove();
	m_fTime = fTime;
}

void Character::FrameMove(const double fTime, CXMMATRIX mWorld, CXMMATRIX mViewProj)
{
	m_pMesh->TransformMesh(XMMatrixIdentity(), fTime);
	SetMatrices(mWorld, mViewProj);
}

void Character::SetMatrices(CXMMATRIX mViewProj, const LPCMATRIX pMatShadow, bool bTemporal)
{
	const auto mTrans = XMMatrixTranslation(m_vPosRot.x, m_vPosRot.y, m_vPosRot.z);
	const auto mRot = XMMatrixRotationY(m_vPosRot.w);
	const auto mWorld = XMMatrixMultiply(mRot, mTrans);
	XMStoreFloat4x4(&m_mWorld, mWorld);

	// Update World-View-Proj matrix
	SetMatrices(mWorld, mViewProj, pMatShadow, bTemporal);
}

void Character::SetMatrices(CXMMATRIX mWorld, CXMMATRIX mViewProj,
	const LPCMATRIX pMatShadow, bool bTemporal)
{
	Model::SetMatrices(mWorld, mViewProj, pMatShadow, bTemporal);

	if (m_pvMeshLinks)
	{
		const auto uNumLinks = static_cast<uint8_t>(m_pvMeshLinks->size());
		for (auto m = 0ui8; m < uNumLinks; ++m)
			setLinkedMatrices(m, mWorld, mViewProj, pMatShadow, bTemporal);
	}
}

void Character::Skinning(const bool bReset)
{
	m_pMesh->TransformMesh(XMMatrixIdentity(), m_fTime);
	if (m_pSkinnedVertexLayout) skinningSO(bReset);
	else skinning(bReset);
}

void Character::RenderTransformed(const SubsetType uMaskType, const uint8_t uVS,
	const uint8_t uGS, const uint8_t uPS, const bool bReset)
{
	renderTransformed(uVS, uGS, uPS, uMaskType, bReset);
	if (m_pvMeshLinks)
	{
		const auto uNumLinks = static_cast<uint8_t>(m_pvMeshLinks->size());
		for (auto m = 0ui8; m < uNumLinks; ++m) renderLinked(uVS, uGS, uPS, m, bReset);
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

void Character::LoadSDKMesh(const CPDXDevice &pDXDevice, const wstring &szMeshFileName,
	const wstring &szAnimFileName, spMesh &pSkinnedMesh)
{
	// Load the animated mesh
	Model::LoadSDKMesh(pDXDevice, szMeshFileName, pSkinnedMesh);
	ThrowIfFailed(pSkinnedMesh->LoadAnimation(szAnimFileName.c_str()));
	pSkinnedMesh->TransformBindPose(XMMatrixIdentity());

	// Fix the frame name to avoid space
	for (auto i = 0u; i < pSkinnedMesh->GetNumFrames(); ++i)
	{
		auto szName = pSkinnedMesh->GetFrame(i)->Name;
		for (auto j = 0ui8; szName[j] != '\0'; ++j)
			if (szName[j] == ' ') szName[j] = '_';
	}
}

void Character::LoadSDKMesh(const CPDXDevice &pDXDevice, const wstring &szMeshFileName,
	const wstring &szAnimFileName, spMesh &pSkinnedMesh,
	svMesh &pLinkedMeshes, const svMeshLink &pMeshLinkage)
{
	// Load the animated mesh
	LoadSDKMesh(pDXDevice, szMeshFileName, szAnimFileName, pSkinnedMesh);

	// Load the linked meshes
	const auto uNumLinks = static_cast<uint8_t>(pMeshLinkage->size());
	VEC_ALLOC_PTR(pLinkedMeshes, vMesh, uNumLinks);
	for (auto m = 0ui8; m < uNumLinks; ++m)
	{
		auto &meshInfo = pMeshLinkage->at(m);
		meshInfo.uBone = pSkinnedMesh->FindFrameIndex(meshInfo.szBoneName.c_str());
		ThrowIfFailed(pLinkedMeshes->at(m).Create(pDXDevice.Get(), meshInfo.szMeshName.c_str()));
	}
}

void Character::InitLayout(const CPDXDevice &pDXDevice, CPDXInputLayout &pVertexLayout,
	const spShader &pShader, const bool bDualQuat, const uint8_t uVSShading,
	LPCPDXInputLayout ppSkinnedVertexLayout, const uint8_t uVSSkinning)
{
	m_bDualQuat = bDualQuat;

	// Define our vertex data layout for post-transformed objects
	Model::InitLayout(pDXDevice, pVertexLayout, pShader, uVSShading);

	// Define our vertex data layout for skinned objects
	if (ppSkinnedVertexLayout)
		createSkinnedLayout(pDXDevice, dref(ppSkinnedVertexLayout), pShader, uVSSkinning);
}

void Character::SetSkinningShader(const CPDXContext &pDXContext, const spShader &pShader,
	const uint8_t uCS, const uint8_t uVS)
{
	if (uVS == NULL_SHADER) pDXContext->CSSetShader(pShader->GetComputeShader(uCS).Get(), nullptr, 0);
	else
	{
		pDXContext->VSSetShader(pShader->GetVertexShader(uVS).Get(), nullptr, 0);
		pDXContext->GSSetShader(pShader->GetGeometryShader(uVS).Get(), nullptr, 0);
	}
}

void Character::createTransformedStates(const uint8_t uBindFlags)
{
#if	TEMPORAL
	for (auto &vpVBs : m_pvpTransformedVBs)
		createTransformedVBs(vpVBs, uBindFlags);
#else
	createTransformedVBs(m_pvpTransformedVBs[0], uBindFlags);
#endif
}

void Character::createTransformedVBs(vuRawBuffer &vpVBs, const uint8_t uBindFlags)
{
	// Create VBs that will hold all of the skinned vertices that need to be output
	const auto uNumMeshes = m_pMesh->GetNumMeshes();
	VEC_ALLOC(vpVBs, uNumMeshes);

	for (auto m = 0u; m < uNumMeshes; ++m)
	{
		const auto uVertexCount = static_cast<uint32_t>(m_pMesh->GetNumVertices(m, 0));
		const auto uByteWidth = uVertexCount * static_cast<uint32_t>(sizeof(Vertex));
		vpVBs[m] = make_unique<RawBuffer>(m_pDXDevice);
		vpVBs[m]->Create(uByteWidth, D3D11_BIND_VERTEX_BUFFER | uBindFlags);
	}
}

void Character::createConstBuffers()
{
	const auto desc = CD3D11_BUFFER_DESC(sizeof(CBMatrices), D3D11_BIND_CONSTANT_BUFFER,
		D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);

	VEC_ALLOC(m_vpCBLinkedMats, m_pvMeshLinks->size());
	for (auto &pCBLinkedMat : m_vpCBLinkedMats)
		ThrowIfFailed(m_pDXDevice->CreateBuffer(&desc, nullptr, &pCBLinkedMat));

	m_pSBBoneWorld = make_unique<StructuredBuffer>(m_pDXDevice);
	m_pSBBoneWorld->Create(UINT8_MAX, sizeof(XMFLOAT4X3), D3D11_BIND_SHADER_RESOURCE,
		nullptr, 0, D3D11_USAGE_DYNAMIC);
}

void Character::setResourceSlots(const bool bReadVB)
{
	// Get shader resource slots
	auto desc = D3D11_SHADER_INPUT_BIND_DESC();
	const auto pReflector = m_pShader->GetCSReflector(g_uCSSkinning);
	if (pReflector)
	{
		auto hr = pReflector->GetResourceBindingDescByName("g_rwVertices", &desc);
		if (SUCCEEDED(hr)) m_uUAVertices = desc.BindPoint;

		hr = pReflector->GetResourceBindingDescByName("g_roVertices", &desc);
		if (SUCCEEDED(hr)) m_uSRVertices = desc.BindPoint;

		hr = pReflector->GetResourceBindingDescByName("g_roDualQuat", &desc);
		if (SUCCEEDED(hr)) m_uSRBoneWorld = desc.BindPoint;
	}
}

void Character::setLinkedMatrices(const uint32_t uMesh, CXMMATRIX mWorld,
	CXMMATRIX mViewProj, const LPCMATRIX pMatShadow, bool bTemporal)
{
	// Set World-View-Proj matrix
	const auto rMat = m_pMesh->GetInfluenceMatrix(m_pvMeshLinks->at(uMesh).uBone);
	const auto mWorldViewProj = rMat * mWorld * mViewProj;

	// Update constant buffers
	auto mappedSubres = D3D11_MAPPED_SUBRESOURCE();
	const auto pCBMatrices = m_vpCBLinkedMats[uMesh].Get();
	ThrowIfFailed(m_pDXContext->Map(pCBMatrices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubres));
	const auto pCBDataMatrices = reinterpret_cast<LPCBMatrices>(mappedSubres.pData);
	pCBDataMatrices->m_mWorldViewProj = XMMatrixTranspose(mWorldViewProj);
	pCBDataMatrices->m_mWorld = XMMatrixTranspose(mWorld);
	pCBDataMatrices->m_mNormal = XMMatrixInverse(nullptr, mWorld);
	//D3DXMatrixTranspose(&mWorldIT, &mWorldIT);	// transpose once
	//D3DXMatrixTranspose(&mWorldIT, &mWorldIT);	// transpose twice
	
	if (pMatShadow)
	{
		const auto mModel = XMMatrixMultiply(rMat, mWorld);
		const auto mShadow = XMMatrixMultiply(mModel, dref(pMatShadow));
		pCBDataMatrices->m_mShadow = XMMatrixTranspose(mShadow);
	}

#if	TEMPORAL
	if (bTemporal)
	{
		XMStoreFloat4x4(&m_vmLinkedWVPs[m_uTSIdx][uMesh], mWorldViewProj);
		const auto mWVPPrev = XMLoadFloat4x4(&m_vmLinkedWVPs[!m_uTSIdx][uMesh]);
		pCBDataMatrices->m_mWVPPrev = XMMatrixTranspose(mWVPPrev);
	}
#endif
	
	m_pDXContext->Unmap(pCBMatrices, 0);
}

void Character::skinning(const bool bReset)
{
	if (bReset) m_pDXContext->CSSetShader(m_pShader->GetComputeShader(g_uVSSkinning).Get(), nullptr, 0);

	// Skin the vertices and output them to buffers
	const auto uNumMeshes = m_pMesh->GetNumMeshes();
	for (auto m = 0u; m < uNumMeshes; ++m)
	{
		// Set the bone matrices
		setBoneMatrices(m);
		
		// Setup
		const auto &pUAV = m_pvpTransformedVBs[m_uTSIdx][m]->GetUAV().Get();
		m_pDXContext->CSSetUnorderedAccessViews(m_uUAVertices, 1, &pUAV, &m_uNullUint);
		m_pDXContext->CSSetShaderResources(m_uSRVertices, 1, m_pMesh->GetVBSRV11(m, 0).GetAddressOf());
		m_pDXContext->CSSetShaderResources(m_uSRBoneWorld, 1, m_pSBBoneWorld->GetSRV().GetAddressOf());

		// Skinning
		const auto fNumGroups = ceilf(m_pMesh->GetNumVertices(m, 0) / 64.0f);
		m_pDXContext->Dispatch(static_cast<uint32_t>(fNumGroups), 1, 1);
	}

	if (bReset)
	{
		// Unset
		const auto pNullSRV = LPDXShaderResourceView(nullptr);	// Helper to Clear SRVs
		m_pDXContext->CSSetShaderResources(m_uSRBoneWorld, 1, &pNullSRV);
		m_pDXContext->CSSetShaderResources(m_uSRVertices, 1, &pNullSRV);
		m_pDXContext->CSSetShader(nullptr, nullptr, 0);
	}

	const auto pNullUAV = LPDXUnorderedAccessView(nullptr);	// Helper to Clear UAVs
	m_pDXContext->CSSetUnorderedAccessViews(m_uUAVertices, 1, &pNullUAV, &m_uNullUint);
}

void Character::skinningSO(const bool bReset)
{
	// Set vertex Layout
	m_pDXContext->IASetInputLayout(m_pSkinnedVertexLayout.Get());
	m_pDXContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);	// When skinning vertices, we don't care about topology.
																				// Treat the entire vertex buffer as list of points.

	// Set Shaders
	if (bReset)
	{
		m_pDXContext->VSSetShader(m_pShader->GetVertexShader(g_uVSSkinning).Get(), nullptr, 0);
		m_pDXContext->GSSetShader(m_pShader->GetGeometryShader(g_uVSSkinning).Get(), nullptr, 0);
		m_pDXContext->PSSetShader(nullptr, nullptr, 0);
		m_pDXContext->RSSetState(nullptr);
		m_pDXContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
		m_pDXContext->OMSetDepthStencilState(m_pState->DepthNone().Get(), 0);
	}

	// Skin the vertices and stream them out
	const auto uNumMeshes = m_pMesh->GetNumMeshes();
	for (auto m = 0u; m < uNumMeshes; ++m)
	{
		// Turn on stream out
		auto pBuffer = m_pvpTransformedVBs[m_uTSIdx][m]->GetBuffer().Get();
		m_pDXContext->SOSetTargets(1, &pBuffer, &m_uOffset);

		// Set source vertex buffer
		pBuffer = m_pMesh->GetVB11(m, 0);
		const auto uStride = m_pMesh->GetVertexStride(m, 0);
		m_pDXContext->IASetVertexBuffers(0, 1, &pBuffer, &uStride, &m_uOffset);

		// Set the bone matrices
		setBoneMatrices(m);
		m_pDXContext->VSSetShaderResources(m_uSRBoneWorld, 1, m_pSBBoneWorld->GetSRV().GetAddressOf());

		// Render the vertices as an array of points
		m_pDXContext->Draw(static_cast<uint32_t>(m_pMesh->GetNumVertices(m, 0)), 0);
	}

	if (bReset)
	{
		m_pDXContext->OMSetDepthStencilState(nullptr, 0);
		m_pDXContext->VSSetShader(nullptr, nullptr, 0);
		m_pDXContext->GSSetShader(nullptr, nullptr, 0);
	}

	// Turn off stream out
	m_pDXContext->SOSetTargets(1, &m_pNullBuffer, &m_uOffset);
}

void Character::renderTransformed(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
	const SubsetType uMaskType, const bool bReset)
{
	// Set vertex Layout
	m_pDXContext->IASetInputLayout(m_pVertexLayout.Get());
	m_pDXContext->VSSetConstantBuffers(m_uCBMatrices, 1, m_pCBMatrices.GetAddressOf());

	// Set Shaders
	setShaders(uVS, uGS, uPS, bReset);

	const auto uNumMeshes = m_pMesh->GetNumMeshes();
	for (auto m = 0u; m < uNumMeshes; ++m)
	{
		// Set IA parameters
		const auto pBuffer = m_pvpTransformedVBs[m_uTSIdx][m]->GetBuffer();
		const auto uStride = static_cast<uint32_t>(sizeof(Vertex));
		m_pDXContext->IASetVertexBuffers(0, 1, pBuffer.GetAddressOf(), &uStride, &m_uOffset);

		// Set historical motion states, if neccessary
#if	TEMPORAL
		m_pDXContext->VSSetShaderResources(m_uSRVertices, 1,
			m_pvpTransformedVBs[!m_uTSIdx][m]->GetSRV().GetAddressOf());
#endif

		// Render mesh
		render(m, uMaskType, bReset);
	}

	// Clear out the vb bindings for the next pass
#if	TEMPORAL
	const auto pNullSRV = LPDXShaderResourceView(nullptr);	// Helper to Clear SRVs
	m_pDXContext->VSSetShaderResources(m_uSRVertices, 1, &pNullSRV);
#endif
	m_pDXContext->IASetVertexBuffers(0, 1, &m_pNullBuffer, &m_uNullStride, &m_uOffset);
	resetShaders(uVS, uGS, uPS, uMaskType, bReset);
}

void Character::renderLinked(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
	const uint32_t uMesh, const bool bReset)
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

//--------------------------------------------------------------------------------------
// SetBoneMatrices
//
// This function handles the various ways of sending bone matrices to the shader.
//		FT_CONSTANTBUFFER:
//			With this approach, the bone matrices are stored in a constant buffer.
//			The shader will index into the constant buffer to grab the correct
//			transformation matrices for each vertex.
//--------------------------------------------------------------------------------------
void Character::setBoneMatrices(const uint32_t uMesh)
{
	auto mappedSubres = D3D11_MAPPED_SUBRESOURCE();
	const auto &pCBBoneWorld = m_pSBBoneWorld->GetBuffer().Get();
	ThrowIfFailed(m_pDXContext->Map(pCBBoneWorld, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubres));
	auto pCBDataBoneWorld = reinterpret_cast<lpfloat4x3>(mappedSubres.pData);

	const auto uNumBones = m_pMesh->GetNumInfluences(uMesh);
	if (m_bDualQuat)
	{
		for (auto i = 0u; i < uNumBones; ++i)
		{
			const auto qMat = getDualQuat(uMesh, i);
			XMStoreFloat4x3(&pCBDataBoneWorld[i], XMMatrixTranspose(qMat));
		}
	}
	else
	{
		for (auto i = 0u; i < uNumBones; ++i)
		{
			const auto rMat = m_pMesh->GetMeshInfluenceMatrix(uMesh, i);
			XMStoreFloat4x3(&pCBDataBoneWorld[i], XMMatrixTranspose(rMat));
		}
	}

	m_pDXContext->Unmap(pCBBoneWorld, 0);
}

// Convert unit quaternion and translation to unit dual quaternion
void Character::convertToDQ(XMFLOAT4 &vDQTran, const CXMVECTOR &vQuat, const XMFLOAT3 &vTran) const
{
	auto vDQRot = XMFLOAT4();
	XMStoreFloat4(&vDQRot, vQuat);
	vDQTran.x = 0.5f * (vTran.x * vDQRot.w + vTran.y * vDQRot.z - vTran.z * vDQRot.y);
	vDQTran.y = 0.5f * (-vTran.x * vDQRot.z + vTran.y * vDQRot.w + vTran.z * vDQRot.x);
	vDQTran.z = 0.5f * (vTran.x * vDQRot.y - vTran.y * vDQRot.x + vTran.z * vDQRot.w);
	vDQTran.w = -0.5f * (vTran.x * vDQRot.x + vTran.y * vDQRot.y + vTran.z * vDQRot.z);
}

FXMMATRIX Character::getDualQuat(const uint32_t uMesh, const uint32_t uInfluence) const
{
	const auto mInfluence = m_pMesh->GetMeshInfluenceMatrix(uMesh, uInfluence);

	auto mQuat = XMMATRIX();
	XMMatrixDecompose(&mQuat.r[2], &mQuat.r[0], &mQuat.r[1], mInfluence);

	auto vTranslation = XMFLOAT3();
	XMStoreFloat3(&vTranslation, mQuat.r[1]);

	auto vDQTran = XMFLOAT4();
	convertToDQ(vDQTran, mQuat.r[0], vTranslation);
	mQuat.r[1] = XMLoadFloat4(&vDQTran);

	return mQuat;
}

void Character::createSkinnedLayout(const CPDXDevice &pDXDevice, CPDXInputLayout &pSkinnedVertexLayout,
	const spShader &pShader, const uint8_t uVSSkinning)
{
	// Define our vertex data layout for skinned objects
	const auto offset = D3D11_APPEND_ALIGNED_ELEMENT;
	const auto layout = initializer_list<D3D11_INPUT_ELEMENT_DESC>
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 0,		D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "WEIGHTS",	0, DXGI_FORMAT_R8G8B8A8_UNORM,		0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BONES",		0, DXGI_FORMAT_R8G8B8A8_UINT,		0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32_UINT,			0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	ThrowIfFailed(pDXDevice->CreateInputLayout(layout.begin(), static_cast<uint32_t>(layout.size()),
		pShader->GetVertexShaderBuffer(uVSSkinning)->GetBufferPointer(),
		pShader->GetVertexShaderBuffer(uVSSkinning)->GetBufferSize(),
		&pSkinnedVertexLayout));
}
