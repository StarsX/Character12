//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "XUSGSDKMesh.h"
#include "XUSGDDSLoader.h"

using namespace std;
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace XUSG;

//--------------------------------------------------------------------------------------
// Create interfaces
//--------------------------------------------------------------------------------------
SDKMesh::uptr SDKMesh::MakeUnique()
{
	return make_unique<SDKMesh_Impl>();
}

SDKMesh::sptr SDKMesh::MakeShared()
{
	return make_shared<SDKMesh_Impl>();
}

//--------------------------------------------------------------------------------------
// Static interface function
//--------------------------------------------------------------------------------------
PrimitiveTopology SDKMesh::GetPrimitiveType(PrimitiveType primType)
{
	PrimitiveTopology retType = PrimitiveTopology::TRIANGLELIST;

	switch (primType)
	{
	case PT_TRIANGLE_LIST:
		retType = PrimitiveTopology::TRIANGLELIST;
		break;
	case PT_TRIANGLE_STRIP:
		retType = PrimitiveTopology::TRIANGLESTRIP;
		break;
	case PT_LINE_LIST:
		retType = PrimitiveTopology::LINELIST;
		break;
	case PT_LINE_STRIP:
		retType = PrimitiveTopology::LINESTRIP;
		break;
	case PT_POINT_LIST:
		retType = PrimitiveTopology::POINTLIST;
		break;
	case PT_TRIANGLE_LIST_ADJ:
		retType = PrimitiveTopology::TRIANGLELIST_ADJ;
		break;
	case PT_TRIANGLE_STRIP_ADJ:
		retType = PrimitiveTopology::TRIANGLESTRIP_ADJ;
		break;
	case PT_LINE_LIST_ADJ:
		retType = PrimitiveTopology::LINELIST_ADJ;
		break;
	case PT_LINE_STRIP_ADJ:
		retType = PrimitiveTopology::LINESTRIP_ADJ;
		break;
	};

	return retType;
}

//--------------------------------------------------------------------------------------
// SDKMesh implementations
//--------------------------------------------------------------------------------------
SDKMesh_Impl::SDKMesh_Impl() :
	m_device(nullptr),
	m_numOutstandingResources(0),
	m_isLoading(false),
	m_pStaticMeshData(nullptr),
	m_heapData(0),
	m_animation(0),
	m_vertices(0),
	m_indices(0),
	m_name(),
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
SDKMesh_Impl::~SDKMesh_Impl()
{
}

//--------------------------------------------------------------------------------------
bool SDKMesh_Impl::Create(const Device& device, const wchar_t* fileName,
	const TextureCache& textureCache, bool isStaticMesh)
{
	return createFromFile(device, fileName, textureCache, isStaticMesh);
}

bool SDKMesh_Impl::Create(const Device& device, uint8_t* pData,
	const TextureCache& textureCache, size_t dataBytes,
	bool isStaticMesh, bool copyStatic)
{
	return createFromMemory(device, pData, textureCache, dataBytes, isStaticMesh, copyStatic);
}

bool SDKMesh_Impl::LoadAnimation(const wchar_t* fileName)
{
	wchar_t filePath[MAX_PATH];

	// Find the path for the file
	wcsncpy_s(filePath, MAX_PATH, fileName, wcslen(fileName));
	//V_RETURN(DXUTFindDXSDKMediaFileCch(filePath, MAX_PATH, fileName));

	// Open the file
	ifstream fileStream(filePath, ios::in | ios::binary);
	F_RETURN(!fileStream, cerr, MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903), false);

	// Read header
	AnimationFileHeader fileheader;
	F_RETURN(!fileStream.read(reinterpret_cast<char*>(&fileheader), sizeof(AnimationFileHeader)),
		fileStream.close(); cerr, GetLastError(), false);

	// Allocate
	m_animation.resize(static_cast<size_t>(sizeof(AnimationFileHeader) + fileheader.AnimationDataSize));

	// Read it all in
	F_RETURN(!fileStream.seekg(0), fileStream.close(); cerr, GetLastError(), false);

	const auto cBytes = static_cast<streamsize>(sizeof(AnimationFileHeader) + fileheader.AnimationDataSize);
	F_RETURN(!fileStream.read(reinterpret_cast<char*>(m_animation.data()), cBytes),
		fileStream.close(); cerr, GetLastError(), false);

	fileStream.close();

	// pointer fixup
	m_pAnimationHeader = reinterpret_cast<AnimationFileHeader*>(m_animation.data());
	m_pAnimationFrameData = reinterpret_cast<AnimationFrameData*>(m_animation.data() + m_pAnimationHeader->AnimationDataOffset);

	const auto BaseOffset = sizeof(AnimationFileHeader);

	for (auto i = 0u; i < m_pAnimationHeader->NumFrames; ++i)
	{
		m_pAnimationFrameData[i].pAnimationData = reinterpret_cast<AnimationData*>
			(m_animation.data() + m_pAnimationFrameData[i].DataOffset + BaseOffset);

		const auto pFrame = FindFrame(m_pAnimationFrameData[i].FrameName);

		if (pFrame) pFrame->AnimationDataIndex = i;
	}

	return true;
}

