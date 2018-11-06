//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGModel.h"

using namespace std;
using namespace DirectX;
//using namespace DX;
using namespace XUSG;

uint8_t	Model::m_uSRShadow = 2;

const LPDXBuffer	Model::m_pNullBuffer	= nullptr;	// Helper to Clear Buffers
const uint32_t		Model::m_uNullUint		= 0;		// Helper to Clear Buffers
const uint32_t		Model::m_uNullStride	= m_uNullUint;
const uint32_t		Model::m_uOffset		= m_uNullUint;

Model::Model(const CPDXDevice &pDXDevice) :
	m_pDXDevice(pDXDevice),
	m_uCBMatrices(0),
	m_uSRDiffuse(0),
	m_uSRNormal(1),
	m_uSmpAnisoWrap(0),
	m_uSmpLinearCmp(2),
	m_uTSIdx(0)
{
	m_pDXDevice->GetImmediateContext(&m_pDXContext);
}

Model::~Model(void)
{
}

void Model::Init(const CPDXInputLayout &pVertexLayout, const spMesh &pMesh,
	const spShader &pShader, const spState &pState)
{
	// Set shader group and states
	m_pShader = pShader;
	m_pState = pState;

	// Create variable slots
	createConstBuffers();
	setResourceSlots();

	// Get SDKMesh
	m_pVertexLayout = pVertexLayout;
	m_pMesh = pMesh;
}

void Model::FrameMove()
{
#if	TEMPORAL
	m_uTSIdx = !m_uTSIdx;
#endif
}

void Model::SetMatrices(CXMMATRIX mWorld, CXMMATRIX mViewProj, const LPCMATRIX pMatShadow, bool bTemporal)
{
	// Set World-View-Proj matrix
	const auto mWorldViewProj = XMMatrixMultiply(mWorld, mViewProj);

	// Update constant buffers
	auto mappedSubres = D3D11_MAPPED_SUBRESOURCE();
	const auto pCBMatrices = m_pCBMatrices.Get();
	ThrowIfFailed(m_pDXContext->Map(pCBMatrices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubres));
	const auto pCBDataMatrices = reinterpret_cast<LPCBMatrices>(mappedSubres.pData);
	pCBDataMatrices->m_mWorldViewProj = XMMatrixTranspose(mWorldViewProj);
	pCBDataMatrices->m_mWorld = XMMatrixTranspose(mWorld);
	pCBDataMatrices->m_mNormal = XMMatrixInverse(nullptr, mWorld);
	//D3DXMatrixTranspose(&mWorldIT, &mWorldIT);	// transpose once
	//D3DXMatrixTranspose(&mWorldIT, &mWorldIT);	// transpose twice
	
	if (pMatShadow)
	{
		const auto mShadow = XMMatrixMultiply(mWorld, dref(pMatShadow));
		pCBDataMatrices->m_mShadow = XMMatrixTranspose(mShadow);
	}

#if	TEMPORAL
	if (bTemporal)
	{
		XMStoreFloat4x4(&m_pmWorldViewProjs[m_uTSIdx], mWorldViewProj);
		const auto mWVPPrev = XMLoadFloat4x4(&m_pmWorldViewProjs[!m_uTSIdx]);
		pCBDataMatrices->m_mWVPPrev = XMMatrixTranspose(mWVPPrev);
	}
#endif

	m_pDXContext->Unmap(pCBMatrices, 0);
}

void Model::Render(const SubsetType uMaskType, const uint8_t uVS,
	const uint8_t uGS, const uint8_t uPS, const bool bReset)
{
	// Set vertex Layout
	m_pDXContext->IASetInputLayout(m_pVertexLayout.Get());

	// Set Shaders
	setShaders(uVS, uGS, uPS, bReset);

	const auto uNumMeshes = m_pMesh->GetNumMeshes();
	for (auto m = 0u; m < uNumMeshes; ++m)
	{
		// Set IA parameters
		const auto pBuffer = m_pMesh->GetVB11(m, 0);
		const auto uStride = m_pMesh->GetVertexStride(m, 0);
		m_pDXContext->IASetVertexBuffers(0, 1, &pBuffer, &uStride, &m_uOffset);
		m_pDXContext->VSSetConstantBuffers(m_uCBMatrices, 1, m_pCBMatrices.GetAddressOf());

		// Render mesh
		render(m, uMaskType, bReset);
	}

	// Clear out the vb bindings for the next pass
	m_pDXContext->IASetVertexBuffers(0, 1, &m_pNullBuffer, &m_uNullStride, &m_uOffset);
	resetShaders(uVS, uGS, uPS, uMaskType, bReset);
}

