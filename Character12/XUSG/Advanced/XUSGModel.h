//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSGGraphicsState.h"
#include "XUSGShaderCommon.h"
#include "XUSGSDKMesh.h"

namespace XUSG
{
	class Model
	{
	public:
		enum PipelineIndex : uint8_t
		{
			OPAQUE_FRONT,
			OPAQUE_TWO_SIDE,
			ALPHA_TWO_SIDE,
			REFLECTED,

			NUM_PIPELINE
		};

		Model(const Device &device, const GraphicsCommandList &commandList);
		virtual ~Model();

		void Init(const InputLayout &inputLayout, const std::shared_ptr<SDKMesh> &mesh,
			const std::shared_ptr<Shader::Pool> &shaderPool,
			const std::shared_ptr <Graphics::Pipeline::Pool> &pipelinePool,
			const std::shared_ptr <DescriptorTablePool> &descriptorTablePool);
		void FrameMove();
		void SetMatrices(DirectX::CXMMATRIX world, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX *pShadow = nullptr, bool isTemporal = true);
		void SetPipelineState(SubsetFlag subsetFlags);
		void SetPipelineState(PipelineIndex pipeline);
		void Render(SubsetFlag subsetFlags, bool isShadow, bool reset = false);

		static InputLayout InitLayout(Graphics::Pipeline::Pool &pipelinePool);
		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device &device, const std::wstring &meshFileName);
		static void SetShadowMap(const GraphicsCommandList &commandList, const DescriptorTable &shadowTable);

	protected:
		enum DescriptorTableSlot : uint8_t
		{
			MATRICES,
			MATERIAL,
			SHADOW_MAP,
			SAMPLERS
		};

		enum CBVTableIndex : uint8_t
		{
			CBV_MATRICES,
			CBV_SHADOW_MATRIX,

			NUM_CBV
		};

		struct alignas(16) CBMatrices
		{
			DirectX::XMMATRIX WorldViewProj;
			DirectX::XMMATRIX World;
			DirectX::XMMATRIX Normal;
			DirectX::XMMATRIX ShadowProj;
#if	TEMPORAL
			DirectX::XMMATRIX WorldViewProj;
#endif
		};

		void createConstantBuffers();
		void createPipelineLayout();
		void createPipelines(const InputLayout &inputLayout, const Format *rtvFormats = nullptr,
			uint32_t numRTVs = 0, Format dsvFormat = Format(0));
		void createDescriptorTables();
		void render(uint32_t mesh, SubsetFlag subsetFlags, bool reset);

		Device				m_device;
		GraphicsCommandList	m_commandList;

		uint8_t				m_temporalIndex;

		std::shared_ptr<SDKMesh>					m_mesh;
		std::shared_ptr<Shader::Pool>				m_shaderPool;
		std::shared_ptr<Graphics::Pipeline::Pool>	m_pipelinePool;
		std::shared_ptr<DescriptorTablePool>		m_descriptorTablePool;

#if	TEMPORAL
		DirectX::XMFLOAT4X4	m_worldViewProjs[2];
#endif

		ConstantBuffer		m_cbMatrices;
		ConstantBuffer		m_cbShadowMatrix;

		PipelineLayout		m_pipelineLayout;
		PipelineState		m_pipelines[NUM_PIPELINE];
		DescriptorTable		m_cbvTables[NUM_CBV];
		DescriptorTable		m_samplerTable;
		std::vector<DescriptorTable> m_srvTables;
	};
}