void SDKMesh_Impl::Destroy()
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
void SDKMesh_Impl::TransformBindPose(CXMMATRIX world)
{
	transformBindPoseFrame(0, world);
}

//--------------------------------------------------------------------------------------
// transform the mesh frames according to the animation for time
//--------------------------------------------------------------------------------------
void SDKMesh_Impl::TransformMesh(CXMMATRIX world, double time)
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
Format SDKMesh_Impl::GetIBFormat(uint32_t mesh) const
{
	switch (m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].IndexType)
	{
	case IT_16BIT:
		return Format::R16_UINT;
	case IT_32BIT:
		return Format::R32_UINT;
	};

	return Format::R16_UINT;
}

SDKMesh::IndexType SDKMesh_Impl::GetIndexType(uint32_t mesh) const
{
	return static_cast<IndexType>(m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].IndexType);
}

const Descriptor& SDKMesh_Impl::GetVertexBufferSRV(uint32_t mesh, uint32_t i) const
{
	return m_vertexBuffer->GetSRV(m_pMeshArray[mesh].VertexBuffers[i]);
}

const VertexBufferView& SDKMesh_Impl::GetVertexBufferView(uint32_t mesh, uint32_t i) const
{
	return m_vertexBuffer->GetVBV(m_pMeshArray[mesh].VertexBuffers[i]);
}

const IndexBufferView& SDKMesh_Impl::GetIndexBufferView(uint32_t mesh) const
{
	return m_indexBuffer->GetIBV(m_pMeshArray[mesh].IndexBuffer);
}

const IndexBufferView& SDKMesh_Impl::GetAdjIndexBufferView(uint32_t mesh) const
{
	return m_adjIndexBuffer->GetIBV(m_pMeshArray[mesh].IndexBuffer);
}

const Descriptor& SDKMesh_Impl::GetVertexBufferSRVAt(uint32_t vb) const
{
	return m_vertexBuffer->GetSRV(vb);
}

const VertexBufferView& SDKMesh_Impl::GetVertexBufferViewAt(uint32_t vb) const
{
	return m_vertexBuffer->GetVBV(vb);
}

const IndexBufferView& SDKMesh_Impl::GetIndexBufferViewAt(uint32_t ib) const
{
	return m_indexBuffer->GetIBV(ib);
}

//--------------------------------------------------------------------------------------
const char* SDKMesh_Impl::GetMeshPathA() const
{
	return m_filePath.c_str();
}

const wchar_t* SDKMesh_Impl::GetMeshPathW() const
{
	return m_filePathW.c_str();
}

uint32_t SDKMesh_Impl::GetNumMeshes() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMeshes : 0;
}

uint32_t SDKMesh_Impl::GetNumMaterials() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumMaterials : 0;
}

uint32_t SDKMesh_Impl::GetNumVertexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumVertexBuffers : 0;
}

uint32_t SDKMesh_Impl::GetNumIndexBuffers() const
{
	return m_pMeshHeader ? m_pMeshHeader->NumIndexBuffers : 0;
}

uint8_t* SDKMesh_Impl::GetRawVerticesAt(uint32_t vb) const
{
	return m_vertices[vb];
}

uint8_t* SDKMesh_Impl::GetRawIndicesAt(uint32_t ib) const
{
	return m_indices[ib];
}

SDKMesh::Material* SDKMesh_Impl::GetMaterial(uint32_t material) const
{
	return &m_pMaterialArray[material];
}

