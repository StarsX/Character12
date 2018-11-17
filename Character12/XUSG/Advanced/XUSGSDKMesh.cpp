//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "XUSGSDKMesh.h"
#include "XUSGDDSLoader.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

//--------------------------------------------------------------------------------------
SDKMesh::SDKMesh() :
	m_device(nullptr),
	m_numOutstandingResources(0),
	m_isLoading(false),
	m_pStaticMeshData(nullptr),
	m_heapData(0),
	m_animation(0),
	m_vertices(0),
	m_indices(0),
	m_filePathW(),
	m_filePath(),
	m_pMeshHeader(nullptr),
	m_pVertexBufferArray(nullptr),
	m_pIndexBufferArray(nullptr),
	m_pMeshArray(nullptr),
	m_pSubsetArray(nullptr),
	m_pFrameArray(nullptr),
	m_pMaterialArray(nullptr),
	m_pAdjIndexBufferArray(nullptr),
	m_pAnimationHeader(nullptr),
	m_pAnimationFrameData(nullptr),
	m_bindPoseFrameMatrices(0),
	m_transformedFrameMatrices(0),
	m_worldPoseFrameMatrices(0)
{
}

//--------------------------------------------------------------------------------------
SDKMesh::~SDKMesh()
{
}

//--------------------------------------------------------------------------------------
bool SDKMesh::Create(const Device &device, const wchar_t *fileName,
	const TextureCache &textureCache)
{
	return createFromFile(device, fileName, textureCache);
}

bool SDKMesh::Create(const Device &device, uint8_t *pData,
	const TextureCache &textureCache, size_t dataBytes, bool copyStatic)
{
	return createFromMemory(device, pData, textureCache, dataBytes, copyStatic);
}

bool SDKMesh::LoadAnimation(const wchar_t *fileName)
{
	wchar_t filePath[MAX_PATH];

	// Find the path for the file
	wcsncpy_s(filePath, MAX_PATH, fileName, wcslen(fileName));
	//V_RETURN(DXUTFindDXSDKMediaFileCch(filePath, MAX_PATH, fileName));

	// Open the file
	ifstream fileStream(filePath, ios::in | ios::binary);
	F_RETURN(!fileStream, cerr, MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903), false);

	// Read header
	SDKAnimationFileHeader fileheader;
	F_RETURN(!fileStream.read(reinterpret_cast<char*>(&fileheader), sizeof(SDKAnimationFileHeader)),
		fileStream.close(); cerr, GetLastError(), false);

	// Allocate
	m_animation.resize(static_cast<size_t>(sizeof(SDKAnimationFileHeader) + fileheader.AnimationDataSize));
	
	// Read it all in
	F_RETURN(!fileStream.seekg(0), fileStream.close(); cerr, GetLastError(), false);

	const auto cBytes = static_cast<streamsize>(sizeof(SDKAnimationFileHeader) + fileheader.AnimationDataSize);
	F_RETURN(!fileStream.read(reinterpret_cast<char*>(m_animation.data()), cBytes),
		fileStream.close(); cerr, GetLastError(), false);

	fileStream.close();

	// pointer fixup
	m_pAnimationHeader = reinterpret_cast<SDKAnimationFileHeader*>(m_animation.data());
	m_pAnimationFrameData = reinterpret_cast<SDKAnimationFrameData*>(m_animation.data() + m_pAnimationHeader->AnimationDataOffset);

	const auto BaseOffset = sizeof(SDKAnimationFileHeader);

	for (auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
	{
		m_pAnimationFrameData[i].pAnimationData = reinterpret_cast<SDKAnimationData*>
			(m_animation.data() + m_pAnimationFrameData[i].DataOffset + BaseOffset);

		const auto pFrame = FindFrame(m_pAnimationFrameData[i].FrameName);

		if (pFrame) pFrame->AnimationDataIndex = i;
	}

	return true;
}

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

				m_pMaterialArray[m].AlphaModeAlbedo = 0;
				m_pMaterialArray[m].AlphaModeNormal = 0;
				m_pMaterialArray[m].AlphaModeSpecular = 0;
			}
		}
	}

	m_pStaticMeshData = nullptr;
	m_heapData.clear();
	m_animation.clear();
	m_bindPoseFrameMatrices.clear();
	m_transformedFrameMatrices.clear();
	m_worldPoseFrameMatrices.clear();

	m_vertices.clear();
	m_indices.clear();

	m_pMeshHeader = nullptr;
	m_pVertexBufferArray = nullptr;
	m_pIndexBufferArray = nullptr;
	m_pMeshArray = nullptr;
	m_pSubsetArray = nullptr;
	m_pFrameArray = nullptr;
	m_pMaterialArray = nullptr;
	m_pAdjIndexBufferArray = nullptr;

	m_pAnimationHeader = nullptr;
	m_pAnimationFrameData = nullptr;
}

