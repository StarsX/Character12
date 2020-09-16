//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"
#include "XUSGShaderCommon.h"
#include "XUSGSharedConst.h"

#define MAX_SHADOW_CASCADES	8

namespace XUSG
{
	//--------------------------------------------------------------------------------------
	// DDS loader
	//--------------------------------------------------------------------------------------
	namespace DDS
	{
		enum AlphaMode : uint8_t
		{
			ALPHA_MODE_UNKNOWN,
			ALPHA_MODE_STRAIGHT,
			ALPHA_MODE_PREMULTIPLIED,
			ALPHA_MODE_OPAQUE,
			ALPHA_MODE_CUSTOM
		};

		class DLL_INTERFACE Loader
		{
		public:
			Loader();
			virtual ~Loader();

			bool CreateTextureFromMemory(const Device& device, CommandList* pCommandList, const uint8_t* ddsData,
				size_t ddsDataSize, size_t maxsize, bool forceSRGB, ResourceBase::sptr& texture, Resource& uploader,
				AlphaMode* alphaMode = nullptr, ResourceState state = ResourceState::COMMON);

			bool CreateTextureFromFile(const Device& device, CommandList* pCommandList, const wchar_t* fileName,
				size_t maxsize, bool forceSRGB, ResourceBase::sptr& texture, Resource& uploader,
				AlphaMode* alphaMode = nullptr, ResourceState state = ResourceState::COMMON);

			static size_t BitsPerPixel(Format fmt);
		};
	}

	//--------------------------------------------------------------------------------------
	// SDKMesh class. This class reads the sdkmesh file format
	//--------------------------------------------------------------------------------------
	enum SubsetFlags : uint8_t
	{
		SUBSET_OPAQUE = 0x1,
		SUBSET_ALPHA = 0x2,
		SUBSET_ALPHA_TEST = 0x4,
		SUBSET_REFLECTED = 0x8,
		SUBSET_FULL = SUBSET_OPAQUE | SUBSET_ALPHA | SUBSET_ALPHA_TEST,

		NUM_SUBSET_TYPE = 2
	};

	DEFINE_ENUM_FLAG_OPERATORS(SubsetFlags);

	struct TextureCacheEntry
	{
		ResourceBase::sptr Texture;
		uint8_t AlphaMode;
	};
	using TextureCache = std::shared_ptr<std::map<std::string, TextureCacheEntry>>;

	class DLL_INTERFACE SDKMesh
	{
	public:
		static const auto MAX_VERTEX_STREAMS	= 16u;
		static const auto MAX_FRAME_NAME		= 100u;
		static const auto MAX_MESH_NAME			= 100u;
		static const auto MAX_SUBSET_NAME		= 100u;
		static const auto MAX_MATERIAL_NAME		= 100u;
		static const auto MAX_TEXTURE_NAME		= MAX_PATH;
		static const auto MAX_MATERIAL_PATH		= MAX_PATH;

		enum IndexType
		{
			IT_16BIT = 0,
			IT_32BIT
		};

		enum PrimitiveType
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

#pragma pack(push, 8)
		struct Data
		{
			char Name[MAX_MESH_NAME];
			uint8_t NumVertexBuffers;
			uint32_t VertexBuffers[MAX_VERTEX_STREAMS];
			uint32_t IndexBuffer;
			uint32_t NumSubsets;
			uint32_t NumFrameInfluences; // aka bones

			DirectX::XMFLOAT3 BoundingBoxCenter;
			DirectX::XMFLOAT3 BoundingBoxExtents;

			union
			{
				uint64_t SubsetOffset;			// Offset to list of subsets (This also forces the union to 64bits)
				uint32_t* pSubsets;				// Pointer to list of subsets
			};
			union
			{
				uint64_t FrameInfluenceOffset;	// Offset to list of frame influences (This also forces the union to 64bits)
				uint32_t* pFrameInfluences;		// Pointer to list of frame influences
			};
		};

		struct Subset
		{
			char Name[MAX_SUBSET_NAME];
			uint32_t MaterialID;
			uint32_t PrimitiveType;
			uint64_t IndexStart;
			uint64_t IndexCount;
			uint64_t VertexStart;
			uint64_t VertexCount;
		};