SDKMesh::Data* SDKMesh_Impl::GetMesh(uint32_t mesh) const
{
	return &m_pMeshArray[mesh];
}

uint32_t SDKMesh_Impl::GetNumSubsets(uint32_t mesh) const
{
	return m_pMeshArray[mesh].NumSubsets;
}

uint32_t SDKMesh_Impl::GetNumSubsets(uint32_t mesh, SubsetFlags materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return static_cast<uint32_t>(m_classifiedSubsets[materialType - 1][mesh].size());
}

SDKMesh::Subset* SDKMesh_Impl::GetSubset(uint32_t mesh, uint32_t subset) const
{
	return &m_pSubsetArray[m_pMeshArray[mesh].pSubsets[subset]];
}

SDKMesh::Subset* SDKMesh_Impl::GetSubset(uint32_t mesh, uint32_t subset, SubsetFlags materialType) const
{
	assert(materialType == SUBSET_OPAQUE || materialType == SUBSET_ALPHA);

	return &m_pSubsetArray[m_classifiedSubsets[materialType - 1][mesh][subset]];
}

uint32_t SDKMesh_Impl::GetVertexStride(uint32_t mesh, uint32_t i) const
{
	return static_cast<uint32_t>(m_pVertexBufferArray[m_pMeshArray[mesh].VertexBuffers[i]].StrideBytes);
}

uint32_t SDKMesh_Impl::GetNumFrames() const
{
	return m_pMeshHeader->NumFrames;
}

SDKMesh::Frame* SDKMesh_Impl::GetFrame(uint32_t frame) const
{
	assert(frame < m_pMeshHeader->NumFrames);

	return &m_pFrameArray[frame];
}

SDKMesh::Frame* SDKMesh_Impl::FindFrame(const char* name) const
{
	const auto i = FindFrameIndex(name);

	return i == INVALID_FRAME ? nullptr : &m_pFrameArray[i];
}

uint32_t SDKMesh_Impl::FindFrameIndex(const char* name) const
{
	for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
		if (_stricmp(m_pFrameArray[i].Name, name) == 0)
			return i;

	return INVALID_FRAME;
}

uint64_t SDKMesh_Impl::GetNumVertices(uint32_t mesh, uint32_t i) const
{
	return m_pVertexBufferArray[m_pMeshArray[mesh].VertexBuffers[i]].NumVertices;
}

uint64_t SDKMesh_Impl::GetNumIndices(uint32_t mesh) const
{
	return m_pIndexBufferArray[m_pMeshArray[mesh].IndexBuffer].NumIndices;
}

XMVECTOR SDKMesh_Impl::GetMeshBBoxCenter(uint32_t mesh) const
{
	return XMLoadFloat3(&m_pMeshArray[mesh].BoundingBoxCenter);
}

XMVECTOR SDKMesh_Impl::GetMeshBBoxExtents(uint32_t mesh) const
{
	return XMLoadFloat3(&m_pMeshArray[mesh].BoundingBoxExtents);
}

uint32_t SDKMesh_Impl::GetOutstandingResources() const
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

uint32_t SDKMesh_Impl::GetOutstandingBufferResources() const
{
	uint32_t outstandingResources = 0;
	if (!m_pMeshHeader) return 1;

	return outstandingResources;
}

bool SDKMesh_Impl::CheckLoadDone()
{
	if (0 == GetOutstandingResources())
	{
		m_isLoading = false;

		return true;
	}

	return false;
}

bool SDKMesh_Impl::IsLoaded() const
{
	if (m_pStaticMeshData && !m_isLoading)
	{
		return true;
	}

	return false;
}

bool SDKMesh_Impl::IsLoading() const
{
	return m_isLoading;
}

void SDKMesh_Impl::SetLoading(bool loading)
{
	m_isLoading = loading;
}

bool SDKMesh_Impl::HadLoadingError() const
{
	return false;
}

//--------------------------------------------------------------------------------------
uint32_t SDKMesh_Impl::GetNumInfluences(uint32_t mesh) const
{
	return m_pMeshArray[mesh].NumFrameInfluences;
}

XMMATRIX SDKMesh_Impl::GetMeshInfluenceMatrix(uint32_t mesh, uint32_t influence) const
{
	const auto frame = m_pMeshArray[mesh].pFrameInfluences[influence];

	return XMLoadFloat4x4(&m_transformedFrameMatrices[frame]);
}