//--------------------------------------------------------------------------------------
// transform the bind pose
//--------------------------------------------------------------------------------------
void SDKMesh::TransformBindPose(CXMMATRIX world)
{
	transformBindPoseFrame(0, world);
}

//--------------------------------------------------------------------------------------
// transform the mesh frames according to the animation for time
//--------------------------------------------------------------------------------------
void SDKMesh::TransformMesh(CXMMATRIX world, double time)
{
	if (!m_pAnimationHeader || FTT_RELATIVE == m_pAnimationHeader->FrameTransformType)
	{
		transformFrame(0, world, time);

		// For each frame, move the transform to the bind pose, then
		// move it to the final position
		for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		{
			const auto invBindPose = XMMatrixInverse(nullptr, XMLoadFloat4x4(&m_bindPoseFrameMatrices[i]));
			const auto final = invBindPose * XMLoadFloat4x4(&m_transformedFrameMatrices[i]);
			XMStoreFloat4x4(&m_transformedFrameMatrices[i], final);
		}
	}
	else if (FTT_ABSOLUTE == m_pAnimationHeader->FrameTransformType)
		for (auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
			transformFrameAbsolute(i, time);
}

//--------------------------------------------------------------------------------------
PrimitiveTopology SDKMesh::GetPrimitiveType(SDKMeshPrimitiveType primType)
{
	PrimitiveTopology retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	switch (primType)
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

Format SDKMesh::GetIBFormat(uint32_t mesh) const
{
	switch (m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].IndexType)
	{
		case IT_16BIT:
			return DXGI_FORMAT_R16_UINT;
		case IT_32BIT:
			return DXGI_FORMAT_R32_UINT;
	};

	return DXGI_FORMAT_R16_UINT;
}

SDKMeshIndexType SDKMesh::GetIndexType(uint32_t mesh) const
{
	return static_cast<SDKMeshIndexType>(m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].IndexType);
}

Descriptor SDKMesh::GetVertexBufferSRV(uint32_t mesh, uint32_t i) const
{
	return m_vertexBuffer.GetSRV(m_pMeshArray[mesh].VertexBuffers[i]);
}

VertexBufferView SDKMesh::GetVertexBufferView(uint32_t mesh, uint32_t i) const
{
	return m_vertexBuffer.GetVBV(m_pMeshArray[mesh].VertexBuffers[i]);
}

IndexBufferView SDKMesh::GetIndexBufferView(uint32_t mesh) const
{
	return m_indexBuffer.GetIBV(m_pMeshArray[mesh].IndexBuffer);
}

IndexBufferView SDKMesh::GetAdjIndexBufferView(uint32_t mesh) const
{
	return m_adjIndexBuffer.GetIBV(m_pMeshArray[mesh].IndexBuffer);
}

Descriptor SDKMesh::GetVertexBufferSRVAt(uint32_t vb) const
{
	return m_vertexBuffer.GetSRV(vb);
}

VertexBufferView SDKMesh::GetVertexBufferViewAt(uint32_t vb) const
{
	return m_vertexBuffer.GetVBV(vb);
}