		struct Frame
		{
			char Name[MAX_FRAME_NAME];
			uint32_t Mesh;
			uint32_t ParentFrame;
			uint32_t ChildFrame;
			uint32_t SiblingFrame;
			DirectX::XMFLOAT4X4 Matrix;
			uint32_t AnimationDataIndex;		// Used to index which set of keyframes transforms this frame
		};

		struct Material
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
				ResourceBase* pAlbedo;
			};
			union
			{
				uint64_t Normal64;			//Force the union to 64bits
				ResourceBase* pNormal;
			};
			union
			{
				uint64_t Specular64;		//Force the union to 64bits
				ResourceBase* pSpecular;
			};
			uint64_t AlphaModeAlbedo;		// Force the union to 64bits
			uint64_t AlphaModeNormal;		// Force the union to 64bits
			uint64_t AlphaModeSpecular;		// Force the union to 64bits
		};

		struct AnimationFileHeader
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

		struct AnimationData
		{
			DirectX::XMFLOAT3 Translation;
			DirectX::XMFLOAT4 Orientation;
			DirectX::XMFLOAT3 Scaling;
		};

		struct AnimationFrameData
		{
			char FrameName[MAX_FRAME_NAME];
			union
			{
				uint64_t DataOffset;
				AnimationData* pAnimationData;
			};
		};