XMMATRIX SDKMesh_Impl::GetWorldMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_worldPoseFrameMatrices[frameIndex]);
}

XMMATRIX SDKMesh_Impl::GetInfluenceMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_transformedFrameMatrices[frameIndex]);
}

XMMATRIX SDKMesh_Impl::GetBindMatrix(uint32_t frameIndex) const
{
	return XMLoadFloat4x4(&m_bindPoseFrameMatrices[frameIndex]);
}

uint32_t SDKMesh_Impl::GetAnimationKeyFromTime(double time) const
{
	if (!m_pAnimationHeader) return 0;

	auto tick = static_cast<uint32_t>(m_pAnimationHeader->AnimationFPS * time);

	tick = tick % (m_pAnimationHeader->NumAnimationKeys - 1);

	return ++tick;
}

bool SDKMesh_Impl::GetAnimationProperties(uint32_t* pNumKeys, float* pFrameTime) const
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
void SDKMesh_Impl::loadMaterials(CommandList* pCommandList, Material* pMaterials,
	uint32_t numMaterials, vector<Resource>& uploaders)
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
				uploaders.emplace_back();

				filePathW.assign(filePath.cbegin(), filePath.cend());
				if (!textureLoader.CreateTextureFromFile(m_device, pCommandList, filePathW.c_str(),
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
				uploaders.emplace_back();

				filePathW.assign(filePath.cbegin(), filePath.cend());
				if (!textureLoader.CreateTextureFromFile(m_device, pCommandList, filePathW.c_str(),
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
				uploaders.emplace_back();

				filePathW.assign(filePath.cbegin(), filePath.cend());
				if (!textureLoader.CreateTextureFromFile(m_device, pCommandList, filePathW.c_str(),
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

bool SDKMesh_Impl::createVertexBuffer(CommandList* pCommandList, std::vector<Resource>& uploaders)
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
	m_vertexBuffer = VertexBuffer::MakeShared();
	N_RETURN(m_vertexBuffer->Create(m_device, numVertices, stride, ResourceFlag::NONE,
		MemoryType::DEFAULT, m_pMeshHeader->NumVertexBuffers, firstVertices.data(),
		m_pMeshHeader->NumVertexBuffers, firstVertices.data(), 1, nullptr,
		m_name.empty() ? nullptr : (m_name + L".VertexBuffer").c_str()), false);

	// Copy vertices into one buffer
	size_t offset = 0;
	vector<uint8_t> bufferData(static_cast<size_t>(stride) * numVertices);

	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		const auto sizeBytes = static_cast<size_t>(m_pVertexBufferArray[i].SizeBytes);
		memcpy(&bufferData[offset], m_vertices[i], sizeBytes);
		offset += sizeBytes;
	}

	// Upload vertices
	uploaders.emplace_back();

	return m_vertexBuffer->Upload(pCommandList, uploaders.back(), bufferData.data(), bufferData.size());
}

bool SDKMesh_Impl::createIndexBuffer(CommandList* pCommandList, std::vector<Resource>& uploaders)
{
	// Index buffer info
	size_t byteWidth = 0;
	vector<size_t> offsets(m_pMeshHeader->NumIndexBuffers);

	for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
	{
		offsets[i] = byteWidth;
		byteWidth += static_cast<uint32_t>(m_pIndexBufferArray[i].SizeBytes);
	}

	// Create a index Buffer
	m_indexBuffer = IndexBuffer::MakeShared();
	N_RETURN(m_indexBuffer->Create(m_device, byteWidth, m_pIndexBufferArray->IndexType == IT_32BIT ?
		Format::R32_UINT : Format::R16_UINT, ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::DEFAULT, m_pMeshHeader->NumIndexBuffers, offsets.data(), 1, nullptr, 1,
		nullptr, m_name.empty() ? nullptr : (m_name + L".IndexBuffer").c_str()), false);

	// Copy indices into one buffer
	size_t offset = 0;
	vector<uint8_t> bufferData(byteWidth);

	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
	{
		const auto sizeBytes = static_cast<size_t>(m_pIndexBufferArray[i].SizeBytes);
		memcpy(&bufferData[offset], m_indices[i], sizeBytes);
		offset += sizeBytes;
	}

	// Upload indices
	uploaders.emplace_back();

	return m_indexBuffer->Upload(pCommandList, uploaders.back(), bufferData.data(), bufferData.size());
}

//--------------------------------------------------------------------------------------
bool SDKMesh_Impl::createFromFile(const Device& device, const wchar_t* fileName,
	const TextureCache& textureCache, bool isStaticMesh)
{
	// Find the path for the file
	m_filePathW = fileName;

	// Open the file
	ifstream fileStream(m_filePathW, ios::in | ios::binary);
	F_RETURN(!fileStream, cerr, MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0903), false);

	// Change the path to just the directory
	const auto found = m_filePathW.find_last_of(L"/\\");
	m_name = m_filePathW.substr(found + 1);
	m_filePathW = m_filePathW.substr(0, found + 1);
	m_filePath.resize(m_filePathW.size());
	for (size_t i = 0; i < m_filePath.size(); ++i) m_filePath[i] = static_cast<char>(m_filePathW[i]);

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

	return createFromMemory(device, m_pStaticMeshData, textureCache, cBytes, isStaticMesh, false);
}

bool SDKMesh_Impl::createFromMemory(const Device& device, uint8_t* pData,
	const TextureCache& textureCache, size_t dataBytes,
	bool isStaticMesh, bool copyStatic)
{
	XMFLOAT3 lower;
	XMFLOAT3 upper;

	m_device = device;
	CommandAllocator commandAllocator = nullptr;
	const auto commandList = CommandList::MakeUnique();
	const auto pCommandList = commandList.get();
	if (device)
	{
		N_RETURN(m_device->GetCommandAllocator(commandAllocator, CommandListType::DIRECT), false);
		N_RETURN(m_device->GetCommandList(pCommandList, 0, CommandListType::DIRECT,
			commandAllocator, nullptr), false);
	}

	F_RETURN(dataBytes < sizeof(Header), cerr, E_FAIL, false);

	// Set outstanding resources to zero
	m_numOutstandingResources = 0;

	if (copyStatic)
	{
		const auto pHeader = reinterpret_cast<Header*>(pData);
		const auto StaticSize = static_cast<SIZE_T>(pHeader->HeaderSize + pHeader->NonBufferDataSize);
		F_RETURN(dataBytes < StaticSize, cerr, E_FAIL, false);

		m_heapData.resize(StaticSize);
		m_pStaticMeshData = m_heapData.data();

		memcpy(m_pStaticMeshData, pData, StaticSize);
	}
	else m_pStaticMeshData = pData;

	// Pointer fixup
	m_pMeshHeader = reinterpret_cast<Header*>(m_pStaticMeshData);

	m_pVertexBufferArray = reinterpret_cast<VertexBufferHeader*>(m_pStaticMeshData + m_pMeshHeader->VertexStreamHeadersOffset);
	m_pIndexBufferArray = reinterpret_cast<IndexBufferHeader*>(m_pStaticMeshData + m_pMeshHeader->IndexStreamHeadersOffset);
	m_pMeshArray = reinterpret_cast<Data*>(m_pStaticMeshData + m_pMeshHeader->MeshDataOffset);
	m_pSubsetArray = reinterpret_cast<Subset*>(m_pStaticMeshData + m_pMeshHeader->SubsetDataOffset);
	m_pFrameArray = reinterpret_cast<Frame*>(m_pStaticMeshData + m_pMeshHeader->FrameDataOffset);
	m_pMaterialArray = reinterpret_cast<Material*>(m_pStaticMeshData + m_pMeshHeader->MaterialDataOffset);

	// Setup subsets
	for (auto i = 0u; i < m_pMeshHeader->NumMeshes; ++i)
	{
		m_pMeshArray[i].pSubsets = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].SubsetOffset);
		m_pMeshArray[i].pFrameInfluences = reinterpret_cast<uint32_t*>(m_pStaticMeshData + m_pMeshArray[i].FrameInfluenceOffset);
	}

	// error condition
	F_RETURN(m_pMeshHeader->Version != SDKMESH_FILE_VERSION, cerr, E_NOINTERFACE, false);

	// Create VBs
	m_vertices.resize(m_pMeshHeader->NumVertexBuffers);
	for (auto i = 0u; i < m_pMeshHeader->NumVertexBuffers; ++i)
		m_vertices[i] = reinterpret_cast<uint8_t*>(pData + m_pVertexBufferArray[i].DataOffset);

	// Create IBs
	m_indices.resize(m_pMeshHeader->NumIndexBuffers);
	for (auto i = 0u; i < m_pMeshHeader->NumIndexBuffers; ++i)
		m_indices[i] = reinterpret_cast<uint8_t*>(pData + m_pIndexBufferArray[i].DataOffset);

	// Uploader buffers
	vector<Resource> uploaders;

	// Load Materials
	m_textureCache = textureCache;
	if (device) loadMaterials(pCommandList, m_pMaterialArray, m_pMeshHeader->NumMaterials, uploaders);

	// Create a place to store our bind pose frame matrices
	m_bindPoseFrameMatrices.resize(m_pMeshHeader->NumFrames);

	// Create a place to store our transformed frame matrices
	m_transformedFrameMatrices.resize(m_pMeshHeader->NumFrames);
	m_worldPoseFrameMatrices.resize(m_pMeshHeader->NumFrames);

	// Process as a static mesh
	if (isStaticMesh) createAsStaticMesh();

	Subset* pSubset = nullptr;
	PrimitiveTopology primType;

	// update bounding volume
	Data* currentMesh = m_pMeshArray;
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

			primType = GetPrimitiveType(static_cast<PrimitiveType>(pSubset->PrimitiveType));
			assert(primType == PrimitiveTopology::TRIANGLELIST);	// only triangle lists are handled.

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
			assert(stride % 4 == 0);
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
					}
					else
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
	N_RETURN(createVertexBuffer(pCommandList, uploaders), false);
	N_RETURN(createIndexBuffer(pCommandList, uploaders), false);

	// Execute commands
	return executeCommandList(pCommandList);
}

