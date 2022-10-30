//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "XUSGAdvanced.h"

//--------------------------------------------------------------------------------------
// Hard Defines for the various structures
//--------------------------------------------------------------------------------------
#define SDKMESH_FILE_VERSION	101
#define MAX_VERTEX_ELEMENTS		32
#define INVALID_FRAME			((uint32_t)-1)
#define INVALID_MESH			((uint32_t)-1)
#define INVALID_MATERIAL		((uint32_t)-1)
#define INVALID_SUBSET			((uint32_t)-1)
#define INVALID_ANIMATION_DATA	((uint32_t)-1)
#define INVALID_SAMPLER_SLOT	((uint32_t)-1)
#define ERROR_RESOURCE_VALUE	1

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p)	{ if (p) { delete[] (p); (p) = nullptr; } }
#endif

namespace XUSG
{
	template<typename TYPE> bool IsErrorResource(TYPE data)
	{
		if (static_cast<TYPE>(ERROR_RESOURCE_VALUE) == data) return true;

		return false;
	}

	enum FrameTransformType
	{
		FTT_RELATIVE = 0,
		FTT_ABSOLUTE		// This is not currently used but is here to support absolute transformations in the future
	};

	//--------------------------------------------------------------------------------------
	// SDKMesh class. This class reads the sdkmesh file format
	//--------------------------------------------------------------------------------------
	class SDKMesh_Impl :
		public virtual SDKMesh
	{
	public:
		//--------------------------------------------------------------------------------------
		// Structures. Unions with pointers are forced to 64bit.
		//--------------------------------------------------------------------------------------
#pragma pack(push, 8)
		struct Header
		{
			//Basic Info and sizes
			uint32_t Version;
			uint8_t IsBigEndian;
			uint64_t HeaderSize;
			uint64_t NonBufferDataSize;
			uint64_t BufferDataSize;

			//Stats
			uint32_t NumVertexBuffers;
			uint32_t NumIndexBuffers;
			uint32_t NumMeshes;
			uint32_t NumTotalSubsets;
			uint32_t NumFrames;
			uint32_t NumMaterials;

			//Offsets to Data
			uint64_t VertexStreamHeadersOffset;
			uint64_t IndexStreamHeadersOffset;
			uint64_t MeshDataOffset;
			uint64_t SubsetDataOffset;
			uint64_t FrameDataOffset;
			uint64_t MaterialDataOffset;
		};

		struct VertexBufferHeader
		{
			uint64_t NumVertices;
			uint64_t SizeBytes;
			uint64_t StrideBytes;

			struct VertexElement
			{
				uint16_t	Stream;		// Stream index
				uint16_t	Offset;		// Offset in the stream in bytes
				uint8_t		Type;		// Data type
				uint8_t		Method;		// Processing method
				uint8_t		Usage;		// Semantics
				uint8_t		UsageIndex;	// Semantic index
			} Decl[MAX_VERTEX_ELEMENTS];

			uint64_t DataOffset;		// (This also forces the union to 64bits)
		};

		struct IndexBufferHeader
		{
			uint64_t NumIndices;
			uint64_t SizeBytes;
			uint32_t IndexType;
			uint64_t DataOffset;		// (This also forces the union to 64bits)
		};
#pragma pack(pop)

		static_assert(sizeof(VertexBufferHeader::VertexElement) == 8, "Vertex element structure size incorrect");
		static_assert(sizeof(Header) == 104, "SDK Mesh structure size incorrect");
		static_assert(sizeof(VertexBufferHeader) == 288, "SDK Mesh structure size incorrect");
		static_assert(sizeof(IndexBufferHeader) == 32, "SDK Mesh structure size incorrect");
		static_assert(sizeof(Data) == 224, "SDK Mesh structure size incorrect");
		static_assert(sizeof(Subset) == 144, "SDK Mesh structure size incorrect");
		static_assert(sizeof(Frame) == 184, "SDK Mesh structure size incorrect");
		static_assert(sizeof(Material) == 1256, "SDK Mesh structure size incorrect");
		static_assert(sizeof(AnimationFileHeader) == 40, "SDK Mesh structure size incorrect");
		static_assert(sizeof(AnimationData) == 40, "SDK Mesh structure size incorrect");
		static_assert(sizeof(AnimationFrameData) == 112, "SDK Mesh structure size incorrect");

		SDKMesh_Impl(API api = API::DIRECTX_12);
		virtual ~SDKMesh_Impl();

		bool Create(const Device* pDevice, const wchar_t* fileName,
			const TextureLib& textureLib, bool isStaticMesh = false);
		bool Create(const Device* pDevice, uint8_t* pData, const TextureLib& textureLib,
			size_t dataBytes, bool isStaticMesh = false, bool copyStatic = false);
		bool LoadAnimation(const wchar_t* fileName);
		void Destroy();

		//Frame manipulation
		void TransformBindPose(DirectX::CXMMATRIX world);
		void TransformMesh(DirectX::CXMMATRIX world, double time);

		// Helpers (Graphics API specific)
		Format GetIBFormat(uint32_t mesh) const;

		IndexType GetIndexType(uint32_t mesh) const;

		const Descriptor&		GetVertexBufferSRV(uint32_t mesh, uint32_t i) const;
		const VertexBufferView&	GetVertexBufferView(uint32_t mesh, uint32_t i) const;
		const IndexBufferView&	GetIndexBufferView(uint32_t mesh) const;
		const IndexBufferView&	GetAdjIndexBufferView(uint32_t mesh) const;

		const Descriptor&		GetVertexBufferSRVAt(uint32_t vb) const;
		const VertexBufferView&	GetVertexBufferViewAt(uint32_t vb) const;
		const IndexBufferView&	GetIndexBufferViewAt(uint32_t ib) const;

		// Helpers (general)
		const char*			GetMeshPathA() const;
		const wchar_t*		GetMeshPathW() const;
		uint32_t			GetNumMeshes() const;
		uint32_t			GetNumMaterials() const;
		uint32_t			GetNumVertexBuffers() const;
		uint32_t			GetNumIndexBuffers() const;

		uint8_t*			GetRawVerticesAt(uint32_t vb) const;
		uint8_t*			GetRawIndicesAt(uint32_t ib) const;

		Material*			GetMaterial(uint32_t material) const;
		Data*				GetMesh(uint32_t mesh) const;
		uint32_t			GetNumSubsets(uint32_t mesh) const;
		uint32_t			GetNumSubsets(uint32_t mesh, SubsetFlags materialType) const;
		Subset*				GetSubset(uint32_t mesh, uint32_t subset) const;
		Subset*				GetSubset(uint32_t mesh, uint32_t subset, SubsetFlags materialType) const;
		uint32_t			GetVertexStride(uint32_t mesh, uint32_t i) const;
		uint32_t			GetNumFrames() const;
		Frame*				GetFrame(uint32_t frame) const;
		Frame*				FindFrame(const char* name) const;
		uint32_t			FindFrameIndex(const char* name) const;
		uint64_t			GetNumVertices(uint32_t mesh, uint32_t i) const;
		uint64_t			GetNumIndices(uint32_t mesh) const;
		DirectX::XMVECTOR	GetMeshBBoxCenter(uint32_t mesh) const;
		DirectX::XMVECTOR	GetMeshBBoxExtents(uint32_t mesh) const;
		uint32_t			GetOutstandingResources() const;
		uint32_t			GetOutstandingBufferResources() const;
		bool				CheckLoadDone();
		bool				IsLoaded() const;
		bool				IsLoading() const;
		void				SetLoading(bool loading);
		bool				HadLoadingError() const;

		// Animation
		uint32_t			GetNumInfluences(uint32_t mesh) const;
		DirectX::XMMATRIX	GetMeshInfluenceMatrix(uint32_t mesh, uint32_t influence) const;
		uint32_t			GetAnimationKeyFromTime(double time) const;
		DirectX::XMMATRIX	GetWorldMatrix(uint32_t frameIndex) const;
		DirectX::XMMATRIX	GetInfluenceMatrix(uint32_t frameIndex) const;
		DirectX::XMMATRIX	GetBindMatrix(uint32_t frameIndex) const;
		bool				GetAnimationProperties(uint32_t* pNumKeys, float* pFrameTime) const;

	protected:
		void loadMaterials(CommandList* pCommandList, Material* pMaterials,
			uint32_t NumMaterials, std::vector<Resource::uptr>& uploaders);

		bool createVertexBuffer(CommandList* pCommandList, std::vector<Resource::uptr>& uploaders);
		bool createIndexBuffer(CommandList* pCommandList, std::vector<Resource::uptr>& uploaders);

		virtual bool createFromFile(const Device* pDevice, const wchar_t* fileName,
			const TextureLib& textureLib, bool isStaticMesh);
		virtual bool createFromMemory(const Device* pDevice, uint8_t* pData, const TextureLib& textureLib,
			size_t dataBytes, bool isStaticMesh, bool copyStatic);

		void createAsStaticMesh();
		void classifyMaterialType();
		bool executeCommandList(CommandList* pCommandList);

		// Frame manipulation
		void transformBindPoseFrame(uint32_t frame, DirectX::CXMMATRIX parentWorld);
		void transformFrame(uint32_t frame, DirectX::CXMMATRIX parentWorld, double time);
		void transformFrameAbsolute(uint32_t frame, double time);

		API m_api;

		// These are the pointers to the two chunks of data loaded in from the mesh file
		uint8_t* m_pStaticMeshData;
		std::vector<uint8_t>	m_heapData;
		std::vector<uint8_t>	m_animation;
		std::vector<uint8_t*>	m_vertices;
		std::vector<uint8_t*>	m_indices;

		// Keep track of the path
		std::wstring			m_name;
		std::wstring			m_filePathW;
		std::string				m_filePath;

		// General mesh info
		Header*					m_pMeshHeader;
		VertexBufferHeader*		m_pVertexBufferArray;
		IndexBufferHeader*		m_pIndexBufferArray;
		Data*					m_pMeshArray;
		Subset*					m_pSubsetArray;
		Frame*					m_pFrameArray;
		Material*				m_pMaterialArray;

		VertexBuffer::sptr		m_vertexBuffer;
		IndexBuffer::sptr		m_indexBuffer;
		IndexBuffer::sptr		m_adjIndexBuffer;

		// Classified subsets
		std::vector<std::vector<uint32_t>> m_classifiedSubsets[NUM_SUBSET_TYPE];

		// Texture cache
		TextureLib				m_textureLib;

		// Adjacency information (not part of the m_pStaticMeshData, so it must be created and destroyed separately )
		IndexBufferHeader*		m_pAdjIndexBufferArray;

		// Animation
		AnimationFileHeader*	m_pAnimationHeader;
		AnimationFrameData*		m_pAnimationFrameData;
		std::vector<DirectX::XMFLOAT4X4> m_bindPoseFrameMatrices;
		std::vector<DirectX::XMFLOAT4X4> m_transformedFrameMatrices;
		std::vector<DirectX::XMFLOAT4X4> m_worldPoseFrameMatrices;

	private:
		uint32_t m_numOutstandingResources;
		bool m_isLoading;
	};
}
