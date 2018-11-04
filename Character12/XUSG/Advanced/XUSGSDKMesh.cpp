//--------------------------------------------------------------------------------------
// File: SDKMesh.cpp
//
// The SDK Mesh format (.sdkmesh) is not a recommended file format for games.  
// It was designed to meet the specific needs of the SDK samples.  Any real-world 
// applications should avoid this file format in favor of a destination format that 
// meets the specific needs of the application.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=320437
//--------------------------------------------------------------------------------------
#include "XUSGSDKMesh.h"
//#include "SDKMisc.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

//--------------------------------------------------------------------------------------
SDKMesh::SDKMesh() noexcept :
    m_numOutstandingResources(0),
    m_isLoading(false),
    m_filehandle(0),
    //m_hFileMappingObject(0),
    m_device(nullptr),
    m_commandList(nullptr),
    m_pStaticMeshData(nullptr),
    m_pHeapData(nullptr),
    m_pAnimationData(nullptr),
    m_ppVertices(nullptr),
    m_ppIndices(nullptr),
    m_strPathW{},
    m_strPath{},
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
HRESULT SDKMesh::Create(const Device &device, const wchar_t *szFileName)
{
    return createFromFile(device, szFileName);
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::Create(const Device &device, uint8_t *pData, size_t DataBytes, bool bCopyStatic)
{
    return createFromMemory(device, pData, DataBytes, bCopyStatic);
}


//--------------------------------------------------------------------------------------
HRESULT SDKMesh::LoadAnimation(_In_z_ const wchar_t *szFileName)
{
    HRESULT hr = E_FAIL;
    DWORD dwBytesRead = 0;
    LARGE_INTEGER liMove;
	wchar_t strPath[MAX_PATH];

    // Find the path for the file
	wcsncpy_s(strPath, MAX_PATH, szFileName, wcslen(szFileName));
    //V_RETURN(DXUTFindDXSDKMediaFileCch(strPath, MAX_PATH, szFileName));

    // Open the file
    const auto hFile = CreateFile(strPath, FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (INVALID_HANDLE_VALUE == hFile)
		return E_FAIL;//DXUTERR_MEDIANOTFOUND;

    /////////////////////////
    // Header
    SDKANIMATION_FILE_HEADER fileheader;
    if (!ReadFile(hFile, &fileheader, sizeof( SDKANIMATION_FILE_HEADER ), &dwBytesRead, nullptr))
    {
        CloseHandle(hFile);

        return HRESULT_FROM_WIN32(GetLastError());
    }

    //allocate
    m_pAnimationData = new (std::nothrow) uint8_t[static_cast<size_t>(sizeof(SDKANIMATION_FILE_HEADER) + fileheader.AnimationDataSize)];
    if (!m_pAnimationData)
    {
        CloseHandle(hFile);

        return E_OUTOFMEMORY;
    }

    // read it all in
    liMove.QuadPart = 0;
    if (!SetFilePointerEx(hFile, liMove, nullptr, FILE_BEGIN))
    {
        CloseHandle(hFile);

        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!ReadFile(hFile, m_pAnimationData, static_cast<DWORD>(sizeof( SDKANIMATION_FILE_HEADER) +
		fileheader.AnimationDataSize ),&dwBytesRead, nullptr))
    {
        CloseHandle(hFile);

        return HRESULT_FROM_WIN32(GetLastError());
    }

    // pointer fixup
    m_pAnimationHeader = reinterpret_cast<SDKANIMATION_FILE_HEADER*>(m_pAnimationData);
    m_pAnimationFrameData = reinterpret_cast<SDKANIMATION_FRAME_DATA*>(m_pAnimationData + m_pAnimationHeader->AnimationDataOffset);

    const auto BaseOffset = sizeof(SDKANIMATION_FILE_HEADER);

    for (auto i = 0u; i < m_pAnimationHeader->NumFrames; i++ )
    {
        m_pAnimationFrameData[i].pAnimationData = reinterpret_cast<SDKANIMATION_DATA*>
			(m_pAnimationData + m_pAnimationFrameData[i].DataOffset + BaseOffset);

        const auto pFrame = FindFrame(m_pAnimationFrameData[i].FrameName);

        if( pFrame ) pFrame->AnimationDataIndex = i;
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
void SDKMesh::ReleaseUploadResources()
{
	m_resourceUploads.clear();
	m_resourceUploads.resize(0);
	m_resourceUploads.shrink_to_fit();
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
                    if (m_pMaterialArray[m].pAlbedo && !IsErrorResource(m_pMaterialArray[m].pAlbedo->GetSRV().ptr))
						SAFE_DELETE(m_pMaterialArray[m].pAlbedo);

                    if( m_pMaterialArray[m].pNormal && !IsErrorResource( m_pMaterialArray[m].pNormal->GetSRV().ptr))
						SAFE_DELETE(m_pMaterialArray[m].pNormal);

                    if( m_pMaterialArray[m].pSpecular && !IsErrorResource( m_pMaterialArray[m].pSpecular->GetSRV().ptr))
						SAFE_DELETE(m_pMaterialArray[m].pSpecular);
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
            auto m = XMLoadFloat4x4( &m_pBindPoseFrameMatrices[i] );
			const auto mInvBindPose = XMMatrixInverse( nullptr, m );
            m = XMLoadFloat4x4( &m_pTransformedFrameMatrices[i] );
			const auto mFinal = mInvBindPose * m;
            XMStoreFloat4x4( &m_pTransformedFrameMatrices[i], mFinal );
        }
    }
    else if (FTT_ABSOLUTE == m_pAnimationHeader->FrameTransformType)
		for(auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
			transformFrameAbsolute( i, fTime );
}


#if 0
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::Render( ID3D11DeviceContext* pd3dDeviceContext,
                           UINT iDiffuseSlot,
                           UINT iNormalSlot,
                           UINT iSpecularSlot )
{
    RenderFrame( 0, false, pd3dDeviceContext, iDiffuseSlot, iNormalSlot, iSpecularSlot );
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderAdjacent( ID3D11DeviceContext* pd3dDeviceContext,
                                   UINT iDiffuseSlot,
                                   UINT iNormalSlot,
                                   UINT iSpecularSlot )
{
    RenderFrame( 0, true, pd3dDeviceContext, iDiffuseSlot, iNormalSlot, iSpecularSlot );
}
#endif

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
    return m_strPath;
}

//--------------------------------------------------------------------------------------
const wchar_t *SDKMesh::GetMeshPathW() const
{
    return m_strPathW;
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
uint32_t SDKMesh::GetNumVBs() const
{
    return m_pMeshHeader ? m_pMeshHeader->NumVertexBuffers : 0;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumIBs() const
{
    return m_pMeshHeader ? m_pMeshHeader->NumIndexBuffers : 0;
}

//--------------------------------------------------------------------------------------
VertexBuffer *SDKMesh::GetVB11At(_In_ uint32_t iVB) const
{
    return m_pVertexBufferArray[iVB].pVertexBuffer;
}

//--------------------------------------------------------------------------------------
IndexBuffer *SDKMesh::GetIB11At(_In_ uint32_t iIB) const
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

//--------------------------------------------------------------------------------------
SDKMESH_SUBSET *SDKMesh::GetSubset(_In_ uint32_t iMesh, _In_ uint32_t iSubset) const
{
    return &m_pSubsetArray[m_pMeshArray[iMesh].pSubsets[iSubset]];
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
SDKMESH_FRAME *SDKMesh::FindFrame(_In_z_ const char *pszName) const
{
    for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		if (_stricmp(m_pFrameArray[i].Name, pszName ) == 0)
			return &m_pFrameArray[i];

    return nullptr;
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
				if (!m_pMaterialArray[i].pAlbedo && !IsErrorResource(m_pMaterialArray[i].pAlbedo->GetSRV().ptr))
					++outstandingResources;

			if (m_pMaterialArray[i].NormalTexture[0] != 0)
				if (!m_pMaterialArray[i].pNormal && !IsErrorResource(m_pMaterialArray[i].pNormal->GetSRV().ptr))
					++outstandingResources;

			if (m_pMaterialArray[i].SpecularTexture[0] != 0)
				if (!m_pMaterialArray[i].pSpecular && !IsErrorResource(m_pMaterialArray[i].pSpecular->GetSRV().ptr))
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
void SDKMesh::loadMaterials(SDKMESH_MATERIAL *pMaterials, uint32_t numMaterials)
{
    char strPath[MAX_PATH];

	for (auto m = 0u; m < numMaterials; ++m)
	{
		pMaterials[m].pAlbedo = nullptr;
		pMaterials[m].pNormal = nullptr;
		pMaterials[m].pSpecular = nullptr;

		// load textures
		if (pMaterials[m].DiffuseTexture[0] != 0)
		{
			sprintf_s(strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].DiffuseTexture);
			/* if (FAILED(DXUTGetGlobalResourceCache().CreateTextureFromFile(pd3dDevice, DXUTGetD3D11DeviceContext(),
			strPath, &pMaterials[m].pDiffuseRV11, true)))
			pMaterials[m].pDiffuseRV11 = ( ID3D11ShaderResourceView* )ERROR_RESOURCE_VALUE;*/

		}
		if (pMaterials[m].NormalTexture[0] != 0)
		{
			/*sprintf_s( strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].NormalTexture );
			if( FAILED( DXUTGetGlobalResourceCache().CreateTextureFromFile( pd3dDevice, DXUTGetD3D11DeviceContext(),
			strPath,
			&pMaterials[m].pNormalRV11 ) ) )
			pMaterials[m].pNormalRV11 = ( ID3D11ShaderResourceView* )ERROR_RESOURCE_VALUE;*/
		}
		if (pMaterials[m].SpecularTexture[0] != 0)
		{
			sprintf_s(strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].SpecularTexture);
			/*if( FAILED( DXUTGetGlobalResourceCache().CreateTextureFromFile( pd3dDevice, DXUTGetD3D11DeviceContext(),
			strPath,
			&pMaterials[m].pSpecularRV11 ) ) )
			pMaterials[m].pSpecularRV11 = ( ID3D11ShaderResourceView* )ERROR_RESOURCE_VALUE;*/
		}
	}
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createVertexBuffer(SDKMESH_VERTEX_BUFFER_HEADER *pHeader, void *pVertices)
{
    HRESULT hr = S_OK;
    pHeader->DataOffset = 0;

	//Vertex Buffer
	//pHeader->pVB11 = make_unique<VertexBuffer>(m_pDev11);
	pHeader->pVertexBuffer = new VertexBuffer(m_device);
	pHeader->pVertexBuffer->Create(static_cast<uint32_t>(pHeader->SizeBytes),
		static_cast<uint32_t>(pHeader->StrideBytes));

	m_resourceUploads.push_back(Resource());
	pHeader->pVertexBuffer->Upload(m_commandList, m_resourceUploads.back(), pVertices);

    return hr;
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createIndexBuffer(SDKMESH_INDEX_BUFFER_HEADER *pHeader, void *pIndices)
{
    HRESULT hr = S_OK;
    pHeader->DataOffset = 0;

    //Index Buffer
	//pHeader->pIB11 = make_unique<IndexBuffer>(m_pDev11);
	pHeader->pIndexBuffer = new IndexBuffer(m_device);
	pHeader->pIndexBuffer->Create(static_cast<uint32_t>(pHeader->SizeBytes));

	m_resourceUploads.push_back(Resource());
	pHeader->pIndexBuffer->Upload(m_commandList, m_resourceUploads.back(), pIndices);

    return hr;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT SDKMesh::createFromFile(const Device &device, const wchar_t *szFileName)
{
    HRESULT hr = S_OK;

    // Find the path for the file
	wcsncpy_s(m_strPathW, MAX_PATH, szFileName, wcslen(szFileName));
    //V_RETURN(DXUTFindDXSDKMediaFileCch(m_strPathW, sizeof(m_strPathW) / sizeof(WCHAR), szFileName));

    // Open the file
    m_filehandle = CreateFile(m_strPathW, FILE_READ_DATA, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (INVALID_HANDLE_VALUE == m_filehandle)
		return E_FAIL;//DXUTERR_MEDIANOTFOUND;

    // Change the path to just the directory
	const auto pLastBSlash = wcsrchr(&m_strPathW[0], L'\\');
    if (pLastBSlash) *(pLastBSlash + 1) = L'\0';
    else m_strPathW[0] = L'\0';

    WideCharToMultiByte(CP_ACP, 0, m_strPathW, -1, m_strPath, MAX_PATH, nullptr, FALSE);

    // Get the file size
    LARGE_INTEGER fileSize;
    GetFileSizeEx(m_filehandle, &fileSize);
    const auto cBytes = fileSize.LowPart;

    // Allocate memory
    m_pStaticMeshData = new (std::nothrow) uint8_t[cBytes];
    if (!m_pStaticMeshData)
    {
        CloseHandle( m_filehandle );
        return E_OUTOFMEMORY;
    }

    // Read in the file
    DWORD dwBytesRead;
    if (!ReadFile( m_filehandle, m_pStaticMeshData, cBytes, &dwBytesRead, nullptr))
		hr = E_FAIL;

    CloseHandle( m_filehandle );

    if( SUCCEEDED( hr ) )
    {
        hr = createFromMemory(device, m_pStaticMeshData, cBytes, false);

        if (FAILED(hr)) delete [] m_pStaticMeshData;
    }

    return hr;
}

_Use_decl_annotations_
HRESULT SDKMesh::createFromMemory(const Device &device, uint8_t *pData, size_t DataBytes, bool bCopyStatic)
{
    XMFLOAT3 lower; 
    XMFLOAT3 upper; 
    
    m_device = device;

    if (DataBytes < sizeof(SDKMESH_HEADER)) return E_FAIL;

    // Set outstanding resources to zero
    m_numOutstandingResources = 0;

    if (bCopyStatic)
    {
        auto pHeader = reinterpret_cast<SDKMESH_HEADER*>( pData );

        SIZE_T StaticSize = static_cast<SIZE_T>(pHeader->HeaderSize + pHeader->NonBufferDataSize);
        if (DataBytes < StaticSize) return E_FAIL;

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

    // Create VBs
    m_ppVertices = new (std::nothrow) uint8_t*[m_pMeshHeader->NumVertexBuffers];
    if (!m_ppVertices) return E_OUTOFMEMORY;

    for(auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
    {
		uint8_t *pVertices = nullptr;
        pVertices = reinterpret_cast<uint8_t*>(pBufferData + (m_pVertexBufferArray[i].DataOffset - BufferDataStart));

        if (device) createVertexBuffer(&m_pVertexBufferArray[i], pVertices);

        m_ppVertices[i] = pVertices;
    }

    // Create IBs
    m_ppIndices = new (std::nothrow) uint8_t*[m_pMeshHeader->NumIndexBuffers];
    if (!m_ppIndices) return E_OUTOFMEMORY;

    for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
    {
		uint8_t *pIndices = nullptr;
        pIndices = reinterpret_cast<uint8_t*>(pBufferData + ( m_pIndexBufferArray[i].DataOffset - BufferDataStart));

		if (device) createIndexBuffer(&m_pIndexBufferArray[i], pIndices);

        m_ppIndices[i] = pIndices;
    }

    // Load Materials
    if (device) loadMaterials(m_pMaterialArray, m_pMeshHeader->NumMaterials);

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

            /*if (bAdjacent)
            {
                IndexCount *= 2;
                IndexStart *= 2;
            }*/

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

    return S_OK;
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
		transformBindPoseFrame( m_pFrameArray[iFrame].SiblingFrame, parentWorld);

    // Transform our children
    if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME)
		transformBindPoseFrame( m_pFrameArray[iFrame].ChildFrame, mLocalWorld);
}

//--------------------------------------------------------------------------------------
// transform frame using a recursive traversal
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void SDKMesh::transformFrame(uint32_t iFrame, CXMMATRIX parentWorld, double fTime)
{
    // Get the tick data
    XMMATRIX mLocalTransform;

    const auto iTick = GetAnimationKeyFromTime(fTime);

    if (INVALID_ANIMATION_DATA != m_pFrameArray[iFrame].AnimationDataIndex)
    {
        const auto pFrameData = &m_pAnimationFrameData[m_pFrameArray[iFrame].AnimationDataIndex];
        const auto pData = &pFrameData->pAnimationData[iTick];

        // turn it into a matrix (Ignore scaling for now)
        const auto parentPos = pData->Translation;
		const auto mTranslate = XMMatrixTranslation(parentPos.x, parentPos.y, parentPos.z);

		auto quat = XMVectorSet(pData->Orientation.x, pData->Orientation.y, pData->Orientation.z, pData->Orientation.w);
        if (XMVector4Equal(quat, g_XMZero)) quat = XMQuaternionIdentity();
        quat = XMQuaternionNormalize(quat);
        const auto mQuat = XMMatrixRotationQuaternion(quat);
        mLocalTransform = (mQuat * mTranslate);
    }
    else mLocalTransform = XMLoadFloat4x4(&m_pFrameArray[iFrame].Matrix);

    // Transform ourselves
    const auto mLocalWorld = XMMatrixMultiply(mLocalTransform, parentWorld);
    DirectX::XMStoreFloat4x4(&m_pTransformedFrameMatrices[iFrame], mLocalWorld);
	DirectX::XMStoreFloat4x4(&m_pWorldPoseFrameMatrices[iFrame], mLocalWorld);

    // Transform our siblings
    if (m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME)
		transformFrame( m_pFrameArray[iFrame].SiblingFrame, parentWorld, fTime);

    // Transform our children
    if (m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME)
		transformFrame( m_pFrameArray[iFrame].ChildFrame, mLocalWorld, fTime);
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

#if 0
#define MAX_D3D11_VERTEX_STREAMS D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderMesh( UINT iMesh,
                               bool bAdjacent,
                               ID3D11DeviceContext* pd3dDeviceContext,
                               UINT iDiffuseSlot,
                               UINT iNormalSlot,
                               UINT iSpecularSlot )
{
    if( 0 < GetOutstandingBufferResources() )
        return;

    auto pMesh = &m_pMeshArray[iMesh];

    UINT Strides[MAX_D3D11_VERTEX_STREAMS];
    UINT Offsets[MAX_D3D11_VERTEX_STREAMS];
    ID3D11Buffer* pVB[MAX_D3D11_VERTEX_STREAMS];

    if( pMesh->NumVertexBuffers > MAX_D3D11_VERTEX_STREAMS )
        return;

    for( UINT64 i = 0; i < pMesh->NumVertexBuffers; i++ )
    {
        pVB[i] = m_pVertexBufferArray[ pMesh->VertexBuffers[i] ].pVB11;
        Strides[i] = ( UINT )m_pVertexBufferArray[ pMesh->VertexBuffers[i] ].StrideBytes;
        Offsets[i] = 0;
    }

    SDKMESH_INDEX_BUFFER_HEADER* pIndexBufferArray;
    if( bAdjacent )
        pIndexBufferArray = m_pAdjacencyIndexBufferArray;
    else
        pIndexBufferArray = m_pIndexBufferArray;

    auto pIB = pIndexBufferArray[ pMesh->IndexBuffer ].pIB11;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_R16_UINT;
    switch( pIndexBufferArray[ pMesh->IndexBuffer ].IndexType )
    {
    case IT_16BIT:
        ibFormat = DXGI_FORMAT_R16_UINT;
        break;
    case IT_32BIT:
        ibFormat = DXGI_FORMAT_R32_UINT;
        break;
    };

    pd3dDeviceContext->IASetVertexBuffers( 0, pMesh->NumVertexBuffers, pVB, Strides, Offsets );
    pd3dDeviceContext->IASetIndexBuffer( pIB, ibFormat, 0 );

    SDKMESH_SUBSET* pSubset = nullptr;
    SDKMESH_MATERIAL* pMat = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY PrimType;

    for( UINT subset = 0; subset < pMesh->NumSubsets; subset++ )
    {
        pSubset = &m_pSubsetArray[ pMesh->pSubsets[subset] ];

        PrimType = GetPrimitiveType11( ( SDKMESH_PRIMITIVE_TYPE )pSubset->PrimitiveType );
        if( bAdjacent )
        {
            switch( PrimType )
            {
            case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
                PrimType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
                break;
            case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
                PrimType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
                break;
            case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
                PrimType = D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
                break;
            case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
                PrimType = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
                break;
            }
        }

        pd3dDeviceContext->IASetPrimitiveTopology( PrimType );

        pMat = &m_pMaterialArray[ pSubset->MaterialID ];
        if( iDiffuseSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pDiffuseRV11 ) )
            pd3dDeviceContext->PSSetShaderResources( iDiffuseSlot, 1, &pMat->pDiffuseRV11 );
        if( iNormalSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pNormalRV11 ) )
            pd3dDeviceContext->PSSetShaderResources( iNormalSlot, 1, &pMat->pNormalRV11 );
        if( iSpecularSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pSpecularRV11 ) )
            pd3dDeviceContext->PSSetShaderResources( iSpecularSlot, 1, &pMat->pSpecularRV11 );

        UINT IndexCount = ( UINT )pSubset->IndexCount;
        UINT IndexStart = ( UINT )pSubset->IndexStart;
        UINT VertexStart = ( UINT )pSubset->VertexStart;
        if( bAdjacent )
        {
            IndexCount *= 2;
            IndexStart *= 2;
        }

        pd3dDeviceContext->DrawIndexed( IndexCount, IndexStart, VertexStart );
    }
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderFrame( UINT iFrame,
                                bool bAdjacent,
                                ID3D11DeviceContext* pd3dDeviceContext,
                                UINT iDiffuseSlot,
                                UINT iNormalSlot,
                                UINT iSpecularSlot )
{
    if( !m_pStaticMeshData || !m_pFrameArray )
        return;

    if( m_pFrameArray[iFrame].Mesh != INVALID_MESH )
    {
        RenderMesh( m_pFrameArray[iFrame].Mesh,
                    bAdjacent,
                    pd3dDeviceContext,
                    iDiffuseSlot,
                    iNormalSlot,
                    iSpecularSlot );
    }

    // Render our children
    if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME )
        RenderFrame( m_pFrameArray[iFrame].ChildFrame, bAdjacent, pd3dDeviceContext, iDiffuseSlot, 
                     iNormalSlot, iSpecularSlot );

    // Render our siblings
    if( m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME )
        RenderFrame( m_pFrameArray[iFrame].SiblingFrame, bAdjacent, pd3dDeviceContext, iDiffuseSlot, 
                     iNormalSlot, iSpecularSlot );
}
#endif
