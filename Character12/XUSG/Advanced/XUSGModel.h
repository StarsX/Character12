//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "XUSGAdvanced.h"

namespace XUSG
{
	class Model_Impl :
		public virtual Model
	{
	public:
		Model_Impl(const wchar_t* name, API api);
		virtual ~Model_Impl();

		bool Init(const Device* pDevice, const InputLayout* pInputLayout,
			const SDKMesh::sptr& mesh, const ShaderLib::sptr& shaderLib,
			const Graphics::PipelineLib::sptr& pipelineLib,
			const PipelineLayoutLib::sptr& pipelineLayoutLib,
			const DescriptorTableLib::sptr& descriptorTableLib,
			bool twoSidedAll);
		bool CreateDescriptorTables();

		void Update(uint8_t frameIndex);
		void SetMatrices(DirectX::CXMMATRIX world, bool isTemporal = true);
#if XUSG_TEMPORAL_AA
		void SetTemporalBias(const DirectX::XMFLOAT2& temporalBias);
#endif
		void SetPipelineLayout(const CommandList* pCommandList, PipelineLayoutIndex layout);
		void SetPipeline(const CommandList* pCommandList, PipelineIndex pipeline);
		void SetPipeline(const CommandList* pCommandList, SubsetFlags subsetFlags, PipelineLayoutIndex layout);
		void Render(const CommandList* pCommandList, SubsetFlags subsetFlags, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout = NUM_PIPELINE_LAYOUT, const DescriptorTable* pCbvPerFrameTable = nullptr,
			uint32_t numInstances = 1);

		bool IsTwoSidedAll() const;

	protected:
		struct CBMatrices
		{
			DirectX::XMFLOAT3X4 World;
			DirectX::XMFLOAT3X4 WorldIT;
#if XUSG_TEMPORAL
			DirectX::XMFLOAT3X4 WorldPrev;
#endif
		};

		bool createConstantBuffers(const Device* pDevice);
		bool createPipelines(const InputLayout* pInputLayout, const Format* rtvFormats, uint32_t numRTVs,
			Format dsvFormat, Format shadowFormat, bool isStatic, bool useZEqual);
		void render(const CommandList* pCommandList, uint32_t mesh, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags, const DescriptorTable* pCbvPerFrameTable, uint32_t numInstances);

		Util::PipelineLayout::sptr initPipelineLayout(VertexShader vs, PixelShader ps);

		API m_api;
		std::wstring m_name;

		uint8_t		m_currentFrame;
		uint8_t		m_previousFrame;

		uint8_t		m_variableSlot;

		SDKMesh::sptr					m_mesh;
		ShaderLib::sptr				m_shaderLib;
		Graphics::PipelineLib::sptr	m_graphicsPipelineLib;
		PipelineLayoutLib::sptr		m_pipelineLayoutLib;
		DescriptorTableLib::sptr		m_descriptorTableLib;

#if XUSG_TEMPORAL
		DirectX::XMFLOAT3X4		m_world;
#endif

		ConstantBuffer::uptr	m_cbMatrices;
#if XUSG_TEMPORAL_AA
		ConstantBuffer::uptr	m_cbTemporalBias;
#endif

		PipelineLayout			m_pipelineLayouts[NUM_PIPELINE_LAYOUT];
		Pipeline				m_pipelines[NUM_PIPELINE];
		DescriptorTable			m_cbvTables[FrameCount][NUM_CBV_TABLE];
		std::vector<DescriptorTable> m_srvTables;

		bool					m_twoSidedAll;
	};
}