void SDKMesh_Impl::createAsStaticMesh()
{
	// Calculate transform
	XMVECTOR vScaling, quat, vTranslate;
	for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
	{
		if (m_pFrameArray[i].Mesh != INVALID_MESH)
		{
			auto localTransform = XMLoadFloat4x4(&m_pFrameArray[i].Matrix);
			XMMatrixDecompose(&vScaling, &quat, &vTranslate, localTransform);

			vScaling = XMVectorSwizzle(vScaling, 0, 2, 1, 3);
			quat = XMVectorSwizzle(quat, 0, 2, 1, 3);
			quat = XMVectorSetY(quat, -XMVectorGetY(quat));
			const auto translate = XMMatrixTranslationFromVector(vTranslate);
			const auto scaling = XMMatrixScalingFromVector(vScaling);
			const auto quatMatrix = XMMatrixRotationQuaternion(quat);
			localTransform = scaling * quatMatrix * translate;
			XMStoreFloat4x4(&m_pFrameArray[i].Matrix, localTransform);
		}
	}
	TransformBindPose(XMMatrixIdentity());

	// Recompute vertex buffers
	for (auto i = 0u; i < m_pMeshHeader->NumFrames; ++i)
	{
		const auto& m = m_pFrameArray[i].Mesh;
		if (m != INVALID_MESH)
		{
			const auto numVerts = GetNumVertices(m, 0);
			const auto vb = m_pMeshArray[m].VertexBuffers[0];
			const auto local = GetBindMatrix(i);
			const auto verts = m_vertices[vb];
			const auto stride = GetVertexStride(m, 0);
			const auto localIT = XMMatrixTranspose(XMMatrixInverse(nullptr, local));

			for (auto i = 0u; i < numVerts; ++i)
			{
				auto offset = stride * i;
				auto& pos = reinterpret_cast<XMFLOAT3&>(verts[offset]);

				offset += sizeof(XMFLOAT3);
				auto& norm = reinterpret_cast<XMHALF4&>(verts[offset]);

				offset += sizeof(XMFLOAT3);
				auto& tan = reinterpret_cast<XMHALF4&>(verts[offset]);

				offset += sizeof(XMHALF4);
				auto& biNorm = reinterpret_cast<XMHALF4&>(verts[offset]);

				XMStoreFloat3(&pos, XMVector3TransformCoord(XMLoadFloat3(&pos), local));
				XMStoreHalf4(&norm, XMVector3TransformNormal(XMLoadHalf4(&norm), localIT));
				XMStoreHalf4(&tan, XMVector3TransformNormal(XMLoadHalf4(&tan), local));
				XMStoreHalf4(&biNorm, XMVector3TransformNormal(XMLoadHalf4(&biNorm), local));
			}
		}
	}
}