SubsetType Model::MaskSubsetType(const SubsetType uMaskType, const bool bReset)
{
	if (bReset)
	{
		if (uMaskType & SUBSET_ALPHA)
		{
			if (uMaskType == SUBSET_ALPHA)
				m_pDXContext->OMSetBlendState(m_pState->AutoAlphaBlend().Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDXContext->RSSetState(m_pState->CullNone().Get());
		}
		// Reflected
		else if (uMaskType == SUBSET_ROPAQUE)
			m_pDXContext->RSSetState(m_pState->CullClockwise().Get());
	}

	return Mesh::MaskSubsetType(uMaskType);
}

void Model::LoadSDKMesh(const CPDXDevice &pDXDevice, const wstring &szMeshFileName, spMesh &pMesh)
{
	// Load the mesh
	pMesh = make_shared<Mesh>();

	ThrowIfFailed(pMesh->Create(pDXDevice.Get(), szMeshFileName.c_str()));

	pMesh->ClassifyMatType();
}

void Model::InitLayout(const CPDXDevice &pDXDevice, CPDXInputLayout &pVertexLayout,
	const spShader &pShader, const uint8_t uVSShading)
{
	// Define vertex data layout for post-transformed objects
	const auto offset = D3D11_APPEND_ALIGNED_ELEMENT;
	const auto layout = initializer_list<D3D11_INPUT_ELEMENT_DESC>
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 0,		D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R16G16_FLOAT,		0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, DXGI_FORMAT_R16G16B16A16_FLOAT,	0, offset,	D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	ThrowIfFailed(pDXDevice->CreateInputLayout(layout.begin(), static_cast<uint32_t>(layout.size()),
		pShader->GetVertexShaderBuffer(uVSShading)->GetBufferPointer(),
		pShader->GetVertexShaderBuffer(uVSShading)->GetBufferSize(),
		&pVertexLayout));
}

void Model::SetShaders(const CPDXContext &pDXContext, const spShader &pShader,
	const uint8_t uVS, const uint8_t uGS, const uint8_t uPS)
{
	if (uVS != NULL_SHADER) pDXContext->VSSetShader(pShader->GetVertexShader(uVS).Get(), nullptr, 0);
	else pDXContext->VSSetShader(nullptr, nullptr, 0);
	
	if (uGS != NULL_SHADER) pDXContext->GSSetShader(pShader->GetGeometryShader(uGS).Get(), nullptr, 0);
	else pDXContext->GSSetShader(nullptr, nullptr, 0);
	
	if (uPS != NULL_SHADER) pDXContext->PSSetShader(pShader->GetPixelShader(uPS).Get(), nullptr, 0);
	else  pDXContext->PSSetShader(nullptr, nullptr, 0);
}

void Model::SetBlendStates(const CPDXContext &pDXContext, const CPDXBlendState &pBlendState,
	const CPDXRasterizerState &pCullState)
{
	pDXContext->OMSetBlendState(pBlendState.Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	pDXContext->RSSetState(pCullState.Get());
}

void Model::SetShadowMap(const CPDXContext &pDXContext, const CPDXShaderResourceView &pSRVShadow)
{
	pDXContext->PSSetShaderResources(m_uSRShadow, 1, pSRVShadow.GetAddressOf());
}

void Model::createConstBuffers()
{
	const auto desc = CD3D11_BUFFER_DESC(sizeof(CBMatrices), D3D11_BIND_CONSTANT_BUFFER,
		D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);

	ThrowIfFailed(m_pDXDevice->CreateBuffer(&desc, nullptr, &m_pCBMatrices));
}

void Model::setResourceSlots()
{
	// Get constant buffer slots
	auto desc = D3D11_SHADER_INPUT_BIND_DESC();
	auto pReflector = m_pShader->GetVSReflector(g_uVSBasePass);
	if (pReflector)
	{
		const auto hr = pReflector->GetResourceBindingDescByName("cbMatrices", &desc);
		if (SUCCEEDED(hr)) m_uCBMatrices = desc.BindPoint;
	}

	pReflector = m_pShader->GetPSReflector(g_uPSBasePass);
	if (pReflector)
	{
		// Get shader resource slots
		auto hr = pReflector->GetResourceBindingDescByName("g_txAlbedo", &desc);
		if (SUCCEEDED(hr)) m_uSRDiffuse = desc.BindPoint;
		hr = pReflector->GetResourceBindingDescByName("g_txNormal", &desc);
		if (SUCCEEDED(hr)) m_uSRNormal = desc.BindPoint;
		hr = pReflector->GetResourceBindingDescByName("g_txShadow", &desc);
		if (SUCCEEDED(hr)) m_uSRShadow = desc.BindPoint;

		// Get sampler slots
		hr = pReflector->GetResourceBindingDescByName("g_smpLinear", &desc);
		if (SUCCEEDED(hr)) m_uSmpAnisoWrap = desc.BindPoint;
		hr = pReflector->GetResourceBindingDescByName("g_smpCmpLinear", &desc);
		if (SUCCEEDED(hr)) m_uSmpLinearCmp = desc.BindPoint;
	}
}

void Model::render(const uint32_t uMesh, const SubsetType uMaskType, const bool bReset)
{
	// Set IA parameters
	const auto &m = uMesh;
	m_pDXContext->IASetIndexBuffer(m_pMesh->GetIB11(m), m_pMesh->GetIBFormat11(m), 0);

	// Set materials
	const auto uSubsetMask = MaskSubsetType(uMaskType, bReset);
	const auto uNumSubsets = m_pMesh->GetNumSubsets(m, uSubsetMask);
	for (auto uSubset = 0u; uSubset < uNumSubsets; ++uSubset)
	{
		const auto pSubset = m_pMesh->GetSubset(m, uSubset, uSubsetMask);
		const auto primType = m_pMesh->GetPrimitiveType11(SDKMESH_PRIMITIVE_TYPE(pSubset->PrimitiveType));
		m_pDXContext->IASetPrimitiveTopology(primType);

		const auto pMat = m_pMesh->GetMaterial(pSubset->MaterialID);
		if (pMat)
		{
			m_pDXContext->PSSetShaderResources(m_uSRDiffuse, 1, &pMat->pDiffuseRV11);
			m_pDXContext->PSSetShaderResources(m_uSRNormal, 1, &pMat->pNormalRV11);
		}

		setSubsetStates(m, uSubset, uMaskType);

		m_pDXContext->DrawIndexed(static_cast<uint32_t>(pSubset->IndexCount),
			static_cast<uint32_t>(pSubset->IndexStart), static_cast<uint32_t>(pSubset->VertexStart));
	}
}

void Model::setSubsetStates(const uint32_t uMesh, const uint32_t uSubset, const SubsetType uMaskType)
{
	if (!(uMaskType & SUBSET_ATEST) || uMaskType == SUBSET_RFULL)	// Full subsets
	{
		if (m_pMesh->IsTranslucent(uMesh, uSubset))
		{
			if (uMaskType == SUBSET_FULL)
				m_pDXContext->OMSetBlendState(m_pState->AutoAlphaBlend().Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDXContext->RSSetState(m_pState->CullNone().Get());
		}
		else
		{
			const auto pCull = (uMaskType == SUBSET_RFULL) ?
				m_pState->CullClockwise() : m_pState->CullCounterClockwise();
			m_pDXContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDXContext->RSSetState(pCull.Get());
		}
	}
}

void Model::setShaders(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS, const bool bReset)
{
	// Set Shaders
	if (bReset)
	{
		SetShaders(m_pDXContext, m_pShader, uVS, uGS, uPS);
		m_pDXContext->PSSetSamplers(m_uSmpAnisoWrap, 1, m_pState->AnisotropicWrap().GetAddressOf());
		m_pDXContext->PSSetSamplers(m_uSmpLinearCmp, 1, m_pState->LinearComparison().GetAddressOf());
	}
}

void Model::resetShaders(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
	const SubsetType uMaskType, const bool bReset)
{
	if (bReset || !(uMaskType & SUBSET_ATEST) || uMaskType == SUBSET_RFULL)
	{
		SetBlendStates(m_pDXContext, nullptr, nullptr);
		if (bReset)
		{
			if (uVS != NULL_SHADER) m_pDXContext->VSSetShader(nullptr, nullptr, 0);
			if (uGS != NULL_SHADER) m_pDXContext->GSSetShader(nullptr, nullptr, 0);
			if (uPS != NULL_SHADER) m_pDXContext->PSSetShader(nullptr, nullptr, 0);
		}
	}
}
