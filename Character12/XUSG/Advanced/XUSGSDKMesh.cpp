//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGSDKMesh.h"
#include "XUSGDDSLoader.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

//--------------------------------------------------------------------------------------
SDKMesh::SDKMesh() noexcept :
	m_device(nullptr),
	m_numOutstandingResources(0),
	m_isLoading(false),
	m_pStaticMeshData(nullptr),
	m_pHeapData(nullptr),
	m_pAnimationData(nullptr),
	m_ppVertices(nullptr),
	m_ppIndices(nullptr),
	m_strPathW(),
	m_strPath(),
	m_pMeshHeader(nullptr),
	m_pVertexBufferArray(nullptr),
	m_pIndexBufferArray(nullptr),
	m_pMeshArray(nullptr),
	m_pSubsetArray(nullptr),
	m_pFrameArray(nullptr),
	m_pMaterialArray(nullptr),
	m_pAdjacencyIndexBufferArray(nullptr),
	m_pAnimationHeader(nullptr),
	m_pAnimationFrameData(nullptr),
	m_pBindPoseFrameMatrices(nullptr),
	m_pTransformedFrameMatrices(nullptr),
	m_pWorldPoseFrameMatrices(nullptr)
{
}

//--------------------------------------------------------------------------------------
SDKMesh::~SDKMesh()
{
	Destroy();
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::Create(const Device &device, const wchar_t *szFileName,
	const TextureCache &textureCache)
{
	return createFromFile(device, szFileName, textureCache);
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::Create(const Device &device, uint8_t *pData,
	const TextureCache &textureCache, size_t dataBytes, bool bCopyStatic)
{
	return createFromMemory(device, pData, textureCache, dataBytes, bCopyStatic);
}


//--------------------------------------------------------------------------------------
HRESULT SDKMesh::LoadAnimation(_In_z_ const wchar_t *szFileName)
{
	HRESULT hr = E_FAIL;
	wchar_t strPath[MAX_PATH];

	// Find the path for the file
	wcsncpy_s(strPath, MAX_PATH, szFileName, wcslen(szFileName));
	//V_RETURN(DXUTFindDXSDKMediaFileCch(strPath, MAX_PATH, szFileName));

	// Open the file
	ifstream fileStream(strPath, ios::in | ios::binary);
	const auto fileBuffer = fileStream ? fileStream.rdbuf() : nullptr;
	if (!fileBuffer) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903);

	// Read header
	SDKANIMATION_FILE_HEADER fileheader;
	if (fileBuffer->sgetn(reinterpret_cast<char*>(&fileheader), sizeof(SDKANIMATION_FILE_HEADER)) < 0)
	{
		fileStream.close();

		return HRESULT_FROM_WIN32(GetLastError());
	}

	// Allocate
	m_pAnimationData = new (std::nothrow) uint8_t[static_cast<size_t>(sizeof(SDKANIMATION_FILE_HEADER) + fileheader.AnimationDataSize)];
	if (!m_pAnimationData)
	{
		fileStream.close();

		return E_OUTOFMEMORY;
	}

	// Read it all in
	if (fileBuffer->pubseekpos(0, fileStream.in) < 0)
	{
		fileStream.close();

		return HRESULT_FROM_WIN32(GetLastError());
	}

	const auto cBytes = static_cast<streamsize>(sizeof(SDKANIMATION_FILE_HEADER) + fileheader.AnimationDataSize);
	if (fileBuffer->sgetn(reinterpret_cast<char*>(m_pAnimationData), cBytes) < cBytes)
	{
		fileStream.close();

		return HRESULT_FROM_WIN32(GetLastError());
	}

	fileStream.close();

	// pointer fixup
	m_pAnimationHeader = reinterpret_cast<SDKANIMATION_FILE_HEADER*>(m_pAnimationData);
	m_pAnimationFrameData = reinterpret_cast<SDKANIMATION_FRAME_DATA*>(m_pAnimationData + m_pAnimationHeader->AnimationDataOffset);

	const auto BaseOffset = sizeof(SDKANIMATION_FILE_HEADER);

	for (auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
	{
		m_pAnimationFrameData[i].pAnimationData = reinterpret_cast<SDKANIMATION_DATA*>
			(m_pAnimationData + m_pAnimationFrameData[i].DataOffset + BaseOffset);

		const auto pFrame = FindFrame(m_pAnimationFrameData[i].FrameName);

		if (pFrame) pFrame->AnimationDataIndex = i;
	}

	return S_OK;
}

//--------------------------------------------------------------------------------------
void SDKMesh::Destroy()
{
	if (!CheckLoadDone()) return;

	if (m_pStaticMeshData)
	{
		if (m_pMaterialArray)
		{
			for (auto m = 0u; m < m_pMeshHeader->NumMaterials; ++m)
			{
				if (m_device)
				{
					m_pMaterialArray[m].pAlbedo = nullptr;
					m_pMaterialArray[m].pNormal = nullptr;
					m_pMaterialArray[m].pSpecular = nullptr;
				}
			}
		}
		for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
			SAFE_DELETE(m_pVertexBufferArray[i].pVertexBuffer);

		for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
			SAFE_DELETE(m_pIndexBufferArray[i].pIndexBuffer);
	}

	if (m_pAdjacencyIndexBufferArray)
		for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; i++ )
			SAFE_DELETE(m_pAdjacencyIndexBufferArray[i].pIndexBuffer);
	SAFE_DELETE_ARRAY(m_pAdjacencyIndexBufferArray);

	SAFE_DELETE_ARRAY(m_pHeapData);
	m_pStaticMeshData = nullptr;
	SAFE_DELETE_ARRAY(m_pAnimationData);
	SAFE_DELETE_ARRAY(m_pBindPoseFrameMatrices);
	SAFE_DELETE_ARRAY(m_pTransformedFrameMatrices);
	SAFE_DELETE_ARRAY(m_pWorldPoseFrameMatrices);

	SAFE_DELETE_ARRAY(m_ppVertices );
	SAFE_DELETE_ARRAY(m_ppIndices );

	m_pMeshHeader = nullptr;
	m_pVertexBufferArray = nullptr;
	m_pIndexBufferArray = nullptr;
	m_pMeshArray = nullptr;
	m_pSubsetArray = nullptr;
	m_pFrameArray = nullptr;
	m_pMaterialArray = nullptr;

	m_pAnimationHeader = nullptr;
	m_pAnimationFrameData = nullptr;
}

//--------------------------------------------------------------------------------------
// transform the bind pose
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::TransformBindPose(CXMMATRIX world)
{
	transformBindPoseFrame(0, world);
}

//--------------------------------------------------------------------------------------
// transform the mesh frames according to the animation for time fTime
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::TransformMesh(CXMMATRIX world, double fTime)
{
	if (!m_pAnimationHeader || FTT_RELATIVE == m_pAnimationHeader->FrameTransformType)
	{
		transformFrame(0, world, fTime);

		// For each frame, move the transform to the bind pose, then
		// move it to the final position
		for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		{
			const auto invBindPose = XMMatrixInverse(nullptr, XMLoadFloat4x4(&m_pBindPoseFrameMatrices[i]));
			const auto final = invBindPose * XMLoadFloat4x4(&m_pTransformedFrameMatrices[i]);
			XMStoreFloat4x4(&m_pTransformedFrameMatrices[i], final);
		}
	}
	else if (FTT_ABSOLUTE == m_pAnimationHeader->FrameTransformType)
		for (auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
			transformFrameAbsolute(i, fTime);
}

//--------------------------------------------------------------------------------------
PrimitiveTopology SDKMesh::GetPrimitiveType(_In_ SDKMESH_PRIMITIVE_TYPE PrimType)
{
	PrimitiveTopology retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	switch (PrimType)
	{
		case PT_TRIANGLE_LIST:
			retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			break;
		case PT_TRIANGLE_STRIP:
			retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			break;
		case PT_LINE_LIST:
			retType = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			break;
		case PT_LINE_STRIP:
			retType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			break;
		case PT_POINT_LIST:
			retType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			break;
		case PT_TRIANGLE_LIST_ADJ:
			retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
			break;
		case PT_TRIANGLE_STRIP_ADJ:
			retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
			break;
		case PT_LINE_LIST_ADJ:
			retType = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
			break;
		case PT_LINE_STRIP_ADJ:
			retType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
			break;
	};

	return retType;
}

//--------------------------------------------------------------------------------------
Format SDKMesh::GetIBFormat(_In_ uint32_t iMesh) const
{
	switch (m_pIndexBufferArray[m_pMeshArray[iMesh].IndexBuffer].IndexType)
	{
		case IT_16BIT:
			return DXGI_FORMAT_R16_UINT;
		case IT_32BIT:
			return DXGI_FORMAT_R32_UINT;
	};

	return DXGI_FORMAT_R16_UINT;
}

//--------------------------------------------------------------------------------------
VertexBuffer *SDKMesh::GetVertexBuffer(_In_ uint32_t iMesh, _In_ uint32_t iVB) const
{
	return m_pVertexBufferArray[m_pMeshArray[iMesh].VertexBuffers[iVB]].pVertexBuffer;
}

//--------------------------------------------------------------------------------------
IndexBuffer *SDKMesh::GetIndexBuffer(_In_ uint32_t iMesh ) const
{
	return m_pIndexBufferArray[m_pMeshArray[iMesh].IndexBuffer ].pIndexBuffer;
}

SDKMESH_INDEX_TYPE SDKMesh::GetIndexType(_In_ uint32_t iMesh) const
{
	return static_cast<SDKMESH_INDEX_TYPE>(m_pIndexBufferArray[m_pMeshArray[iMesh].IndexBuffer].IndexType);
}

//--------------------------------------------------------------------------------------
IndexBuffer *SDKMesh::GetAdjIndexBuffer(_In_ uint32_t iMesh) const
{
	return m_pAdjacencyIndexBufferArray[m_pMeshArray[iMesh].IndexBuffer].pIndexBuffer;
}

//--------------------------------------------------------------------------------------
const char *SDKMesh::GetMeshPathA() const
{
	return m_strPath.c_str();
}

//--------------------------------------------------------------------------------------
const wchar_t *SDKMesh::GetMeshPathW() const
{
	return m_strPathW.c_str();
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumMeshes() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMeshes : 0;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumMaterials() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMaterials : 0;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumVertexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumVertexBuffers : 0;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumIndexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumIndexBuffers : 0;
}

//--------------------------------------------------------------------------------------
VertexBuffer *SDKMesh::GetVertexBufferAt(_In_ uint32_t iVB) const
{
	return m_pVertexBufferArray[iVB].pVertexBuffer;
}

//--------------------------------------------------------------------------------------
IndexBuffer *SDKMesh::GetIndexBufferAt(_In_ uint32_t iIB) const
{
	return m_pIndexBufferArray[iIB].pIndexBuffer;
}

//--------------------------------------------------------------------------------------
uint8_t *SDKMesh::GetRawVerticesAt(_In_ uint32_t iVB) const
{
	return m_ppVertices[iVB];
}

//--------------------------------------------------------------------------------------
uint8_t *SDKMesh::GetRawIndicesAt(_In_ uint32_t iIB) const
{
	return m_ppIndices[iIB];
}

//--------------------------------------------------------------------------------------
SDKMESH_MATERIAL *SDKMesh::GetMaterial(_In_ uint32_t iMaterial) const
{
	return &m_pMaterialArray[iMaterial];
}

//--------------------------------------------------------------------------------------
SDKMESH_MESH *SDKMesh::GetMesh(_In_ uint32_t iMesh) const
{
	return &m_pMeshArray[iMesh];
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumSubsets(_In_ uint32_t iMesh) const
{
	return m_pMeshArray[iMesh].NumSubsets;
}

uint32_t SDKMesh::GetNumSubsets(_In_ uint32_t iMesh, _In_ SubsetFlag materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return static_cast<uint32_t>(m_classifiedSubsets[materialType - 1][iMesh].size());
}

//--------------------------------------------------------------------------------------
SDKMESH_SUBSET *SDKMesh::GetSubset(_In_ uint32_t iMesh, _In_ uint32_t iSubset) const
{
	return &m_pSubsetArray[m_pMeshArray[iMesh].pSubsets[iSubset]];
}

SDKMESH_SUBSET *SDKMesh::GetSubset(_In_ uint32_t iMesh, _In_ uint32_t iSubset, _In_ SubsetFlag materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return &m_pSubsetArray[m_classifiedSubsets[materialType - 1][iMesh][iSubset]];
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetVertexStride(_In_ uint32_t iMesh, _In_ uint32_t iVB) const
{
	return static_cast<uint32_t>(m_pVertexBufferArray[m_pMeshArray[iMesh].VertexBuffers[iVB]].StrideBytes);
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumFrames() const
{
	return m_pMeshHeader->NumFrames;
}

//--------------------------------------------------------------------------------------
SDKMESH_FRAME *SDKMesh::GetFrame(_In_ uint32_t iFrame) const
{
	assert(iFrame < m_pMeshHeader->NumFrames);

	return &m_pFrameArray[iFrame];
}

//--------------------------------------------------------------------------------------
SDKMESH_FRAME *SDKMesh::FindFrame(_In_z_ const char *szName) const
{
	const auto i = FindFrameIndex(szName);

	return i == INVALID_FRAME ? nullptr : &m_pFrameArray[i];
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::FindFrameIndex(_In_z_ const char *szName) const
{
	for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		if (_stricmp(m_pFrameArray[i].Name, szName) == 0)
			return i;

	return INVALID_FRAME;
}

//--------------------------------------------------------------------------------------
uint64_t SDKMesh::GetNumVertices(_In_ uint32_t iMesh, _In_ uint32_t iVB) const
{
	return m_pVertexBufferArray[m_pMeshArray[iMesh].VertexBuffers[iVB]].NumVertices;
}

//--------------------------------------------------------------------------------------
uint64_t SDKMesh::GetNumIndices(_In_ uint32_t iMesh) const
{
	return m_pIndexBufferArray[m_pMeshArray[iMesh].IndexBuffer].NumIndices;
}

//--------------------------------------------------------------------------------------
XMVECTOR SDKMesh::GetMeshBBoxCenter(_In_ uint32_t iMesh) const
{
	return XMLoadFloat3(&m_pMeshArray[iMesh].BoundingBoxCenter);
}

//--------------------------------------------------------------------------------------
XMVECTOR SDKMesh::GetMeshBBoxExtents(_In_ uint32_t iMesh) const
{
	return XMLoadFloat3(&m_pMeshArray[iMesh].BoundingBoxExtents);
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetOutstandingResources() const
{
	auto outstandingResources = 0u;
	if (!m_pMeshHeader) return 1;

	outstandingResources += GetOutstandingBufferResources();

	if (m_device)
	{
		for (auto i = 0u; i < m_pMeshHeader->NumMaterials; ++i)
		{
			if (m_pMaterialArray[i].DiffuseTexture[0] != 0)
				if (!m_pMaterialArray[i].pAlbedo && !IsErrorResource(m_pMaterialArray[i].Force64_Albedo))
					++outstandingResources;

			if (m_pMaterialArray[i].NormalTexture[0] != 0)
				if (!m_pMaterialArray[i].pNormal && !IsErrorResource(m_pMaterialArray[i].Force64_Normal))
					++outstandingResources;

			if (m_pMaterialArray[i].SpecularTexture[0] != 0)
				if (!m_pMaterialArray[i].pSpecular && !IsErrorResource(m_pMaterialArray[i].Force64_Specular))
					++outstandingResources;
		}
	}

	return outstandingResources;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetOutstandingBufferResources() const
{
	uint32_t outstandingResources = 0;
	if (!m_pMeshHeader) return 1;

	return outstandingResources;
}

//--------------------------------------------------------------------------------------
bool SDKMesh::CheckLoadDone()
{
	if (0 == GetOutstandingResources())
	{
		m_isLoading = false;

		return true;
	}

	return false;
}

//--------------------------------------------------------------------------------------
bool SDKMesh::IsLoaded() const
{
	if( m_pStaticMeshData && !m_isLoading )
	{
		return true;
	}

	return false;
}

//--------------------------------------------------------------------------------------
bool SDKMesh::IsLoading() const
{
	return m_isLoading;
}

//--------------------------------------------------------------------------------------
void SDKMesh::SetLoading(_In_ bool bLoading)
{
	m_isLoading = bLoading;
}

//--------------------------------------------------------------------------------------
bool SDKMesh::HadLoadingError() const
{
	return false;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumInfluences(_In_ uint32_t iMesh) const
{
	return m_pMeshArray[iMesh].NumFrameInfluences;
}

//--------------------------------------------------------------------------------------
XMMATRIX SDKMesh::GetMeshInfluenceMatrix( _In_ uint32_t iMesh, _In_ uint32_t iInfluence ) const
{
	const auto iFrame = m_pMeshArray[iMesh].pFrameInfluences[iInfluence];

	return XMLoadFloat4x4(&m_pTransformedFrameMatrices[iFrame]);
}

XMMATRIX SDKMesh::GetWorldMatrix(_In_ uint32_t iFrameIndex) const
{
	return XMLoadFloat4x4(&m_pWorldPoseFrameMatrices[iFrameIndex]);
}

XMMATRIX SDKMesh::GetInfluenceMatrix(_In_ uint32_t iFrameIndex) const
{
	return XMLoadFloat4x4(&m_pTransformedFrameMatrices[iFrameIndex]);
}

XMMATRIX SDKMesh::GetBindMatrix(uint32_t iFrameIndex) const
{
	return XMLoadFloat4x4(&m_pBindPoseFrameMatrices[iFrameIndex]);
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetAnimationKeyFromTime(_In_ double fTime) const
{
	if (!m_pAnimationHeader) return 0;

	auto iTick = static_cast<uint32_t>(m_pAnimationHeader->AnimationFPS * fTime);

	iTick = iTick % (m_pAnimationHeader->NumAnimationKeys - 1);

	return ++iTick;
}

_Use_decl_annotations_
bool SDKMesh::GetAnimationProperties(uint32_t *pNumKeys, float *pFrameTime) const
{
	if (!m_pAnimationHeader)
	{
		*pNumKeys = 0;
		*pFrameTime = 0;

		return false;
	}

	*pNumKeys = m_pAnimationHeader->NumAnimationKeys;
	*pFrameTime = 1.0f / static_cast<float>(m_pAnimationHeader->AnimationFPS);

	return true;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::loadMaterials(const GraphicsCommandList &commandList, SDKMESH_MATERIAL *pMaterials,
	uint32_t numMaterials, vector<Resource> &uploaders)
{
	string strPath;
	wstring strPathW;
	DDS::Loader textureLoader;

	for (auto m = 0u; m < numMaterials; ++m)
	{
		pMaterials[m].pAlbedo = nullptr;
		pMaterials[m].pNormal = nullptr;
		pMaterials[m].pSpecular = nullptr;

		// load textures
		if (pMaterials[m].DiffuseTexture[0] != 0)
		{
			strPath = m_strPath + pMaterials[m].DiffuseTexture;
			const auto textureIter = m_textureCache->find(strPath);

			if (textureIter != m_textureCache->end())
				pMaterials[m].pAlbedo = textureIter->second.get();
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				strPathW.assign(strPath.begin(), strPath.end());
				if (FAILED(textureLoader.CreateTextureFromFile(m_device, commandList, strPathW.c_str(),
					8192, true, texture, uploaders.back())))
					pMaterials[m].Force64_Albedo = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pAlbedo = texture.get();
					(*m_textureCache)[strPath] = texture;
				}
			}
		}
		if (pMaterials[m].NormalTexture[0] != 0)
		{
			strPath = m_strPath + pMaterials[m].NormalTexture;
			const auto textureIter = m_textureCache->find(strPath);

			if (textureIter != m_textureCache->end())
				pMaterials[m].pNormal = textureIter->second.get();
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				strPathW.assign(strPath.begin(), strPath.end());
				if (FAILED(textureLoader.CreateTextureFromFile(m_device, commandList, strPathW.c_str(),
					8192, false, texture, uploaders.back())))
					pMaterials[m].Force64_Normal = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pNormal = texture.get();
					(*m_textureCache)[strPath] = texture;
				}
			}
		}
		if (pMaterials[m].SpecularTexture[0] != 0)
		{
			strPath = m_strPath + pMaterials[m].SpecularTexture;
			const auto textureIter = m_textureCache->find(strPath);

			if (textureIter != m_textureCache->end())
				pMaterials[m].pSpecular = textureIter->second.get();
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				strPathW.assign(strPath.begin(), strPath.end());
				if (FAILED(textureLoader.CreateTextureFromFile(m_device, commandList, strPathW.c_str(),
					8192, false, texture, uploaders.back())))
					pMaterials[m].Force64_Specular = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pSpecular = texture.get();
					(*m_textureCache)[strPath] = texture;
				}
			}
		}
	}
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createVertexBuffer(const GraphicsCommandList &commandList, SDKMESH_VERTEX_BUFFER_HEADER *pHeader,
	void *pVertices, vector<Resource> &uploaders)
{
	HRESULT hr = S_OK;
	pHeader->DataOffset = 0;

	//Vertex Buffer
	pHeader->pVertexBuffer = new VertexBuffer();
	pHeader->pVertexBuffer->Create(m_device, static_cast<uint32_t>(pHeader->SizeBytes),
		static_cast<uint32_t>(pHeader->StrideBytes), D3D12_RESOURCE_FLAG_NONE,
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

	uploaders.push_back(Resource());
	pHeader->pVertexBuffer->Upload(commandList, uploaders.back(), pVertices,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	return hr;
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createIndexBuffer(const GraphicsCommandList &commandList, SDKMESH_INDEX_BUFFER_HEADER *pHeader,
	void *pIndices, std::vector<Resource> &uploaders)
{
	HRESULT hr = S_OK;
	pHeader->DataOffset = 0;

	//Index Buffer
	//pHeader->pIB11 = make_unique<IndexBuffer>(m_pDev11);
	pHeader->pIndexBuffer = new IndexBuffer();
	pHeader->pIndexBuffer->Create(m_device, static_cast<uint32_t>(pHeader->SizeBytes),
		pHeader->IndexType == IT_32BIT ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT,
		D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE, D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COPY_DEST);

	uploaders.push_back(Resource());
	pHeader->pIndexBuffer->Upload(commandList, uploaders.back(), pIndices,
		D3D12_RESOURCE_STATE_INDEX_BUFFER);

	return hr;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createFromFile(const Device &device, const wchar_t *szFileName,
	const TextureCache &textureCache)
{
	HRESULT hr = S_OK;

	// Find the path for the file
	m_strPathW = szFileName;
	//V_RETURN(DXUTFindDXSDKMediaFileCch(m_strPathW, sizeof(m_strPathW) / sizeof(WCHAR), szFileName));

	// Open the file
	ifstream fileStream(m_strPathW, ios::in | ios::binary);
	const auto fileBuffer = fileStream ? fileStream.rdbuf() : nullptr;
	if (!fileBuffer) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903);

	// Change the path to just the directory
	const auto found = m_strPathW.find_last_of(L"/\\");
	m_strPathW = m_strPathW.substr(0, found + 1);

	m_strPath.assign(m_strPathW.begin(), m_strPathW.end());

	// Get the file size
	const auto cBytes = fileBuffer->pubseekoff(0, fileStream.end, fileStream.in);
	if (fileBuffer->pubseekpos(0, fileStream.in) < 0)
	{
		fileStream.close();

		return E_FAIL;
	}

	// Allocate memory
	m_pStaticMeshData = new (std::nothrow) uint8_t[cBytes];
	if (!m_pStaticMeshData)
	{
		fileStream.close();

		return E_OUTOFMEMORY;
	}

	// Read in the file
	if (fileBuffer->sgetn(reinterpret_cast<char*>(m_pStaticMeshData), cBytes) < cBytes)
		hr = E_FAIL;

	fileStream.close();

	if (SUCCEEDED(hr))
	{
		hr = createFromMemory(device, m_pStaticMeshData, textureCache, cBytes, false);

		if (FAILED(hr)) delete [] m_pStaticMeshData;
	}

	return hr;
}

_Use_decl_annotations_
HRESULT SDKMesh::createFromMemory(const Device &device, uint8_t *pData,
	const TextureCache &textureCache, size_t dataBytes, bool bCopyStatic)
{
	XMFLOAT3 lower; 
	XMFLOAT3 upper; 
	
	m_device = device;
	ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	GraphicsCommandList commandList = nullptr;
	if (device)
	{
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
	}

	if (dataBytes < sizeof(SDKMESH_HEADER)) return E_FAIL;

	// Set outstanding resources to zero
	m_numOutstandingResources = 0;

	if (bCopyStatic)
	{
		auto pHeader = reinterpret_cast<SDKMESH_HEADER*>( pData );

		SIZE_T StaticSize = static_cast<SIZE_T>(pHeader->HeaderSize + pHeader->NonBufferDataSize);
		if (dataBytes < StaticSize) return E_FAIL;

		m_pHeapData = new (std::nothrow) uint8_t[StaticSize];
		if (!m_pHeapData) return E_OUTOFMEMORY;

		m_pStaticMeshData = m_pHeapData;

		memcpy(m_pStaticMeshData, pData, StaticSize);
	}
	else
	{
		m_pHeapData = pData;
		m_pStaticMeshData = pData;
	}

	// Pointer fixup
	m_pMeshHeader = reinterpret_cast<SDKMESH_HEADER*>(m_pStaticMeshData);

	m_pVertexBufferArray = reinterpret_cast<SDKMESH_VERTEX_BUFFER_HEADER*>(m_pStaticMeshData + m_pMeshHeader->VertexStreamHeadersOffset);
	m_pIndexBufferArray = reinterpret_cast<SDKMESH_INDEX_BUFFER_HEADER*>(m_pStaticMeshData + m_pMeshHeader->IndexStreamHeadersOffset);
	m_pMeshArray = reinterpret_cast<SDKMESH_MESH*>(m_pStaticMeshData + m_pMeshHeader->MeshDataOffset);
	m_pSubsetArray = reinterpret_cast<SDKMESH_SUBSET*>(m_pStaticMeshData + m_pMeshHeader->SubsetDataOffset);
	m_pFrameArray = reinterpret_cast<SDKMESH_FRAME*>(m_pStaticMeshData + m_pMeshHeader->FrameDataOffset);
	m_pMaterialArray = reinterpret_cast<SDKMESH_MATERIAL*>(m_pStaticMeshData + m_pMeshHeader->MaterialDataOffset);

	// Setup subsets
	for(auto i = 0u; i < m_pMeshHeader->NumMeshes; ++i)
	{
		m_pMeshArray[i].pSubsets = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].SubsetOffset);
		m_pMeshArray[i].pFrameInfluences = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].FrameInfluenceOffset);
	}

	// error condition
	if (m_pMeshHeader->Version != SDKMESH_FILE_VERSION) return E_NOINTERFACE;

	// Setup buffer data pointer
	uint8_t* pBufferData = pData + m_pMeshHeader->HeaderSize + m_pMeshHeader->NonBufferDataSize;

	// Get the start of the buffer data
	uint64_t BufferDataStart = m_pMeshHeader->HeaderSize + m_pMeshHeader->NonBufferDataSize;

	// Uploader buffers
	vector<Resource> uploaders;

	// Create VBs
	m_ppVertices = new (std::nothrow) uint8_t*[m_pMeshHeader->NumVertexBuffers];
	if (!m_ppVertices) return E_OUTOFMEMORY;

	for(auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		uint8_t *pVertices = nullptr;
		pVertices = reinterpret_cast<uint8_t*>(pBufferData + (m_pVertexBufferArray[i].DataOffset - BufferDataStart));

		if (commandList) createVertexBuffer(commandList, &m_pVertexBufferArray[i], pVertices, uploaders);

		m_ppVertices[i] = pVertices;
	}

	// Create IBs
	m_ppIndices = new (std::nothrow) uint8_t*[m_pMeshHeader->NumIndexBuffers];
	if (!m_ppIndices) return E_OUTOFMEMORY;

	for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
	{
		uint8_t *pIndices = nullptr;
		pIndices = reinterpret_cast<uint8_t*>(pBufferData + ( m_pIndexBufferArray[i].DataOffset - BufferDataStart));

		if (commandList) createIndexBuffer(commandList, &m_pIndexBufferArray[i], pIndices, uploaders);

		m_ppIndices[i] = pIndices;
	}

	// Load Materials
	m_textureCache = textureCache;
	if (commandList) loadMaterials(commandList, m_pMaterialArray, m_pMeshHeader->NumMaterials, uploaders);

	// Execute commands
	executeCommandList(commandList);

	// Create a place to store our bind pose frame matrices
	m_pBindPoseFrameMatrices = new (std::nothrow) XMFLOAT4X4[m_pMeshHeader->NumFrames];
	if (!m_pBindPoseFrameMatrices) return E_OUTOFMEMORY;

	// Create a place to store our transformed frame matrices
	m_pTransformedFrameMatrices = new (std::nothrow) XMFLOAT4X4[m_pMeshHeader->NumFrames];
	if (!m_pTransformedFrameMatrices) return E_OUTOFMEMORY;

	m_pWorldPoseFrameMatrices = new (std::nothrow) XMFLOAT4X4[m_pMeshHeader->NumFrames];
	if (!m_pWorldPoseFrameMatrices) return E_OUTOFMEMORY;

	SDKMESH_SUBSET* pSubset = nullptr;
	PrimitiveTopology primType;

	// update bounding volume 
	SDKMESH_MESH *currentMesh = m_pMeshArray;
	auto tris = 0;

	for (auto m = 0u; m < m_pMeshHeader->NumMeshes; ++m)
	{
		lower.x = FLT_MAX; lower.y = FLT_MAX; lower.z = FLT_MAX;
		upper.x = -FLT_MAX; upper.y = -FLT_MAX; upper.z = -FLT_MAX;
		currentMesh = GetMesh(m);
		const auto indSize = m_pIndexBufferArray[currentMesh->IndexBuffer].IndexType == IT_16BIT ? 2 : 4;

		for (auto subset = 0u; subset < currentMesh->NumSubsets; ++subset)
		{
			pSubset = GetSubset(m, subset);	//&m_pSubsetArray[currentMesh->pSubsets[subset]];

			primType = GetPrimitiveType(static_cast<SDKMESH_PRIMITIVE_TYPE>(pSubset->PrimitiveType));
			assert(primType == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);	// only triangle lists are handled.

			const auto indexCount = static_cast<uint32_t>(pSubset->IndexCount);
			const auto indexStart = static_cast<uint32_t>(pSubset->IndexStart);

			//if (bAdjacent)
			//{
			//	IndexCount *= 2;
			//	IndexStart *= 2;
			//}

			//uint8_t* pIndices = nullptr;
			//m_ppIndices[i]
			const auto indices = reinterpret_cast<uint32_t*>(m_ppIndices[currentMesh->IndexBuffer]);
			const auto vertices = reinterpret_cast<float*>(m_ppVertices[currentMesh->VertexBuffers[0]]);
			auto stride = static_cast<uint32_t>(m_pVertexBufferArray[currentMesh->VertexBuffers[0]].StrideBytes);
			assert (stride % 4 == 0);
			stride /= 4;

			for (auto vertIdx = indexStart; vertIdx < indexStart + indexCount; ++vertIdx)
			{
				auto currentIndex = 0u;
				if (indSize == 2)
				{
					const auto ind_div2 = vertIdx / 2;
					currentIndex = indices[ind_div2];
					if (vertIdx % 2 == 0)
					{
						currentIndex = currentIndex << 16;
						currentIndex = currentIndex >> 16;
					}else
					{
						currentIndex = currentIndex >> 16;
					}
				}
				else currentIndex = indices[vertIdx];
				++tris;
				const auto pt = reinterpret_cast<XMFLOAT3*>(&vertices[stride * currentIndex]);
				if (pt->x < lower.x) lower.x = pt->x;
				if (pt->y < lower.y) lower.y = pt->y;
				if (pt->z < lower.z) lower.z = pt->z;
				if (pt->x > upper.x) upper.x = pt->x;
				if (pt->y > upper.y) upper.y = pt->y;
				if (pt->z > upper.z) upper.z = pt->z;
				//uint8_t** m_ppVertices;
				//uint8_t** m_ppIndices;
			}
			//pd3dDeviceContext->DrawIndexed(IndexCount, IndexStart, VertexStart);
		}

		XMFLOAT3 half((upper.x - lower.x) * 0.5f, (upper.y - lower.y) * 0.5f, (upper.z - lower.z) * 0.5f);

		currentMesh->BoundingBoxCenter.x = lower.x + half.x;
		currentMesh->BoundingBoxCenter.y = lower.y + half.y;
		currentMesh->BoundingBoxCenter.z = lower.z + half.z;
		currentMesh->BoundingBoxExtents = half;
	}
	// Update 

	// Classify material type for each subset
	classifyMaterialType();

	return S_OK;
}

_Use_decl_annotations_
void SDKMesh::classifyMaterialType()
{
	const auto numMeshes = GetNumMeshes();
	for (auto &subsets : m_classifiedSubsets)
		subsets.resize(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		const auto &numSubsets = m_pMeshArray[m].NumSubsets;
		for (auto s = 0u; s < numSubsets; ++s)
		{
			const auto &subsetIdx = m_pMeshArray[m].pSubsets[s];
			const auto &pSubset = m_pSubsetArray[subsetIdx];
			const auto pMaterial = GetMaterial(pSubset.MaterialID);
			
			auto subsetType = SUBSET_OPAQUE - 1;
			if (pMaterial && pMaterial->pAlbedo)// && !IsErrorResource(pMaterial->Force64_Albedo))
			{
				switch (pMaterial->pAlbedo->GetResource()->GetDesc().Format)
				{
				case DXGI_FORMAT_BC2_UNORM:
				case DXGI_FORMAT_BC2_UNORM_SRGB:
				case DXGI_FORMAT_BC3_UNORM:
				case DXGI_FORMAT_BC3_UNORM_SRGB:
				case DXGI_FORMAT_BC4_UNORM:
				case DXGI_FORMAT_BC5_UNORM:

				case DXGI_FORMAT_B8G8R8A8_UNORM:
				case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				case DXGI_FORMAT_B5G5R5A1_UNORM:
				case DXGI_FORMAT_B4G4R4A4_UNORM:

				case DXGI_FORMAT_R8G8B8A8_UNORM:
				case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				case DXGI_FORMAT_R10G10B10A2_UNORM:

				case DXGI_FORMAT_R16G16B16A16_FLOAT:
				case DXGI_FORMAT_R16G16B16A16_UNORM:
				case DXGI_FORMAT_R32G32B32A32_FLOAT:
					subsetType = SUBSET_ALPHA - 1;
				}
			}
			m_classifiedSubsets[subsetType][m].push_back(subsetIdx);
		}

		m_classifiedSubsets[SUBSET_OPAQUE - 1][m].shrink_to_fit();
		m_classifiedSubsets[SUBSET_ALPHA - 1][m].shrink_to_fit();
	}
}

void SDKMesh::executeCommandList(const GraphicsCommandList &commandList)
{
	if (commandList)
	{
		// Describe and create the command queue.
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
		ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

		// Close the command list and execute it to begin the initial GPU setup.
		ThrowIfFailed(commandList->Close());
		ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Create synchronization objects and wait until assets have been uploaded to the GPU.
		{
			HANDLE fenceEvent;
			ComPtr<ID3D12Fence> fence;
			uint64_t fenceValue = 0;

			ThrowIfFailed(m_device->CreateFence(fenceValue++, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

			// Create an event handle to use for frame synchronization.
			fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (fenceEvent == nullptr)
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

			// Wait for the command list to execute; we are reusing the same command 
			// list in our main loop but for now, we just want to wait for setup to 
			// complete before continuing.
			// Schedule a Signal command in the queue.
			ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue));

			// Wait until the fence has been processed, and increment the fence value for the current frame.
			ThrowIfFailed(fence->SetEventOnCompletion(fenceValue++, fenceEvent));
			WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE);
		}
	}
}

//--------------------------------------------------------------------------------------
// transform bind pose frame using a recursive traversal
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::transformBindPoseFrame(uint32_t iFrame, CXMMATRIX parentWorld)
{
	if (!m_pBindPoseFrameMatrices) return;

	// Transform ourselves
	const auto m = XMLoadFloat4x4(&m_pFrameArray[iFrame].Matrix);
	const auto mLocalWorld = XMMatrixMultiply(m, parentWorld);
	DirectX::XMStoreFloat4x4(&m_pBindPoseFrameMatrices[iFrame], mLocalWorld);

	// Transform our siblings
	if (m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME)
		transformBindPoseFrame(m_pFrameArray[iFrame].SiblingFrame, parentWorld);

	// Transform our children
	if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME)
		transformBindPoseFrame(m_pFrameArray[iFrame].ChildFrame, mLocalWorld);
}

//--------------------------------------------------------------------------------------
// transform frame using a recursive traversal
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::transformFrame(uint32_t iFrame, CXMMATRIX parentWorld, double fTime)
{
	// Get the tick data
	XMMATRIX localTransform;
	const auto tick = GetAnimationKeyFromTime(fTime);

	if (INVALID_ANIMATION_DATA != m_pFrameArray[iFrame].AnimationDataIndex)
	{
		const auto frameData = &m_pAnimationFrameData[m_pFrameArray[iFrame].AnimationDataIndex];
		const auto data = &frameData->pAnimationData[tick];

		// Turn it into a matrix
		const auto translate = XMMatrixTranslation(data->Translation.x, data->Translation.y, data->Translation.z);
		const auto scaling = XMMatrixScaling(data->Scaling.x, data->Scaling.y, data->Scaling.z); // BY STARS----Scaling
		auto quat = XMLoadFloat4(&data->Orientation);

		if (XMVector4Equal(quat, g_XMZero)) quat = XMQuaternionIdentity();

		quat = XMQuaternionNormalize(quat);
		const auto quatMatrix = XMMatrixRotationQuaternion(quat);
		localTransform = scaling * quatMatrix * translate;
	}
	else localTransform = XMLoadFloat4x4(&m_pFrameArray[iFrame].Matrix);

	// Transform ourselves
	auto localWorld = XMMatrixMultiply(localTransform, parentWorld);
	XMStoreFloat4x4(&m_pTransformedFrameMatrices[iFrame], localWorld);
	XMStoreFloat4x4(&m_pWorldPoseFrameMatrices[iFrame], localWorld);

	// Transform our siblings
	if (m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME)
		transformFrame(m_pFrameArray[iFrame].SiblingFrame, parentWorld, fTime);

	// Transform our children
	if (m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME)
		transformFrame(m_pFrameArray[iFrame].ChildFrame, localWorld, fTime);
}

//--------------------------------------------------------------------------------------
// transform frame assuming that it is an absolute transformation
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::transformFrameAbsolute(uint32_t iFrame, double fTime)
{
	const auto iTick = GetAnimationKeyFromTime(fTime);

	if (INVALID_ANIMATION_DATA != m_pFrameArray[iFrame].AnimationDataIndex)
	{
		const auto pFrameData = &m_pAnimationFrameData[m_pFrameArray[iFrame].AnimationDataIndex];
		const auto pData = &pFrameData->pAnimationData[iTick ];
		const auto pDataOrig = &pFrameData->pAnimationData[0];

		const auto mTrans1 = XMMatrixTranslation(-pDataOrig->Translation.x, -pDataOrig->Translation.y, -pDataOrig->Translation.z);
		const auto mTrans2 = XMMatrixTranslation(pData->Translation.x, pData->Translation.y, pData->Translation.z);

		auto quat1 = XMVectorSet(pDataOrig->Orientation.x, pDataOrig->Orientation.y, pDataOrig->Orientation.z, pDataOrig->Orientation.w);
		quat1 = XMQuaternionInverse(quat1);
		const auto mRot1 = XMMatrixRotationQuaternion(quat1);
		const auto mInvTo = mTrans1 * mRot1;

		const auto quat2 = XMVectorSet(pData->Orientation.x, pData->Orientation.y, pData->Orientation.z, pData->Orientation.w);
		const auto mRot2 = XMMatrixRotationQuaternion(quat2);
		const auto mFrom = mRot2 * mTrans2;

		const auto mOutput = mInvTo * mFrom;
		XMStoreFloat4x4(&m_pTransformedFrameMatrices[iFrame], mOutput);
	}
}