void SDKMesh_Impl::classifyMaterialType()
{
	const auto numMeshes = GetNumMeshes();
	for (auto& subsets : m_classifiedSubsets)
		subsets.resize(numMeshes);

	for (auto m = 0u; m < numMeshes; ++m)
	{
		const auto& numSubsets = m_pMeshArray[m].NumSubsets;
		for (auto s = 0u; s < numSubsets; ++s)
		{
			const auto& subsetIdx = m_pMeshArray[m].pSubsets[s];
			const auto& pSubset = m_pSubsetArray[subsetIdx];
			const auto pMaterial = GetMaterial(pSubset.MaterialID);

			auto subsetType = SUBSET_OPAQUE - 1;
			if (pMaterial && pMaterial->pAlbedo && !IsErrorResource(pMaterial->Albedo64))
			{
				switch (pMaterial->pAlbedo->GetFormat())
				{
				case Format::BC2_TYPELESS:
				case Format::BC2_UNORM:
				case Format::BC2_UNORM_SRGB:
				case Format::BC3_TYPELESS:
				case Format::BC3_UNORM:
				case Format::BC3_UNORM_SRGB:

				case Format::B8G8R8A8_TYPELESS:
				case Format::B8G8R8A8_UNORM:
				case Format::B8G8R8A8_UNORM_SRGB:
				case Format::B4G4R4A4_UNORM:

				case Format::R8G8B8A8_TYPELESS:
				case Format::R8G8B8A8_UNORM:
				case Format::R8G8B8A8_UNORM_SRGB:
				case Format::R8G8B8A8_SNORM:
				case Format::R10G10B10A2_UNORM:

				case Format::R16G16B16A16_TYPELESS:
				case Format::R16G16B16A16_FLOAT:
				case Format::R16G16B16A16_UNORM:
				case Format::R16G16B16A16_SNORM:
				case Format::R32G32B32A32_TYPELESS:
				case Format::R32G32B32A32_FLOAT:
					subsetType = SUBSET_ALPHA - 1;
				}
			}
			m_classifiedSubsets[subsetType][m].emplace_back(subsetIdx);
		}

		m_classifiedSubsets[SUBSET_OPAQUE - 1][m].shrink_to_fit();
		m_classifiedSubsets[SUBSET_ALPHA - 1][m].shrink_to_fit();
	}
}