IndexBufferView SDKMesh::GetIndexBufferViewAt(uint32_t ib) const
{
	return m_indexBuffer.GetIBV(ib);
}

//--------------------------------------------------------------------------------------
const char *SDKMesh::GetMeshPathA() const
{
	return m_filePath.c_str();
}

const wchar_t *SDKMesh::GetMeshPathW() const
{
	return m_filePathW.c_str();
}

uint32_t SDKMesh::GetNumMeshes() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMeshes : 0;
}

uint32_t SDKMesh::GetNumMaterials() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMaterials : 0;
}

uint32_t SDKMesh::GetNumVertexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumVertexBuffers : 0;
}

uint32_t SDKMesh::GetNumIndexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumIndexBuffers : 0;
}

uint8_t *SDKMesh::GetRawVerticesAt(uint32_t vb) const
{
	return m_vertices[vb];
}

uint8_t *SDKMesh::GetRawIndicesAt(uint32_t ib) const
{
	return m_indices[ib];
}

SDKMeshMaterial *SDKMesh::GetMaterial(uint32_t material) const
{
	return &m_pMaterialArray[material];
}

SDKMeshData *SDKMesh::GetMesh(uint32_t mesh) const
{
	return &m_pMeshArray[mesh];
}

uint32_t SDKMesh::GetNumSubsets(uint32_t mesh) const
{
	return m_pMeshArray[mesh].NumSubsets;
}

uint32_t SDKMesh::GetNumSubsets(uint32_t mesh, SubsetFlags materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return static_cast<uint32_t>(m_classifiedSubsets[materialType - 1][mesh].size());
}

SDKMeshSubset *SDKMesh::GetSubset(uint32_t mesh, uint32_t subset) const
{
	return &m_pSubsetArray[m_pMeshArray[mesh].pSubsets[subset]];
}

SDKMeshSubset *SDKMesh::GetSubset(uint32_t mesh, uint32_t subset, SubsetFlags materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return &m_pSubsetArray[m_classifiedSubsets[materialType - 1][mesh][subset]];
}

uint32_t SDKMesh::GetVertexStride(uint32_t mesh, uint32_t i) const
{
	return static_cast<uint32_t>(m_pVertexBufferArray[m_pMeshArray[mesh].VertexBuffers[i]].StrideBytes);
}

uint32_t SDKMesh::GetNumFrames() const
{
	return m_pMeshHeader->NumFrames;
}

SDKMeshFrame *SDKMesh::GetFrame(uint32_t frame) const
{
	assert(frame < m_pMeshHeader->NumFrames);

	return &m_pFrameArray[frame];
}

SDKMeshFrame *SDKMesh::FindFrame(const char *name) const
{
	const auto i = FindFrameIndex(name);

	return i == INVALID_FRAME ? nullptr : &m_pFrameArray[i];
}

uint32_t SDKMesh::FindFrameIndex(const char *name) const
{
	for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		if (_stricmp(m_pFrameArray[i].Name, name) == 0)
			return i;

	return INVALID_FRAME;
}

uint64_t SDKMesh::GetNumVertices(uint32_t mesh, uint32_t i) const
{
	return m_pVertexBufferArray[m_pMeshArray[mesh].VertexBuffers[i]].NumVertices;
}

uint64_t SDKMesh::GetNumIndices(uint32_t mesh) const
{
	return m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].NumIndices;
}

XMVECTOR SDKMesh::GetMeshBBoxCenter(uint32_t mesh) const
{
	return XMLoadFloat3(&m_pMeshArray[mesh].BoundingBoxCenter);
}

XMVECTOR SDKMesh::GetMeshBBoxExtents(uint32_t mesh) const
{
	return XMLoadFloat3(&m_pMeshArray[mesh].BoundingBoxExtents);
}

