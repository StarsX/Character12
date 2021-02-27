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
		Model_Impl(const Device& device, const wchar_t* name);
		virtual ~Model_Impl();

		bool Init(const InputLayout* pInputLayout,
			const SDKMesh::sptr& mesh, const ShaderPool::sptr& shaderPool,
			const Graphics::PipelineCache::sptr& pipelineCache,
			const PipelineLayoutCache::sptr& pipelineLayoutCache,
			const DescriptorTableCache::sptr& descriptorTableCache);
		void Update(uint8_t frameIndex);
		void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::CXMMATRIX world,
			DirectX::FXMMATRIX* pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true);
#if TEMPORAL_AA
		void SetTemporalBias(const DirectX::XMFLOAT2& temporalBias);
#endif
		void SetPipelineLayout(const CommandList* pCommandList, PipelineLayoutIndex layout);
		void SetPipeline(const CommandList* pCommandList, PipelineIndex pipeline);
		void SetPipeline(const CommandList* pCommandList, SubsetFlags subsetFlags, PipelineLayoutIndex layout);
		void Render(const CommandList* pCommandList, SubsetFlags subsetFlags, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout = NUM_PIPELINE_LAYOUT, uint32_t numInstances = 1);

	protected:
		struct CBMatrices
		{
			DirectX::XMFLOAT4X4 WorldViewProj;
			DirectX::XMFLOAT3X4 World;
			DirectX::XMFLOAT3X4 WorldIT;
#if TEMPORAL
			DirectX::XMFLOAT4X4 WorldViewProjPrev;
#endif
		};

		bool createConstantBuffers();
		bool createPipelines(bool isStatic, const InputLayout* pInputLayout, const Format* rtvFormats,
			uint32_t numRTVs, Format dsvFormat, Format shadowFormat);
		bool createDescriptorTables();
		void render(const CommandList* pCommandList, uint32_t mesh, PipelineLayoutIndex layout,
			SubsetFlags subsetFlags, uint32_t numInstances);

		Util::PipelineLayout::sptr initPipelineLayout(VertexShader vs, PixelShader ps);

		Device		m_device;
		std::wstring m_name;

		uint8_t		m_currentFrame;
		uint8_t		m_previousFrame;

		uint8_t		m_variableSlot;

		SDKMesh::sptr					m_mesh;
		ShaderPool::sptr				m_shaderPool;
		Graphics::PipelineCache::sptr	m_pipelineCache;
		PipelineLayoutCache::sptr		m_pipelineLayoutCache;
		DescriptorTableCache::sptr		m_descriptorTableCache;

#if TEMPORAL
		DirectX::XMFLOAT4X4		m_worldViewProjs[FrameCount];
#endif

		ConstantBuffer::uptr	m_cbMatrices;
		ConstantBuffer::uptr	m_cbShadowMatrices;
#if TEMPORAL_AA
		ConstantBuffer::uptr	m_cbTemporalBias;
#endif

		PipelineLayout			m_pipelineLayouts[NUM_PIPELINE_LAYOUT];
		Pipeline				m_pipelines[NUM_PIPELINE];
		DescriptorTable			m_cbvTables[FrameCount][NUM_CBV_TABLE];
		DescriptorTable			m_samplerTable;
		std::vector<DescriptorTable> m_srvTables;
	};
}