bool SDKMesh_Impl::executeCommandList(CommandList* pCommandList)
{
	if (pCommandList)
	{
		// Create the command queue.
		CommandQueue commandQueue = nullptr;
		N_RETURN(m_device->GetCommandQueue(commandQueue, CommandListType::DIRECT, CommandQueueFlag::NONE), false);

		// Close the command list and execute it to begin the initial GPU setup.
		V_RETURN(pCommandList->Close(), cerr, false);
		commandQueue->SubmitCommandList(pCommandList);

		// Create synchronization objects and wait until assets have been uploaded to the GPU.
		{
			void* fenceEvent;
			Fence fence;
			uint64_t fenceValue = 0;

			N_RETURN(m_device->GetFence(fence, fenceValue++, FenceFlag::NONE), false);

			// Create an event handle to use for frame synchronization.
			fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			F_RETURN(!fenceEvent, cerr, GetLastError(), false);

			// Wait for the command list to execute; we are reusing the same command 
			// list in our main loop but for now, we just want to wait for setup to 
			// complete before continuing.
			// Schedule a Signal command in the queue.
			V_RETURN(commandQueue->Signal(fence.get(), fenceValue), cerr, false);

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
void SDKMesh_Impl::transformBindPoseFrame(uint32_t frame, CXMMATRIX parentWorld)
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
	if (m_pFrameArray[frame].ChildFrame != INVALID_FRAME)
		transformBindPoseFrame(m_pFrameArray[frame].ChildFrame, mLocalWorld);
}

//--------------------------------------------------------------------------------------
// transform frame using a recursive traversal
//--------------------------------------------------------------------------------------
void SDKMesh_Impl::transformFrame(uint32_t frame, CXMMATRIX parentWorld, double time)
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
void SDKMesh_Impl::transformFrameAbsolute(uint32_t frame, double time)
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