uint32_t SDKMesh::GetOutstandingResources() const
{
	auto outstandingResources = 0u;
	if (!m_pMeshHeader) return 1;

	outstandingResources += GetOutstandingBufferResources();

	if (m_device)
	{
		for (auto i = 0u; i < m_pMeshHeader->NumMaterials; ++i)
		{
			if (m_pMaterialArray[i].AlbedoTexture[0] != 0)
				if (!m_pMaterialArray[i].pAlbedo && !IsErrorResource(m_pMaterialArray[i].Albedo64))
					++outstandingResources;

			if (m_pMaterialArray[i].NormalTexture[0] != 0)
				if (!m_pMaterialArray[i].pNormal && !IsErrorResource(m_pMaterialArray[i].Normal64))
					++outstandingResources;

			if (m_pMaterialArray[i].SpecularTexture[0] != 0)
				if (!m_pMaterialArray[i].pSpecular && !IsErrorResource(m_pMaterialArray[i].Specular64))
					++outstandingResources;
		}
	}

	return outstandingResources;
}

uint32_t SDKMesh::GetOutstandingBufferResources() const
{
	uint32_t outstandingResources = 0;
	if (!m_pMeshHeader) return 1;

	return outstandingResources;
}

bool SDKMesh::CheckLoadDone()
{
	if (0 == GetOutstandingResources())
	{
		m_isLoading = false;

		return true;
	}

	return false;
}

bool SDKMesh::IsLoaded() const
{
	if( m_pStaticMeshData && !m_isLoading )
	{
		return true;
	}

	return false;
}

bool SDKMesh::IsLoading() const
{
	return m_isLoading;
}

void SDKMesh::SetLoading(bool loading)
{
	m_isLoading = loading;
}

bool SDKMesh::HadLoadingError() const
{
	return false;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh::GetNumInfluences(uint32_t mesh) const
{
	return m_pMeshArray[mesh].NumFrameInfluences;
}

XMMATRIX SDKMesh::GetMeshInfluenceMatrix(uint32_t mesh, uint32_t influence) const
{
	const auto frame = m_pMeshArray[mesh].pFrameInfluences[influence];

	return XMLoadFloat4x4(&m_transformedFrameMatrices[frame]);
}

XMMATRIX SDKMesh::GetWorldMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_worldPoseFrameMatrices[frameIndex]);
}

XMMATRIX SDKMesh::GetInfluenceMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_transformedFrameMatrices[frameIndex]);
}

XMMATRIX SDKMesh::GetBindMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_bindPoseFrameMatrices[frameIndex]);
}

