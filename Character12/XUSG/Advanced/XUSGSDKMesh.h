//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

//#undef D3DCOLOR_ARGB
#include "DXFramework.h"
#include "Core/XUSGResource.h"

//--------------------------------------------------------------------------------------
// Hard Defines for the various structures
//--------------------------------------------------------------------------------------
#define SDKMESH_FILE_VERSION	101
#define MAX_VERTEX_ELEMENTS		32
#define MAX_VERTEX_STREAMS		16
#define MAX_FRAME_NAME			100
#define MAX_MESH_NAME			100
#define MAX_SUBSET_NAME			100
#define MAX_MATERIAL_NAME		100
#define MAX_TEXTURE_NAME		MAX_PATH
#define MAX_MATERIAL_PATH		MAX_PATH
#define INVALID_FRAME			((uint32_t)-1)
#define INVALID_MESH			((uint32_t)-1)
#define INVALID_MATERIAL		((uint32_t)-1)
#define INVALID_SUBSET			((uint32_t)-1)
#define INVALID_ANIMATION_DATA	((uint32_t)-1)
#define INVALID_SAMPLER_SLOT	((uint32_t)-1)
#define ERROR_RESOURCE_VALUE	1

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)			{ if (p) { delete (p); (p) = nullptr; } }
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p)	{ if (p) { delete[] (p); (p) = nullptr; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)			{ if (p) { (p)->Release(); (p) = nullptr; } }
#endif

namespace XUSG
{
	template<typename TYPE> bool IsErrorResource(TYPE data)
	{
		if (static_cast<TYPE>(ERROR_RESOURCE_VALUE) == data) return true;

		return false;
	}

	//--------------------------------------------------------------------------------------
	// Enumerated Types.
	//--------------------------------------------------------------------------------------
	enum SubsetFlags : uint8_t
	{
		SUBSET_OPAQUE		= 0x1,
		SUBSET_ALPHA		= 0x2,
		SUBSET_ALPHA_TEST	= 0x4,
		SUBSET_REFLECTED	= 0x8,
		SUBSET_FULL = SUBSET_OPAQUE | SUBSET_ALPHA | SUBSET_ALPHA_TEST,

		NUM_SUBSET_TYPE = 2
	};
	DEFINE_ENUM_FLAG_OPERATORS(SubsetFlags);

	enum SDKMESH_PRIMITIVE_TYPE
	{
		PT_TRIANGLE_LIST = 0,
		PT_TRIANGLE_STRIP,
		PT_LINE_LIST,
		PT_LINE_STRIP,
		PT_POINT_LIST,
		PT_TRIANGLE_LIST_ADJ,
		PT_TRIANGLE_STRIP_ADJ,
		PT_LINE_LIST_ADJ,
		PT_LINE_STRIP_ADJ,
		PT_QUAD_PATCH_LIST,
		PT_TRIANGLE_PATCH_LIST
	};

	enum SDKMESH_INDEX_TYPE
	{
		IT_16BIT = 0,
		IT_32BIT
	};

	enum FRAME_TRANSFORM_TYPE
	{
		FTT_RELATIVE = 0,
		FTT_ABSOLUTE		//This is not currently used but is here to support absolute transformations in the future
	};

	//--------------------------------------------------------------------------------------
	// Structures.  Unions with pointers are forced to 64bit.
	//--------------------------------------------------------------------------------------
#pragma pack(push, 8)

	struct SDKMESH_HEADER
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

	struct SDKMESH_VERTEX_BUFFER_HEADER
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

		union
		{
			uint64_t DataOffset;	// (This also forces the union to 64bits)
			void *pVertexBuffer;
		};
	};

	struct SDKMESH_INDEX_BUFFER_HEADER
	{
		uint64_t NumIndices;
		uint64_t SizeBytes;
		uint32_t IndexType;
		union
		{
			uint64_t DataOffset;	// (This also forces the union to 64bits)
			void *pIndexBuffer;
		};
	};

	struct SDKMESH_MESH
	{
		char Name[MAX_MESH_NAME];
		uint8_t NumVertexBuffers;
		uint32_t VertexBuffers[MAX_VERTEX_STREAMS];
		uint32_t IndexBuffer;
		uint32_t NumSubsets;
		uint32_t NumFrameInfluences; //aka bones

		DirectX::XMFLOAT3 BoundingBoxCenter;
		DirectX::XMFLOAT3 BoundingBoxExtents;

		union
		{
			uint64_t SubsetOffset;			//Offset to list of subsets (This also forces the union to 64bits)
			uint32_t *pSubsets;				//Pointer to list of subsets
		};
		union
		{
			uint64_t FrameInfluenceOffset;	//Offset to list of frame influences (This also forces the union to 64bits)
			uint32_t *pFrameInfluences;		//Pointer to list of frame influences
		};
	};

	struct SDKMESH_SUBSET
	{
		char Name[MAX_SUBSET_NAME];
		uint32_t MaterialID;
		uint32_t PrimitiveType;
		uint64_t IndexStart;
		uint64_t IndexCount;
		uint64_t VertexStart;
		uint64_t VertexCount;
	};

	struct SDKMESH_FRAME
	{
		char Name[MAX_FRAME_NAME];
		uint32_t Mesh;
		uint32_t ParentFrame;
		uint32_t ChildFrame;
		uint32_t SiblingFrame;
		DirectX::XMFLOAT4X4 Matrix;
		uint32_t AnimationDataIndex;		//Used to index which set of keyframes transforms this frame
	};

	struct SDKMESH_MATERIAL
	{
		char Name[MAX_MATERIAL_NAME];

		// Use MaterialInstancePath
		char MaterialInstancePath[MAX_MATERIAL_PATH];

		// Or fall back to d3d8-type materials
		char AlbedoTexture[MAX_TEXTURE_NAME];
		char NormalTexture[MAX_TEXTURE_NAME];
		char SpecularTexture[MAX_TEXTURE_NAME];

		DirectX::XMFLOAT4 Albedo;
		DirectX::XMFLOAT4 Ambient;
		DirectX::XMFLOAT4 Specular;
		DirectX::XMFLOAT4 Emissive;
		float Power;

		union
		{
			uint64_t Albedo64;			//Force the union to 64bits
			ResourceBase *pAlbedo;
		};
		union
		{
			uint64_t Normal64;			//Force the union to 64bits
			ResourceBase *pNormal;
		};
		union
		{
			uint64_t Specular64;		//Force the union to 64bits
			ResourceBase *pSpecular;
		};
		uint64_t AlphaModeAlbedo;		// Force the union to 64bits
		uint64_t AlphaModeNormal;		// Force the union to 64bits
		uint64_t AlphaModeSpecular;		// Force the union to 64bits
	};

	struct SDKANIMATION_FILE_HEADER
	{
		uint32_t	Version;
		uint8_t		IsBigEndian;
		uint32_t	FrameTransformType;
		uint32_t	NumFrames;
		uint32_t	NumAnimationKeys;
		uint32_t	AnimationFPS;
		uint64_t	AnimationDataSize;
		uint64_t	AnimationDataOffset;
	};

	struct SDKANIMATION_DATA
	{
		DirectX::XMFLOAT3 Translation;
		DirectX::XMFLOAT4 Orientation;
		DirectX::XMFLOAT3 Scaling;
	};

	struct SDKANIMATION_FRAME_DATA
	{
		char FrameName[MAX_FRAME_NAME];
		union
		{
			uint64_t DataOffset;
			SDKANIMATION_DATA* pAnimationData;
		};
	};

#pragma pack(pop)

	static_assert(sizeof(SDKMESH_VERTEX_BUFFER_HEADER::VertexElement) == 8, "Vertex element structure size incorrect");
	static_assert(sizeof(SDKMESH_HEADER) == 104, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_VERTEX_BUFFER_HEADER) == 288, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_INDEX_BUFFER_HEADER) == 32, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_MESH) == 224, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_SUBSET) == 144, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_FRAME) == 184, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKMESH_MATERIAL) == 1256, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKANIMATION_FILE_HEADER) == 40, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKANIMATION_DATA) == 40, "SDK Mesh structure size incorrect");
	static_assert(sizeof(SDKANIMATION_FRAME_DATA) == 112, "SDK Mesh structure size incorrect");

	struct TextureCacheEntry
	{
		std::shared_ptr<ResourceBase> Texture;
		uint8_t AlphaMode;
	};
	using TextureCache = std::shared_ptr<std::unordered_map<std::string, TextureCacheEntry>>;

	//--------------------------------------------------------------------------------------
	// SDKMesh class. This class reads the sdkmesh file format for use by the samples
	//--------------------------------------------------------------------------------------
	class SDKMesh
	{
	public:
		SDKMesh();
		virtual ~SDKMesh();

		virtual bool Create(const Device &device, const wchar_t *szFileName, const TextureCache &textureCache);
		virtual bool Create(const Device &device, uint8_t *pData, const TextureCache &textureCache,
			size_t dataBytes, bool bCopyStatic = false);
		virtual bool LoadAnimation(const wchar_t *szFileName);
		virtual void Destroy();

		//Frame manipulation
		void TransformBindPose(DirectX::CXMMATRIX world);
		void TransformMesh(DirectX::CXMMATRIX world, double fTime);

		// Helpers (Graphics API specific)
		static PrimitiveTopology GetPrimitiveType(SDKMESH_PRIMITIVE_TYPE PrimType);
		Format GetIBFormat(uint32_t iMesh) const;

		SDKMESH_INDEX_TYPE	GetIndexType(uint32_t iMesh) const;

		Descriptor			GetVertexBufferSRV(uint32_t iMesh, uint32_t iVB) const;
		VertexBufferView	GetVertexBufferView(uint32_t iMesh, uint32_t iVB) const;
		IndexBufferView		GetIndexBufferView(uint32_t iMesh) const;
		IndexBufferView		GetAdjIndexBufferView(uint32_t iMesh) const;

		Descriptor			GetVertexBufferSRVAt(uint32_t iVB) const;
		VertexBufferView	GetVertexBufferViewAt(uint32_t iVB) const;
		IndexBufferView		GetIndexBufferViewAt(uint32_t iIB) const;

		// Helpers (general)
		const char			*GetMeshPathA() const;
		const wchar_t		*GetMeshPathW() const;
		uint32_t			GetNumMeshes() const;
		uint32_t			GetNumMaterials() const;
		uint32_t			GetNumVertexBuffers() const;
		uint32_t			GetNumIndexBuffers() const;

		uint8_t				*GetRawVerticesAt(uint32_t iVB) const;
		uint8_t				*GetRawIndicesAt(uint32_t iIB) const;

		SDKMESH_MATERIAL	*GetMaterial(uint32_t iMaterial) const;
		SDKMESH_MESH		*GetMesh(uint32_t iMesh) const;
		uint32_t			GetNumSubsets(uint32_t iMesh) const;
		uint32_t			GetNumSubsets(uint32_t iMesh, SubsetFlags materialType) const;
		SDKMESH_SUBSET		*GetSubset(uint32_t iMesh, uint32_t iSubset) const;
		SDKMESH_SUBSET		*GetSubset(uint32_t iMesh, uint32_t iSubset, SubsetFlags materialType) const;
		uint32_t			GetVertexStride(uint32_t iMesh, uint32_t iVB) const;
		uint32_t			GetNumFrames() const;
		SDKMESH_FRAME		*GetFrame(uint32_t iFrame) const;
		SDKMESH_FRAME		*FindFrame(const char *szName) const;
		uint32_t			FindFrameIndex(const char *szName) const;
		uint64_t			GetNumVertices(uint32_t iMesh, uint32_t iVB) const;
		uint64_t			GetNumIndices(uint32_t iMesh) const;
		DirectX::XMVECTOR	GetMeshBBoxCenter(uint32_t iMesh) const;
		DirectX::XMVECTOR	GetMeshBBoxExtents(uint32_t iMesh) const;
		uint32_t			GetOutstandingResources() const;
		uint32_t			GetOutstandingBufferResources() const;
		bool				CheckLoadDone();
		bool				IsLoaded() const;
		bool				IsLoading() const;
		void				SetLoading(bool bLoading);
		bool				HadLoadingError() const;

		// Animation
		uint32_t			GetNumInfluences(uint32_t iMesh) const;
		DirectX::XMMATRIX	GetMeshInfluenceMatrix(uint32_t iMesh, uint32_t iInfluence) const;
		uint32_t			GetAnimationKeyFromTime(double fTime) const;
		DirectX::XMMATRIX	GetWorldMatrix(uint32_t iFrameIndex) const;
		DirectX::XMMATRIX	GetInfluenceMatrix(uint32_t iFrameIndex) const;
		DirectX::XMMATRIX	GetBindMatrix(uint32_t iFrameIndex) const;
		bool				GetAnimationProperties(uint32_t *pNumKeys, float *pFrameTime) const;

	protected:
		void loadMaterials(const GraphicsCommandList &commandList, SDKMESH_MATERIAL *pMaterials,
			uint32_t NumMaterials, std::vector<Resource> &uploaders);

		bool createVertexBuffer(const GraphicsCommandList &commandList, std::vector<Resource> &uploaders);
		bool createIndexBuffer(const GraphicsCommandList &commandList, std::vector<Resource> &uploaders);

		virtual bool createFromFile(const Device &device, const wchar_t *szFileName, const TextureCache &textureCache);
		virtual bool createFromMemory(const Device &device, uint8_t *pData, const TextureCache &textureCache,
			size_t dataBytes, bool bCopyStatic);

		void classifyMaterialType();
		bool executeCommandList(const GraphicsCommandList &commandList);

		// Frame manipulation
		void transformBindPoseFrame(uint32_t iFrame, DirectX::CXMMATRIX parentWorld);
		void transformFrame(uint32_t iFrame, DirectX::CXMMATRIX parentWorld, double fTime);
		void transformFrameAbsolute(uint32_t iFrame, double fTime);

		// These are the pointers to the two chunks of data loaded in from the mesh file
		uint8_t							*m_pStaticMeshData;
		std::vector<uint8_t>			m_pHeapData;
		std::vector<uint8_t>			m_animation;
		std::vector<uint8_t*>			m_vertices;
		std::vector<uint8_t*>			m_indices;

		// Keep track of the path
		std::wstring					m_strPathW;
		std::string						m_strPath;

		// General mesh info
		SDKMESH_HEADER					*m_pMeshHeader;
		SDKMESH_VERTEX_BUFFER_HEADER	*m_pVertexBufferArray;
		SDKMESH_INDEX_BUFFER_HEADER		*m_pIndexBufferArray;
		SDKMESH_MESH					*m_pMeshArray;
		SDKMESH_SUBSET					*m_pSubsetArray;
		SDKMESH_FRAME					*m_pFrameArray;
		SDKMESH_MATERIAL				*m_pMaterialArray;

		VertexBuffer					m_vertexBuffer;
		IndexBuffer						m_indexBuffer;
		IndexBuffer						m_adjIndexBuffer;

		// Classified subsets
		std::vector<std::vector<uint32_t>> m_classifiedSubsets[NUM_SUBSET_TYPE];

		// Texture cache
		TextureCache					m_textureCache;

		// Adjacency information (not part of the m_pStaticMeshData, so it must be created and destroyed separately )
		SDKMESH_INDEX_BUFFER_HEADER		*m_pAdjIndexBufferArray;

		// Animation
		SDKANIMATION_FILE_HEADER		*m_pAnimationHeader;
		SDKANIMATION_FRAME_DATA			*m_pAnimationFrameData;
		std::vector<DirectX::XMFLOAT4X4> m_bindPoseFrameMatrices;
		std::vector<DirectX::XMFLOAT4X4> m_transformedFrameMatrices;
		std::vector<DirectX::XMFLOAT4X4> m_worldPoseFrameMatrices;

	private:
		Device m_device;
		uint32_t m_numOutstandingResources;
		bool m_isLoading;
	};
}