#pragma pack(pop)

		virtual ~SDKMesh() {};

		virtual bool Create(const Device& device, const wchar_t* fileName,
			const TextureCache& textureCache, bool isStaticMesh = false) = 0;
		virtual bool Create(const Device& device, uint8_t* pData, const TextureCache& textureCache,
			size_t dataBytes, bool isStaticMesh = false, bool copyStatic = false) = 0;
		virtual bool LoadAnimation(const wchar_t* fileName) = 0;
		virtual void Destroy() = 0;

		//Frame manipulation
		virtual void TransformBindPose(DirectX::CXMMATRIX world) = 0;
		virtual void TransformMesh(DirectX::CXMMATRIX world, double time) = 0;

		// Helpers (Graphics API specific)
		static PrimitiveTopology GetPrimitiveType(PrimitiveType primType);
		virtual Format GetIBFormat(uint32_t mesh) const = 0;

		virtual IndexType			GetIndexType(uint32_t mesh) const = 0;

		virtual Descriptor			GetVertexBufferSRV(uint32_t mesh, uint32_t i) const = 0;
		virtual VertexBufferView	GetVertexBufferView(uint32_t mesh, uint32_t i) const = 0;
		virtual IndexBufferView		GetIndexBufferView(uint32_t mesh) const = 0;
		virtual IndexBufferView		GetAdjIndexBufferView(uint32_t mesh) const = 0;

		virtual Descriptor			GetVertexBufferSRVAt(uint32_t vb) const = 0;
		virtual VertexBufferView	GetVertexBufferViewAt(uint32_t vb) const = 0;
		virtual IndexBufferView		GetIndexBufferViewAt(uint32_t ib) const = 0;

		// Helpers (general)
		virtual const char*			GetMeshPathA() const = 0;
		virtual const wchar_t*		GetMeshPathW() const = 0;
		virtual uint32_t			GetNumMeshes() const = 0;
		virtual uint32_t			GetNumMaterials() const = 0;
		virtual uint32_t			GetNumVertexBuffers() const = 0;
		virtual uint32_t			GetNumIndexBuffers() const = 0;

		virtual uint8_t*			GetRawVerticesAt(uint32_t vb) const = 0;
		virtual uint8_t*			GetRawIndicesAt(uint32_t ib) const = 0;

		virtual Material*			GetMaterial(uint32_t material) const = 0;
		virtual Data*				GetMesh(uint32_t mesh) const = 0;
		virtual uint32_t			GetNumSubsets(uint32_t mesh) const = 0;
		virtual uint32_t			GetNumSubsets(uint32_t mesh, SubsetFlags materialType) const = 0;
		virtual Subset*				GetSubset(uint32_t mesh, uint32_t subset) const = 0;
		virtual Subset*				GetSubset(uint32_t mesh, uint32_t subset, SubsetFlags materialType) const = 0;
		virtual uint32_t			GetVertexStride(uint32_t mesh, uint32_t i) const = 0;
		virtual uint32_t			GetNumFrames() const = 0;
		virtual Frame*				GetFrame(uint32_t frame) const = 0;
		virtual Frame*				FindFrame(const char* name) const = 0;
		virtual uint32_t			FindFrameIndex(const char* name) const = 0;
		virtual uint64_t			GetNumVertices(uint32_t mesh, uint32_t i) const = 0;
		virtual uint64_t			GetNumIndices(uint32_t mesh) const = 0;
		virtual DirectX::XMVECTOR	GetMeshBBoxCenter(uint32_t mesh) const = 0;
		virtual DirectX::XMVECTOR	GetMeshBBoxExtents(uint32_t mesh) const = 0;
		virtual uint32_t			GetOutstandingResources() const = 0;
		virtual uint32_t			GetOutstandingBufferResources() const = 0;
		virtual bool				CheckLoadDone() = 0;
		virtual bool				IsLoaded() const = 0;
		virtual bool				IsLoading() const = 0;
		virtual void				SetLoading(bool loading) = 0;
		virtual bool				HadLoadingError() const = 0;

		// Animation
		virtual uint32_t			GetNumInfluences(uint32_t mesh) const = 0;
		virtual DirectX::XMMATRIX	GetMeshInfluenceMatrix(uint32_t mesh, uint32_t influence) const = 0;
		virtual uint32_t			GetAnimationKeyFromTime(double time) const = 0;
		virtual DirectX::XMMATRIX	GetWorldMatrix(uint32_t frameIndex) const = 0;
		virtual DirectX::XMMATRIX	GetInfluenceMatrix(uint32_t frameIndex) const = 0;
		virtual DirectX::XMMATRIX	GetBindMatrix(uint32_t frameIndex) const = 0;
		virtual bool				GetAnimationProperties(uint32_t* pNumKeys, float* pFrameTime) const = 0;

		using uptr = std::unique_ptr<SDKMesh>;
		using sptr = std::shared_ptr<SDKMesh>;

		static uptr MakeUnique();
		static sptr MakeShared();
	};

	//--------------------------------------------------------------------------------------
	// Model base
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Model
	{
	public:
		enum PipelineLayoutIndex : uint8_t
		{
			BASE_PASS,
			DEPTH_PASS,
			DEPTH_ALPHA_PASS,
			GLOBAL_BASE_PASS,
			GLOBAL_DEPTH_PASS,
			GLOBAL_DEPTH_ALPHA_PASS,

			NUM_PIPELINE_LAYOUT
		};

		enum PipelineIndex : uint8_t
		{
			OPAQUE_FRONT,
			OPAQUE_FRONT_EQUAL,
			OPAQUE_TWO_SIDED,
			OPAQUE_TWO_SIDED_EQUAL,
			ALPHA_TWO_SIDED,
			DEPTH_FRONT,
			DEPTH_TWO_SIDED,
			SHADOW_FRONT,
			SHADOW_TWO_SIDED,
			REFLECTED,

			NUM_PIPELINE
		};

		enum DescriptorTableSlot : uint8_t
		{
			MATRICES,
#if TEMPORAL_AA
			TEMPORAL_BIAS,
#endif
			VARIABLE_SLOT
		};

		enum DescriptorTableSlotOffset : uint8_t
		{
			SAMPLERS_OFFSET,
			MATERIAL_OFFSET,
			IMMUTABLE_OFFSET,
			ALPHA_REF_OFFSET = IMMUTABLE_OFFSET
		};

		enum CBVTableIndex : uint8_t
		{
			CBV_MATRICES,
			CBV_SHADOW_MATRIX,

			NUM_CBV_TABLE = CBV_SHADOW_MATRIX + MAX_SHADOW_CASCADES
		};

		//Model(const Device& device, const wchar_t* name);
		virtual ~Model() {};

		virtual bool Init(const InputLayout& inputLayout, const SDKMesh::sptr& mesh,
			const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& pipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache) = 0;
		virtual void Update(uint8_t frameIndex) = 0;
		virtual void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::CXMMATRIX world,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true) = 0;
		virtual void SetPipelineLayout(const CommandList* pCommandList, PipelineLayoutIndex layout) = 0;
		virtual void SetPipeline(const CommandList* pCommandList, PipelineIndex pipeline) = 0;
		virtual void SetPipeline(const CommandList* pCommandList, SubsetFlags subsetFlags, PipelineLayoutIndex layout) = 0;
		virtual void Render(const CommandList* pCommandList, SubsetFlags subsetFlags, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout = NUM_PIPELINE_LAYOUT, uint32_t numInstances = 1) = 0;

		Model* AsModel();

		static InputLayout CreateInputLayout(Graphics::PipelineCache& pipelineCache);
		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device& device, const std::wstring& meshFileName,
			const TextureCache& textureCache, bool isStaticMesh);

		static constexpr uint32_t GetFrameCount() { return FrameCount; }

		using uptr = std::unique_ptr<Model>;
		using sptr = std::shared_ptr<Model>;

		static uptr MakeUnique(const Device& device, const wchar_t* name);
		static sptr MakeShared(const Device& device, const wchar_t* name);

	protected:
		static const uint32_t FrameCount = FRAME_COUNT;
	};

	//--------------------------------------------------------------------------------------
	// Character model
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Character :
		public virtual Model
	{
	public:
		enum DescriptorTableSlot : uint8_t
		{
			SAMPLERS = VARIABLE_SLOT,
			MATERIAL,
			IMMUTABLE,
			PER_FRAME,
			ALPHA_REF = IMMUTABLE,
#if TEMPORAL
			HISTORY = IMMUTABLE
#endif
		};

		struct MeshLink
		{
			std::wstring		MeshName;
			std::string			BoneName;
			uint32_t			BoneIndex;
		};

		//Character(const Device& device, const wchar_t* name = nullptr);
		virtual ~Character() {};

		virtual bool Init(const InputLayout& inputLayout,
			const SDKMesh::sptr& mesh, const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& graphicsPipelineCache,
			const Compute::PipelineCache::sptr& computePipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			const std::shared_ptr<std::vector<SDKMesh>>& linkedMeshes = nullptr,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			const Format* rtvFormats = nullptr, uint32_t numRTVs = 0,
			Format dsvFormat = Format::UNKNOWN, Format shadowFormat = Format::UNKNOWN) = 0;
		virtual void InitPosition(const DirectX::XMFLOAT4& posRot) = 0;
		virtual void Update(uint8_t frameIndex, double time) = 0;
		virtual void Update(uint8_t frameIndex, double time, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX* pWorld = nullptr, DirectX::FXMMATRIX* pShadows = nullptr,
			uint8_t numShadows = 0, bool isTemporal = true) = 0;
		virtual void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX* pWorld = nullptr,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true) = 0;
		virtual void SetSkinningPipeline(const CommandList* pCommandList) = 0;
		virtual void Skinning(const CommandList* pCommandList, uint32_t& numBarriers,
			ResourceBarrier* pBarriers, bool reset = false) = 0;
		virtual void RenderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags = SUBSET_FULL, uint8_t matrixTableIndex = CBV_MATRICES,
			uint32_t numInstances = 1) = 0;

		virtual const DirectX::XMFLOAT4& GetPosition() const = 0;
		virtual DirectX::FXMMATRIX GetWorldMatrix() const = 0;

		static SDKMesh::sptr LoadSDKMesh(const Device& device, const std::wstring& meshFileName,
			const std::wstring& animFileName, const TextureCache& textureCache,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			std::vector<SDKMesh::sptr>* pLinkedMeshes = nullptr);

		using uptr = std::unique_ptr<Character>;
		using sptr = std::shared_ptr<Character>;

		static uptr MakeUnique(const Device& device, const wchar_t* name = nullptr);
		static sptr MakeShared(const Device& device, const wchar_t* name = nullptr);
	};

	//--------------------------------------------------------------------------------------
	// Static model
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE StaticModel :
		public virtual Model
	{
	public:
		enum DescriptorTableSlot : uint8_t
		{
			PER_OBJECT = VARIABLE_SLOT,
			PER_FRAME,
			SAMPLERS,
			MATERIAL,
			SHADOW_MAP,
			IMMUATABLE,
			ALPHA_REF = SHADOW_MAP
		};

		//StaticModel(const Device& device, const wchar_t* name = nullptr);
		virtual ~StaticModel() {};

		virtual bool Init(const InputLayout& inputLayout, const SDKMesh::sptr& mesh,
			const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& pipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			const Format* rtvFormats = nullptr, uint32_t numRTVs = 0,
			Format dsvFormat = Format::UNKNOWN, Format shadowFormat = Format::UNKNOWN) = 0;
		//virtual void CreateBoundCBuffer() = 0;
		virtual void Update(uint8_t frameIndex) = 0;
		virtual void Update(uint8_t frameIndex, DirectX::CXMMATRIX viewProj, DirectX::CXMMATRIX world,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true) = 0;
		virtual void Render(const CommandList* pCommandList, uint32_t mesh, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags = SUBSET_FULL, uint8_t matrixTableIndex = CBV_MATRICES,
			uint32_t numInstances = 1) = 0;
		//virtual void RenderBoundary(uint32_t mesh, const DirectX::XMFLOAT4* pTBox) = 0;

		virtual const SDKMesh::sptr& GetMesh() const = 0;

		static SDKMesh::sptr LoadSDKMesh(const Device& device, const std::wstring& meshFileName,
			const TextureCache& textureCache);

		using uptr = std::unique_ptr<StaticModel>;
		using sptr = std::shared_ptr<StaticModel>;

		static uptr MakeUnique(const Device& device, const wchar_t* name = nullptr);
		static sptr MakeShared(const Device& device, const wchar_t* name = nullptr);
	};

	//--------------------------------------------------------------------------------------
	// Octree
	//--------------------------------------------------------------------------------------
	class OctNode;
	using Octree = std::unique_ptr<OctNode>;
	class DLL_INTERFACE OctNode
	{
	public:
		enum Visibility : uint8_t
		{
			VISIBILITY_INTERSECT,
			VISIBILITY_INSIDE,
			VISIBILITY_OUTSIDE
		};

		//OctNode(uint32_t numModels, const StaticModel::sptr* pModels, const DirectX::XMFLOAT3& eyePt);
		virtual ~OctNode() {};

		virtual void CreateTree(SubsetFlags subsetFlags) = 0;
		virtual void Init(const DirectX::XMFLOAT3& center, float diameter) = 0;
		virtual void Sort(std::vector<DirectX::XMUINT2>& meshIDQueue,
			DirectX::CXMMATRIX viewProj, bool isNearToFar, bool isAllVisible = false) const = 0;
		virtual void TraverseBoundary(DirectX::CXMMATRIX viewProj, bool isNearToFar,
			bool allVisible, uint8_t begin, uint8_t logSize) const = 0;	// 0x, 1y, 2z
		virtual void RenderBoundary(DirectX::CXMMATRIX viewProj, bool isNearToFar, bool isAllVisible) const = 0;

		static void SetLooseCoeff(float looseCoeff);
		static void Sort(const Octree* pOctTrees, std::vector<DirectX::XMUINT2>* pMeshIDQueues,
			uint8_t opaqueQueue, uint8_t alphaQueue, DirectX::CXMMATRIX viewProj,
			DirectX::CXMMATRIX viewProjI, bool isAlphaQueueN2F);
		static void ComputeFrustumBound(DirectX::CXMMATRIX viewProjI);

		using uptr = std::unique_ptr<OctNode>;
		using sptr = std::shared_ptr<OctNode>;

		static uptr MakeUnique(uint32_t numModels, const StaticModel::sptr* pModels, const DirectX::XMFLOAT3& eyePt);
		static sptr MakeShared(uint32_t numModels, const StaticModel::sptr* pModels, const DirectX::XMFLOAT3& eyePt);
	};

	//--------------------------------------------------------------------------------------
	// Shadow
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Shadow
	{
	public:
		//Shadow(const Device& device);
		virtual ~Shadow() {};

		// This runs when the application is initialized.
		virtual bool Init(float sceneMapSize, float shadowMapSize,
			const DescriptorTableCache::sptr& descriptorTableCache,
			uint8_t numCasLevels = NUM_CASCADE) = 0;
		virtual bool CreateDescriptorTables() = 0;
		// This runs per frame. This data could be cached when the cameras do not move.
		virtual void Update(uint8_t frameIndex, const DirectX::XMFLOAT4X4& view,
			const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT4& lightPt) = 0;

		virtual void SetViewport(const CommandList* pCommandList, uint8_t i) = 0;
		virtual void GetShadowMatrices(DirectX::XMMATRIX* pShadows) const = 0;

		virtual const DepthStencil::uptr& GetShadowMap() const = 0;
		virtual const DescriptorTable& GetShadowTable() const = 0;
		virtual const Framebuffer& GetFramebuffer() const = 0;
		virtual DirectX::FXMMATRIX GetShadowMatrix(uint8_t i) const = 0;
		virtual DirectX::FXMMATRIX GetShadowViewMatrix() const = 0;

		using uptr = std::unique_ptr<Shadow>;
		using sptr = std::shared_ptr<Shadow>;

		static uptr MakeUnique(const Device& device);
		static sptr MakeShared(const Device& device);
	};

	//--------------------------------------------------------------------------------------
	// Nature objects 
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Nature
	{
	public:
		//Nature(const Device& device);
		virtual ~Nature() {};

		virtual bool Init(CommandList* pCommandList, const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& graphicsPipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			const std::wstring& skyTexture, std::vector<Resource>& uploaders,
			bool renderWater, Format rtvFormat = Format::R11G11B10_FLOAT,
			Format dsvFormat = Format::D24_UNORM_S8_UINT) = 0;
		virtual bool CreateResources(ResourceBase* pSceneColor, const DepthStencil& depth) = 0;

		virtual void Update(uint8_t frameIndex, DirectX::FXMMATRIX* pViewProj, DirectX::FXMMATRIX* pWorld = nullptr) = 0;
		virtual void SetGlobalCBVTables(DescriptorTable cbvImmutable, DescriptorTable cbvPerFrameTable) = 0;
		virtual void RenderSky(const CommandList* pCommandList, bool reset = false) = 0;
		virtual void RenderWater(const CommandList* pCommandList, const Framebuffer& framebuffer,
			uint32_t& numBarriers, ResourceBarrier* pBarriers, bool reset = false) = 0;

		virtual Descriptor GetSkySRV() const = 0;

		using uptr = std::unique_ptr<Nature>;
		using sptr = std::shared_ptr<Nature>;

		static uptr MakeUnique(const Device& device);
		static sptr MakeShared(const Device& device);
	};

	//--------------------------------------------------------------------------------------
	// Scene
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Scene
	{
	public:
		enum GBufferIndex : uint8_t
		{
			ALBEDO_IDX,
			NORMAL_IDX,
			RGHMTL_IDX,
#if TEMPORAL
			MOTION_IDX,
#endif
			AO_IDX,

			NUM_GBUFFER,
			NUM_GB_FIXED = RGHMTL_IDX + 1,
			NUM_GB_RTV = AO_IDX
		};

		enum CBVTableIndex : uint8_t
		{
			CBV_IMMUTABLE,
			CBV_PER_FRAME_VS,
			CBV_PER_FRAME_PS = CBV_PER_FRAME_VS + FRAME_COUNT,	// Window size dependent
#if TEMPORAL_AA
			CBV_TEMPORAL_BIAS0 = CBV_PER_FRAME_PS + FRAME_COUNT,
			CBV_TEMPORAL_BIAS,

			NUM_CBV_TABLE = CBV_TEMPORAL_BIAS + FRAME_COUNT
#else
			NUM_CBV_TABLE = CBV_PER_FRAME_PS + FrameCount
#endif
		};

		//Scene(const Device& device);
		virtual ~Scene() {};

		virtual bool LoadAssets(void* pSceneReader, CommandList* pCommandList,
			const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& graphicsPipelineCache,
			const Compute::PipelineCache::sptr& computePipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			std::vector<Resource>& uploaders, Format rtvFormat, Format dsvFormat,
			bool useIBL = false) = 0;
		virtual bool ChangeWindowSize(CommandList* pCommandList, std::vector<Resource>& uploaders,
			RenderTarget& rtColor, DepthStencil& depth, RenderTarget& rtMasks) = 0;
		virtual bool CreateResources(CommandList* pCommandList, std::vector<Resource>& uploaders) = 0;
		virtual void Update(uint8_t frameIndex, double time, float timeStep) = 0;
		virtual void Update(uint8_t frameIndex, double time, float timeStep,
			DirectX::CXMMATRIX view, DirectX::CXMMATRIX proj,
			DirectX::CXMVECTOR eyePt) = 0;
		virtual void Render(const CommandList* pCommandList) = 0;
		virtual void SetViewProjMatrix(DirectX::CXMMATRIX view, DirectX::CXMMATRIX proj) = 0;
		virtual void SetEyePoint(DirectX::CXMVECTOR eyePt) = 0;
		virtual void SetFocusAndDistance(DirectX::CXMVECTOR focus_dist) = 0;
		virtual void SetRenderTarget(RenderTarget& rtColor, DepthStencil& depth,
			RenderTarget& rtMasks, bool createFB = true) = 0;
		virtual void SetViewport(const Viewport& viewport, const RectRange& scissorRect) = 0;

		virtual DirectX::FXMVECTOR GetFocusAndDistance() const = 0;
		virtual const DescriptorTable& GetCBVTable(uint8_t i) const = 0;
		virtual const RenderTarget::sptr GetGBuffer(const uint8_t i) const = 0;
		//virtual const RenderTarget::sptr GetMasks() const = 0;
		
		using uptr = std::unique_ptr<Scene>;
		using sptr = std::shared_ptr<Scene>;

		static uptr MakeUnique(const Device& device);
		static sptr MakeShared(const Device& device);
	};

	//--------------------------------------------------------------------------------------
	// Postprocess
	//--------------------------------------------------------------------------------------
	class DLL_INTERFACE Postprocess
	{
	public:
		enum PipelineIndex : uint8_t
		{
			ANTIALIAS,
			POST_EFFECTS,
			RESAMPLE_LUM,
			LUM_ADAPT,
			TONE_MAP,
			UNSHARP,

			NUM_PIPELINE
		};

		enum DescriptorPoolIndex : uint8_t
		{
			IMMUTABLE_POOL,
			RESIZABLE_POOL
		};

		//Postprocess(const Device& device);
		virtual ~Postprocess() {};

		virtual bool Init(const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& graphicsPipelineCache,
			const Compute::PipelineCache::sptr& computePipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			Format hdrFormat, Format ldrFormat) = 0;
		virtual bool ChangeWindowSize(const RenderTarget& refRT) = 0;

		virtual void Update(const DescriptorTable& cbvImmutable, const DescriptorTable& cbvPerFrameTable,
			float timeStep) = 0;
		virtual void Render(const CommandList* pCommandList, RenderTarget& dst, Texture2D& src,
			const DescriptorTable& srvTable, bool clearRT = false) = 0;
		virtual void ScreenRender(const CommandList* pCommandList, PipelineIndex pipelineIndex,
			const DescriptorTable& srvTable, bool hasPerFrameCB, bool hasSampler, bool reset = false) = 0;
		virtual void LumAdaption(const CommandList* pCommandList, const DescriptorTable& uavSrvTable, bool reset = false) = 0;
		virtual void Antialias(const CommandList* pCommandList, RenderTarget** ppDsts, Texture2D** ppSrcs,
			const DescriptorTable& srvTable, uint8_t numRTVs, uint8_t numSRVs, bool reset = false) = 0;
		virtual void Unsharp(const CommandList* pCommandList, const Descriptor* pRTVs, const DescriptorTable& srvTable,
			uint8_t numRTVs = 1, bool reset = false) = 0;

		virtual DescriptorTable CreateTemporalAASRVTable(const Descriptor& srvCurrent, const Descriptor& srvPrevious,
			const Descriptor& srvVelocity, const Descriptor& srvMasks, const Descriptor& srvMeta) = 0;

		using uptr = std::unique_ptr<Postprocess>;
		using sptr = std::shared_ptr<Postprocess>;

		static uptr MakeUnique(const Device& device);
		static sptr MakeShared(const Device& device);
	};
}