uint32_t SDKMesh::GetAnimationKeyFromTime(double time) const
{
	if (!m_pAnimationHeader) return 0;

	auto tick = static_cast<uint32_t>(m_pAnimationHeader->AnimationFPS * time);

	tick = tick % (m_pAnimationHeader->NumAnimationKeys - 1);

	return ++tick;
}

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
void SDKMesh::loadMaterials(const GraphicsCommandList &commandList, SDKMeshMaterial *pMaterials,
	uint32_t numMaterials, vector<Resource> &uploaders)
{
	string filePath;
	wstring filePathW;
	DDS::Loader textureLoader;
	DDS::AlphaMode alphaMode;

	for (auto m = 0u; m < numMaterials; ++m)
	{
		pMaterials[m].pAlbedo = nullptr;
		pMaterials[m].pNormal = nullptr;
		pMaterials[m].pSpecular = nullptr;
		pMaterials[m].AlphaModeAlbedo = 0;
		pMaterials[m].AlphaModeNormal = 0;
		pMaterials[m].AlphaModeSpecular = 0;

		// load textures
		if (pMaterials[m].AlbedoTexture[0] != 0)
		{
			filePath = m_filePath + pMaterials[m].AlbedoTexture;
			const auto textureIter = m_textureCache->find(filePath);

			if (textureIter != m_textureCache->end())
			{
				pMaterials[m].pAlbedo = textureIter->second.Texture.get();
				pMaterials[m].AlphaModeAlbedo = textureIter->second.AlphaMode;
			}
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				filePathW.assign(filePath.begin(), filePath.end());
				if (!textureLoader.CreateTextureFromFile(m_device, commandList, filePathW.c_str(),
					8192, true, texture, uploaders.back(), &alphaMode))
					pMaterials[m].Albedo64 = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pAlbedo = texture.get();
					pMaterials[m].AlphaModeAlbedo = alphaMode;
					(*m_textureCache)[filePath] = { texture, alphaMode };
				}
			}
		}
		if (pMaterials[m].NormalTexture[0] != 0)
		{
			filePath = m_filePath + pMaterials[m].NormalTexture;
			const auto textureIter = m_textureCache->find(filePath);

			if (textureIter != m_textureCache->end())
			{
				pMaterials[m].pNormal = textureIter->second.Texture.get();
				pMaterials[m].AlphaModeNormal = textureIter->second.AlphaMode;
			}
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				filePathW.assign(filePath.begin(), filePath.end());
				if (!textureLoader.CreateTextureFromFile(m_device, commandList, filePathW.c_str(),
					8192, false, texture, uploaders.back(), &alphaMode))
					pMaterials[m].Normal64 = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pNormal = texture.get();
					pMaterials[m].AlphaModeNormal = alphaMode;
					(*m_textureCache)[filePath] = { texture, alphaMode };
				}
			}
		}
		if (pMaterials[m].SpecularTexture[0] != 0)
		{
			filePath = m_filePath + pMaterials[m].SpecularTexture;
			const auto textureIter = m_textureCache->find(filePath);

			if (textureIter != m_textureCache->end())
			{
				pMaterials[m].pSpecular = textureIter->second.Texture.get();
				pMaterials[m].AlphaModeSpecular = textureIter->second.AlphaMode;
			}
			else
			{
				shared_ptr<ResourceBase> texture;
				uploaders.push_back(Resource());

				filePathW.assign(filePath.begin(), filePath.end());
				if (!textureLoader.CreateTextureFromFile(m_device, commandList, filePathW.c_str(),
					8192, false, texture, uploaders.back()))
					pMaterials[m].Specular64 = ERROR_RESOURCE_VALUE;
				else
				{
					pMaterials[m].pSpecular = texture.get();
					pMaterials[m].AlphaModeNormal = alphaMode;
					(*m_textureCache)[filePath] = { texture, alphaMode };
				}
			}
		}
	}
}

bool SDKMesh::createVertexBuffer(const GraphicsCommandList &commandList, std::vector<Resource> &uploaders)
{
	// Vertex buffer info
	auto numVertices = 0u;
	const auto stride = static_cast<uint32_t>(m_pVertexBufferArray->StrideBytes);
	vector<uint32_t> firstVertices(m_pMeshHeader->NumVertexBuffers);

	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		firstVertices[i] = numVertices;
		numVertices += static_cast<uint32_t>(m_pVertexBufferArray[i].SizeBytes) / stride;
	}

	// Create a vertex Buffer
	N_RETURN(m_vertexBuffer.Create(m_device, numVertices, stride, D3D12_RESOURCE_FLAG_NONE,
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
		m_pMeshHeader->NumVertexBuffers, firstVertices.data(),
		m_pMeshHeader->NumVertexBuffers, firstVertices.data()), false);

	// Copy vertices into one buffer
	auto offset = 0u;
	vector<uint8_t> bufferData(stride * numVertices);

	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		const auto sizeBytes = static_cast<uint32_t>(m_pVertexBufferArray[i].SizeBytes);
		memcpy(&bufferData[offset], m_vertices[i], sizeBytes);
		offset += sizeBytes;
	}

	// Upload vertices
	uploaders.push_back(Resource());
	
	return m_vertexBuffer.Upload(commandList, uploaders.back(), bufferData.data(),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
}

