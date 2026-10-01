//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "XUSGAdvanced.h"

namespace XUSG
{
	class Postprocess_Impl :
		public virtual Postprocess
	{
	public:
		Postprocess_Impl(API api);
		virtual ~Postprocess_Impl();

		bool Init(const Device* pDevice, const ShaderLib::sptr& shaderLib,
			const Graphics::PipelineLib::sptr& graphicsPipelineLib,
			const Compute::PipelineLib::sptr& computePipelineLib,
			const PipelineLayoutLib::sptr& pipelineLayoutLib,
			const DescriptorTableLib::sptr& descriptorTableLib,
			Format hdrFormat, Format ldrFormat);
		bool ChangeWindowSize(const Device* pDevice, const Texture* pReference);

		void Update(const DescriptorTable& cbvImmutable, const DescriptorTable& cbvPerFrameTable,
			uint8_t frameIndex, float timeStep);
		void Render(CommandList* pCommandList, RenderTarget* pDst, Texture* pSrc,
			const DescriptorTable& srvTable, bool clearRT = false);
		void ScreenRender(const CommandList* pCommandList, PipelineIndex pipelineIndex,
			const DescriptorTable& srvTable, bool hasImmutableCB, bool hasPerFrameCB);
		void LumAdaption(const CommandList* pCommandList, const DescriptorTable& uavSrvTable);
		void Antialias(CommandList* pCommandList, uint8_t numRTVs, RenderTarget** ppDsts,
			uint8_t numSRVs, Texture** ppSrcs, const DescriptorTable& srvTable);
		void Unsharp(const CommandList* pCommandList, uint8_t numRTVs,
			const Descriptor* pRTVs, const DescriptorTable& srvTable);

		DescriptorTable CreateTAASrvTable(const Descriptor& srvCurrent, const Descriptor& srvPrevious,
			const Descriptor& srvVelocity, const Descriptor& srvShadeAmt, const Descriptor& srvMeta);

	protected:
		enum DescriptorTableSlot : uint8_t
		{
			TEXTURES,
			IMMUTABLE,
			PER_FRAME,
			TIME_STEP = IMMUTABLE
		};

		static const uint8_t FrameCount = XUSG_FRAME_COUNT;

		enum CBVTableIndex : uint8_t
		{
			CBV_IMMUTABLE,
			CBV_PER_FRAME,
			CBV_TIME_STEP,

			NUM_CBV_TABLE = CBV_TIME_STEP + FrameCount
		};

		enum UavSrvTableIndex : uint8_t
		{
			SRV_COLOR_AVG_LUM,
			SRV_TAA_INPUTS,
			SRV_LOG_LUM,
			UAV_SRV_LUM = SRV_LOG_LUM
		};

		bool createGBuffers(const Device* pDevice, const Texture* pReference);
		bool createPipelineLayouts();
		bool createPipelines(Format hdrFormat, Format ldrFormat);

		API m_api;
		uint8_t			m_frameIndex;
		float			m_timeStep;

		ShaderLib::sptr				m_shaderLib;
		Graphics::PipelineLib::sptr	m_graphicsPipelineLib;
		Compute::PipelineLib::sptr	m_computePipelineLib;
		PipelineLayoutLib::sptr		m_pipelineLayoutLib;
		DescriptorTableLib::sptr	m_descriptorTableLib;

		Viewport			m_viewport;
		RectRange			m_scissorRect;

		RenderTarget::uptr	m_postImage;
		RenderTarget::uptr	m_logLum;

		StructuredBuffer::uptr m_avgLum;

		PipelineLayout		m_pipelineLayouts[NUM_PIPELINE];
		Pipeline			m_pipelines[NUM_PIPELINE];

		ConstantBuffer::uptr m_cbTimeStep;

		DescriptorTable		m_cbvTables[NUM_CBV_TABLE];
		std::vector<DescriptorTable> m_uavSrvTables;
	};
}
