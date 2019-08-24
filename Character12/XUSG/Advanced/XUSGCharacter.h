//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSGComputeState.h"
#include "XUSGModel.h"

namespace XUSG
{
	class Character :
		public Model
	{
	public:
		struct Vertex
		{
			DirectX::XMFLOAT3	Pos;
			DirectX::XMUINT3	NormTex;
			DirectX::XMUINT4	TanBiNrm;
		};

		struct MeshLink
		{
			std::wstring		MeshName;
			std::string			BoneName;
			uint32_t			BoneIndex;
		};

		Character(const Device& device, const wchar_t* name = nullptr);
		virtual ~Character();

		bool Init(const InputLayout& inputLayout,
			const std::shared_ptr<SDKMesh>& mesh,
			const std::shared_ptr<ShaderPool>& shaderPool,
			const std::shared_ptr<Graphics::PipelineCache>& graphicsPipelineCache,
			const std::shared_ptr<Compute::PipelineCache>& computePipelineCache,
			const std::shared_ptr<PipelineLayoutCache>& pipelineLayoutCache,
			const std::shared_ptr<DescriptorTableCache>& descriptorTableCache,
			const std::shared_ptr<std::vector<SDKMesh>>& linkedMeshes = nullptr,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			const Format* rtvFormats = nullptr, uint32_t numRTVs = 0,
			Format dsvFormat = Format::UNKNOWN, Format shadowFormat = Format::UNKNOWN);
		void InitPosition(const DirectX::XMFLOAT4& posRot);
		void Update(uint8_t frameIndex, double time);
		void Update(uint8_t frameIndex, double time, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX* pWorld = nullptr, DirectX::FXMMATRIX* pShadowView = nullptr,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true);
		virtual void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX* pWorld = nullptr,
			DirectX::FXMMATRIX* pShadowView = nullptr, DirectX::FXMMATRIX* pShadows = nullptr,
			uint8_t numShadows = 0, bool isTemporal = true);
		void SetSkinningPipeline(const CommandList& commandList);
		void Skinning(const CommandList& commandList, uint32_t& numBarriers,
			ResourceBarrier* pBarriers, bool reset = false);
		void RenderTransformed(const CommandList& commandList, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags = SUBSET_FULL, uint8_t matrixTableIndex = CBV_MATRICES,
			uint32_t numInstances = 1);

		const DirectX::XMFLOAT4& GetPosition() const;
		DirectX::FXMMATRIX GetWorldMatrix() const;

		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device& device, const std::wstring& meshFileName,
			const std::wstring& animFileName, const TextureCache& textureCache,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			std::vector<SDKMesh>* pLinkedMeshes = nullptr);

	protected:
		enum InternalDescriptorTableSlot : uint8_t
		{
			INPUT,
			OUTPUT,
			SAMPLERS = BASE_SLOT,
			MATERIAL,
			SHADOW_ALPHA_REF,
#if TEMPORAL
			HISTORY,
#endif
			PER_FRAME
		};

		bool createTransformedStates();
		bool createTransformedVBs(VertexBuffer& vertexBuffer, ResourceState state = ResourceState::COMMON);
		bool createBuffers();
		bool createPipelineLayouts();
		bool createPipelines(const InputLayout& inputLayout, const Format* rtvFormats,
			uint32_t numRTVs, Format dsvFormat, Format shadowFormat);
		bool createDescriptorTables();
		virtual void setLinkedMatrices(uint32_t mesh, DirectX::CXMMATRIX viewProj,
			DirectX::CXMMATRIX world, DirectX::FXMMATRIX* pShadowView,
			DirectX::FXMMATRIX* pShadows, uint8_t numShadows, bool isTemporal);
		void skinning(const CommandList& commandList, bool reset);
		void renderTransformed(const CommandList& commandList, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances);
		void renderLinked(uint32_t mesh, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout, uint32_t numInstances);
		void setSkeletalMatrices(uint32_t numMeshes);
		void setBoneMatrices(uint32_t mesh);
		void convertToDQ(DirectX::XMFLOAT4& dqTran, DirectX::CXMVECTOR quat,
			const DirectX::XMFLOAT3& tran) const;
		DirectX::FXMMATRIX getDualQuat(uint32_t mesh, uint32_t influence) const;

		std::shared_ptr<Compute::PipelineCache> m_computePipelineCache;

		VertexBuffer m_transformedVBs[FrameCount];
		DirectX::XMFLOAT4X4	m_mWorld;
		DirectX::XMFLOAT4	m_vPosRot;

		double m_time;

		StructuredBuffer m_boneWorlds[FrameCount];

		PipelineLayout	m_skinningPipelineLayout;
		Pipeline		m_skinningPipeline;
		std::vector<DescriptorTable> m_srvSkinningTables[FrameCount];
		std::vector<DescriptorTable> m_uavSkinningTables[FrameCount];
#if TEMPORAL
		std::vector<DescriptorTable> m_srvSkinnedTables[FrameCount];

		std::vector<DirectX::XMFLOAT4X4> m_linkedWorldViewProjs[FrameCount];
#endif

		std::shared_ptr<std::vector<SDKMesh>>	m_linkedMeshes;
		std::shared_ptr<std::vector<MeshLink>>	m_meshLinks;

		std::vector<ConstantBuffer> m_cbLinkedMatrices;
		std::vector<ConstantBuffer> m_cbLinkedShadowMatrices;

	public:
		enum DescriptorTableSlot : uint8_t
		{
			SHADOW_MAP	= SHADOW_ALPHA_REF,
			ALPHA_REF	= SHADOW_ALPHA_REF,
			IMMUATABLE	= PER_FRAME + 1
		};
	};
}