bool SDKMesh::createIndexBuffer(const GraphicsCommandList &commandList, std::vector<Resource> &uploaders)
{
	// Index buffer info
	auto byteWidth = 0u;
	vector<uint32_t> offsets(m_pMeshHeader->NumIndexBuffers);

	for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
	{
		offsets[i] = byteWidth;
		byteWidth += static_cast<uint32_t>(m_pIndexBufferArray[i].SizeBytes);
	}

	// Create a vertex Buffer
	N_RETURN(m_indexBuffer.Create(m_device, byteWidth, m_pIndexBufferArray->IndexType == IT_32BIT ?
		DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
		m_pMeshHeader->NumIndexBuffers, offsets.data()), false);
	
	// Copy indices into one buffer
	auto offset = 0u;
	vector<uint8_t> bufferData(byteWidth);

	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		const auto sizeBytes = static_cast<uint32_t>(m_pIndexBufferArray[i].SizeBytes);
		memcpy(&bufferData[offset], m_indices[i], sizeBytes);
		offset += sizeBytes;
	}

	// Upload indices
	uploaders.push_back(Resource());

	return m_indexBuffer.Upload(commandList, uploaders.back(), bufferData.data(),
		D3D12_RESOURCE_STATE_INDEX_BUFFER);
}

//--------------------------------------------------------------------------------------
bool SDKMesh::createFromFile(const Device &device, const wchar_t *fileName,
	const TextureCache &textureCache)
{
	// Find the path for the file
	m_filePathW = fileName;
	//V_RETURN(DXUTFindDXSDKMediaFileCch(m_filePathW, sizeof(m_strPathW) / sizeof(WCHAR), fileName));

	// Open the file
	ifstream fileStream(m_filePathW, ios::in | ios::binary);
	F_RETURN(!fileStream, cerr, MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903), false);

	// Change the path to just the directory
	const auto found = m_filePathW.find_last_of(L"/\\");
	m_filePathW = m_filePathW.substr(0, found + 1);
	m_filePath.assign(m_filePathW.begin(), m_filePathW.end());

	// Get the file size
	F_RETURN(!fileStream.seekg(0, fileStream.end), fileStream.close(); cerr, E_FAIL, false);
	const auto cBytes = static_cast<uint32_t>(fileStream.tellg());
	F_RETURN(!fileStream.seekg(0), fileStream.close(); cerr, E_FAIL, false);

	// Allocate memory
	m_heapData.resize(cBytes);
	m_pStaticMeshData = m_heapData.data();

	// Read in the file
	F_RETURN(!fileStream.read(reinterpret_cast<char*>(m_pStaticMeshData), cBytes), cerr, E_FAIL, false);

	fileStream.close();

	return createFromMemory(device, m_pStaticMeshData, textureCache, cBytes, false);
}

