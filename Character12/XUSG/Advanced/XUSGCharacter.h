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
		Character_Impl(const wchar_t* name = nullptr);
		virtual ~Character_Impl();

		bool Init(Device* pDevice, const InputLayout* pInputLayout,
			const SDKMesh::sptr& mesh, const ShaderLib::sptr& shaderLib,
			const Graphics::PipelineLib::sptr& graphicsPipelineLib,
			const Compute::PipelineLib::sptr& computePipelineLib,
			const PipelineLayoutLib::sptr& pipelineLayoutLib,
			const DescriptorTableLib::sptr& descriptorTableLib,
			const std::shared_ptr<std::vector<SDKMesh>>& linkedMeshes = nullptr,
			const std::shared_ptr<std::vector<MeshLink>>& meshLinks = nullptr,
			uint32_t numRTVs = 0, const Format* rtvFormats = nullptr,
			Format dsvFormat = Format::UNKNOWN, Format shadowFormat = Format::UNKNOWN,
			bool twoSidedAll = false, bool useZEqual = true);
		bool CreateDescriptorTables();

		void InitPosition(const DirectX::XMFLOAT4& posRot);
		void Update(uint8_t frameIndex, double time);
		void Update(uint8_t frameIndex, double time, DirectX::FXMMATRIX* pWorld, bool isTemporal = true);
		virtual void SetMatrices(DirectX::FXMMATRIX* pWorld = nullptr, bool isTemporal = true);
		void SetSkinningPipeline(CommandList* pCommandList) const;
		void Skinning(CommandList* pCommandList, uint32_t& numBarriers,
			ResourceBarrier* pBarriers, bool reset = false) const;
		void RenderTransformed(CommandList* pCommandList, PipelineLayoutIndex layout,
			SubsetFlags subsetMask = SUBSET_FULL, const DescriptorTable* pCbvPerFrameTable = nullptr,
			uint32_t numInstances = 1) const;

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
			DirectX::XMFLOAT4	Pos;
			DirectX::XMUINT4	NormUV;
			DirectX::XMUINT4	Tangent;
		};
		static_assert(sizeof(Vertex) == 48, "Vertex stride must match 16-byte alignement");

		bool createTransformedStates(Device* pDevice);
		bool createTransformedVBs(Device* pDevice, VertexBuffer* pVertexBuffer);
		bool createBuffers(Device* pDevice);
		bool createPipelineLayouts();
		bool createPipelines(const InputLayout* pInputLayout, uint32_t numRTVs,
			const Format* rtvFormats, Format dsvFormat, Format shadowFormat, bool useZEqual);
		virtual void setLinkedMatrices(uint32_t mesh, DirectX::CXMMATRIX world, bool isTemporal);
		void skinning(CommandList* pCommandList, bool reset) const;
		void renderTransformed(CommandList* pCommandList, PipelineLayoutIndex layout,
			SubsetFlags subsetMask, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances) const;
		void renderLinked(uint32_t mesh, PipelineLayoutIndex layout, uint32_t numInstances);
		void setSkeletalMatrices(uint32_t numMeshes) const;
		void setBoneMatrices(uint32_t mesh) const;
		void convertToDQ(DirectX::XMFLOAT4& dqTran, DirectX::CXMVECTOR quat,
			const DirectX::XMFLOAT3& tran) const;
		DirectX::FXMMATRIX getDualQuat(uint32_t mesh, uint32_t influence) const;

		std::shared_ptr<Compute::PipelineLib> m_computePipelineLib;

		VertexBuffer::uptr	m_transformedVB;
		DirectX::XMFLOAT4X4	m_mWorld;
		DirectX::XMFLOAT4	m_vPosRot;

		double m_time;

		StructuredBuffer::uptr m_boneWorlds;
		uint32_t m_numBoneWorlds;
		uint8_t m_framePingPong;

		PipelineLayout	m_skinningPipelineLayout;
		Pipeline		m_skinningPipeline;
		std::vector<DescriptorTable> m_srvSkinningTables[FrameCount];
		std::vector<DescriptorTable> m_uavSkinningTables[2];
#if XUSG_TEMPORAL
		std::vector<DescriptorTable> m_srvSkinnedTables[2];

		std::vector<DirectX::XMFLOAT3X4> m_linkedWorlds;
#endif

		std::shared_ptr<std::vector<SDKMesh>>	m_linkedMeshes;
		std::shared_ptr<std::vector<MeshLink>>	m_meshLinks;

		std::vector<ConstantBuffer::uptr> m_cbLinkedMatrices;
	};
}
