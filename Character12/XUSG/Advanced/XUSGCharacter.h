//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "XUSGAdvanced.h"
#include "XUSGModel.h"

namespace XUSG
{
	class Character_Impl :
		public virtual Character,
		public virtual Model_Impl
	{
	public:
		Character_Impl(const Device::sptr& device, const wchar_t* name = nullptr);
		virtual ~Character_Impl();

		bool Init(const InputLayout* pInputLayout,
			const SDKMesh::sptr& mesh,
			const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& graphicsPipelineCache,
			const Compute::PipelineCache::sptr& computePipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache,
			const std::shared_ptr<std::vector<SDKMesh>>& linkedMeshes = nullptr,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			const Format* rtvFormats = nullptr, uint32_t numRTVs = 0,
			Format dsvFormat = Format::UNKNOWN, Format shadowFormat = Format::UNKNOWN);
		void InitPosition(const DirectX::XMFLOAT4& posRot);
		void Update(uint8_t frameIndex, double time);
		void Update(uint8_t frameIndex, double time, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX* pWorld = nullptr, DirectX::FXMMATRIX* pShadows = nullptr,
			uint8_t numShadows = 0, bool isTemporal = true);
		virtual void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX* pWorld = nullptr,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true);
		void SetSkinningPipeline(const CommandList* pCommandList);
		void Skinning(const CommandList* pCommandList, uint32_t& numBarriers,
			ResourceBarrier* pBarriers, bool reset = false);
		void RenderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags = SUBSET_FULL, uint8_t matrixTableIndex = CBV_MATRICES,
			uint32_t numInstances = 1);

		const DirectX::XMFLOAT4& GetPosition() const;
		DirectX::FXMMATRIX GetWorldMatrix() const;

	protected:
		enum SkinningDescriptorTableSlot : uint8_t
		{
			INPUT,
			OUTPUT
		};

		struct Vertex
		{
			DirectX::XMFLOAT3	Pos;
			DirectX::XMUINT3	NormTex;
#if HAS_TANGENTS
			DirectX::XMUINT4	TanBiNrm;
#endif
		};

		bool createTransformedStates();
		bool createTransformedVBs(VertexBuffer* pVertexBuffer);
		bool createBuffers();
		bool createPipelineLayouts();
		bool createPipelines(const InputLayout* pInputLayout, const Format* rtvFormats,
			uint32_t numRTVs, Format dsvFormat, Format shadowFormat);
		bool createDescriptorTables();
		virtual void setLinkedMatrices(uint32_t mesh, DirectX::CXMMATRIX viewProj,
			DirectX::CXMMATRIX world, DirectX::FXMMATRIX* pShadows,
			uint8_t numShadows, bool isTemporal);
		void skinning(const CommandList* pCommandList, bool reset);
		void renderTransformed(const CommandList* pCommandList, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags, uint8_t matrixTableIndex, uint32_t numInstances);
		void renderLinked(uint32_t mesh, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout, uint32_t numInstances);
		void setSkeletalMatrices(uint32_t numMeshes);
		void setBoneMatrices(uint32_t mesh);
		void convertToDQ(DirectX::XMFLOAT4& dqTran, DirectX::CXMVECTOR quat,
			const DirectX::XMFLOAT3& tran) const;
		DirectX::FXMMATRIX getDualQuat(uint32_t mesh, uint32_t influence) const;

		std::shared_ptr<Compute::PipelineCache> m_computePipelineCache;

		VertexBuffer::uptr	m_transformedVBs[FrameCount];
		DirectX::XMFLOAT4X4	m_mWorld;
		DirectX::XMFLOAT4	m_vPosRot;

		double m_time;

		StructuredBuffer::uptr m_boneWorlds[FrameCount];

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

		std::vector<ConstantBuffer::uptr> m_cbLinkedMatrices;
		std::vector<ConstantBuffer::uptr> m_cbLinkedShadowMatrices;
	};
}