bool SDKMesh::createFromMemory(const Device &device, uint8_t *pData,
	const TextureCache &textureCache, size_t dataBytes, bool copyStatic)
{
	XMFLOAT3 lower; 
	XMFLOAT3 upper; 
	
	m_device = device;
	ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	GraphicsCommandList commandList = nullptr;
	if (device)
	{
		V_RETURN(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&commandAllocator)), cerr, false);
		V_RETURN(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)), cerr, false);
	}

	F_RETURN(dataBytes < sizeof(SDKMeshHeader), cerr, E_FAIL, false);

	// Set outstanding resources to zero
	m_numOutstandingResources = 0;

	if (copyStatic)
	{
		const auto pHeader = reinterpret_cast<SDKMeshHeader*>(pData);
		const auto StaticSize = static_cast<SIZE_T>(pHeader->HeaderSize + pHeader->NonBufferDataSize);
		F_RETURN(dataBytes < StaticSize, cerr, E_FAIL, false);

		m_heapData.resize(StaticSize);
		m_pStaticMeshData = m_heapData.data();

		memcpy(m_pStaticMeshData, pData, StaticSize);
	}
	else m_pStaticMeshData = pData;

	// Pointer fixup
	m_pMeshHeader = reinterpret_cast<SDKMeshHeader*>(m_pStaticMeshData);

	m_pVertexBufferArray = reinterpret_cast<SDKMeshVertexBufferHeader*>(m_pStaticMeshData + m_pMeshHeader->VertexStreamHeadersOffset);
	m_pIndexBufferArray = reinterpret_cast<SDKMeshIndexBufferHeader*>(m_pStaticMeshData + m_pMeshHeader->IndexStreamHeadersOffset);
	m_pMeshArray = reinterpret_cast<SDKMeshData*>(m_pStaticMeshData + m_pMeshHeader->MeshDataOffset);
	m_pSubsetArray = reinterpret_cast<SDKMeshSubset*>(m_pStaticMeshData + m_pMeshHeader->SubsetDataOffset);
	m_pFrameArray = reinterpret_cast<SDKMeshFrame*>(m_pStaticMeshData + m_pMeshHeader->FrameDataOffset);
	m_pMaterialArray = reinterpret_cast<SDKMeshMaterial*>(m_pStaticMeshData + m_pMeshHeader->MaterialDataOffset);

	// Setup subsets
	for(auto i = 0u; i < m_pMeshHeader->NumMeshes; ++i)
	{
		m_pMeshArray[i].pSubsets = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].SubsetOffset);
		m_pMeshArray[i].pFrameInfluences = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].FrameInfluenceOffset);
	}

	// error condition
	F_RETURN(m_pMeshHeader->Version != SDKMESH_FILE_VERSION, cerr, E_NOINTERFACE, false);

	// Create VBs
	m_vertices.resize(m_pMeshHeader->NumVertexBuffers);
	for(auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
		m_vertices[i] = reinterpret_cast<uint8_t*>(pData + m_pVertexBufferArray[i].DataOffset);

	// Create IBs
	m_indices.resize(m_pMeshHeader->NumIndexBuffers);
	for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
		m_indices[i] = reinterpret_cast<uint8_t*>(pData + m_pIndexBufferArray[i].DataOffset);

	// Uploader buffers
	vector<Resource> uploaders;

	// Load Materials
	m_textureCache = textureCache;
	if (commandList) loadMaterials(commandList, m_pMaterialArray, m_pMeshHeader->NumMaterials, uploaders);

	// Create a place to store our bind pose frame matrices
	m_bindPoseFrameMatrices.resize(m_pMeshHeader->NumFrames);

	// Create a place to store our transformed frame matrices
	m_transformedFrameMatrices.resize(m_pMeshHeader->NumFrames);
	m_worldPoseFrameMatrices.resize(m_pMeshHeader->NumFrames);

	SDKMeshSubset* pSubset = nullptr;
	PrimitiveTopology primType;

	// update bounding volume
	SDKMeshData *currentMesh = m_pMeshArray;
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

			primType = GetPrimitiveType(static_cast<SDKMeshPrimitiveType>(pSubset->PrimitiveType));
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
			const auto indices = reinterpret_cast<uint32_t*>(m_indices[currentMesh->IndexBuffer]);
			const auto vertices = reinterpret_cast<float*>(m_vertices[currentMesh->VertexBuffers[0]]);
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

	//Create vertex Buffer and index buffer
	N_RETURN(createVertexBuffer(commandList, uploaders), false);
	N_RETURN(createIndexBuffer(commandList, uploaders), false);

	// Execute commands
	return executeCommandList(commandList);
}

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
			if (pMaterial && pMaterial->pAlbedo && !IsErrorResource(pMaterial->Albedo64))
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

bool SDKMesh::executeCommandList(const GraphicsCommandList &commandList)
{
	if (commandList)
	{
		// Describe and create the command queue.
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
		V_RETURN(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)), cerr, false);

		// Close the command list and execute it to begin the initial GPU setup.
		V_RETURN(commandList->Close(), cerr, false);
		ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Create synchronization objects and wait until assets have been uploaded to the GPU.
		{
			HANDLE fenceEvent;
			ComPtr<ID3D12Fence> fence;
			uint64_t fenceValue = 0;

			V_RETURN(m_device->CreateFence(fenceValue++, D3D12_FENCE_FLAG_NONE,
				IID_PPV_ARGS(&fence)), cerr, false);

			// Create an event handle to use for frame synchronization.
			fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			F_RETURN(!fenceEvent, cerr, GetLastError(), false);

			// Wait for the command list to execute; we are reusing the same command 
			// list in our main loop but for now, we just want to wait for setup to 
			// complete before continuing.
			// Schedule a Signal command in the queue.
			V_RETURN(commandQueue->Signal(fence.Get(), fenceValue), cerr, false);

			// Wait until the fence has been processed, and increment the fence value for the current frame.
			V_RETURN(fence->SetEventOnCompletion(fenceValue++, fenceEvent), cerr, false);
			WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE);
		}
	}

	return true;
}

//--------------------------------------------------------------------------------------
// transform bind pose frame using a recursive traversal
//--------------------------------------------------------------------------------------
void SDKMesh::transformBindPoseFrame(uint32_t frame, CXMMATRIX parentWorld)
{
	if (m_bindPoseFrameMatrices.empty()) return;

	// Transform ourselves
	const auto m = XMLoadFloat4x4(&m_pFrameArray[frame].Matrix);
	const auto mLocalWorld = XMMatrixMultiply(m, parentWorld);
	DirectX::XMStoreFloat4x4(&m_bindPoseFrameMatrices[frame], mLocalWorld);

	// Transform our siblings
	if (m_pFrameArray[frame].SiblingFrame != INVALID_FRAME)
		transformBindPoseFrame(m_pFrameArray[frame].SiblingFrame, parentWorld);

	// Transform our children
	if( m_pFrameArray[frame].ChildFrame != INVALID_FRAME)
		transformBindPoseFrame(m_pFrameArray[frame].ChildFrame, mLocalWorld);
}

//--------------------------------------------------------------------------------------
// transform frame using a recursive traversal
//--------------------------------------------------------------------------------------
void SDKMesh::transformFrame(uint32_t frame, CXMMATRIX parentWorld, double time)
{
	// Get the tick data
	XMMATRIX localTransform;
	const auto tick = GetAnimationKeyFromTime(time);

	if (INVALID_ANIMATION_DATA != m_pFrameArray[frame].AnimationDataIndex)
	{
		const auto frameData = &m_pAnimationFrameData[m_pFrameArray[frame].AnimationDataIndex];
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
	else localTransform = XMLoadFloat4x4(&m_pFrameArray[frame].Matrix);

	// Transform ourselves
	auto localWorld = XMMatrixMultiply(localTransform, parentWorld);
	XMStoreFloat4x4(&m_transformedFrameMatrices[frame], localWorld);
	XMStoreFloat4x4(&m_worldPoseFrameMatrices[frame], localWorld);

	// Transform our siblings
	if (m_pFrameArray[frame].SiblingFrame != INVALID_FRAME)
		transformFrame(m_pFrameArray[frame].SiblingFrame, parentWorld, time);

	// Transform our children
	if (m_pFrameArray[frame].ChildFrame != INVALID_FRAME)
		transformFrame(m_pFrameArray[frame].ChildFrame, localWorld, time);
}

//--------------------------------------------------------------------------------------
// transform frame assuming that it is an absolute transformation
//--------------------------------------------------------------------------------------
void SDKMesh::transformFrameAbsolute(uint32_t frame, double time)
{
	const auto iTick = GetAnimationKeyFromTime(time);

	if (INVALID_ANIMATION_DATA != m_pFrameArray[frame].AnimationDataIndex)
	{
		const auto pFrameData = &m_pAnimationFrameData[m_pFrameArray[frame].AnimationDataIndex];
		const auto pData = &pFrameData->pAnimationData[iTick];
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
		XMStoreFloat4x4(&m_transformedFrameMatrices[frame], mOutput);
	}
}
