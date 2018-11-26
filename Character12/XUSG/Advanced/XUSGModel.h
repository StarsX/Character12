//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSGGraphicsState.h"
#include "XUSGShaderCommon.h"
#include "XUSGSDKMesh.h"
#include "XUSGSharedConst.h"

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

		bool Init(const InputLayout &inputLayout, const std::shared_ptr<SDKMesh> &mesh,
			const std::shared_ptr<ShaderPool> &shaderPool,
			const std::shared_ptr<Graphics::PipelineCache> &pipelineCache,
			const std::shared_ptr<PipelineLayoutCache> &pipelineLayoutCache,
			const std::shared_ptr<DescriptorTableCache> &descriptorTableCache);
		void FrameMove();
		void SetMatrices(DirectX::CXMMATRIX world, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX *pShadow = nullptr, bool isTemporal = true);
		void SetPipelineState(SubsetFlags subsetFlags);
		void SetPipelineState(PipelineIndex pipeline);
		void Render(SubsetFlags subsetFlags, bool isShadow, bool reset = false);

		static InputLayout CreateInputLayout(Graphics::PipelineCache &pipelineCache);
		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device &device, const std::wstring &meshFileName,
			const TextureCache &textureCache);
		static void SetShadowMap(const GraphicsCommandList &commandList, const DescriptorTable &shadowTable);

		static constexpr uint32_t GetFrameCount() { return FrameCount; }

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
			DirectX::XMMATRIX WorldViewProjPrev;
#endif
		};

		bool createConstantBuffers();
		virtual void createPipelineLayout();
		void createPipelines(const InputLayout &inputLayout, const Format *rtvFormats = nullptr,
			uint32_t numRTVs = 0, Format dsvFormat = Format(0));
		void createDescriptorTables();
		void render(uint32_t mesh, SubsetFlags subsetFlags, bool reset);

		Util::PipelineLayout initPipelineLayout(VertexShader vs, PixelShader ps);

		static const uint32_t FrameCount = FRAME_COUNT;

		Device				m_device;
		GraphicsCommandList	m_commandList;

		uint8_t				m_currentFrame;
		uint8_t				m_previousFrame;

		std::shared_ptr<SDKMesh>					m_mesh;
		std::shared_ptr<ShaderPool>					m_shaderPool;
		std::shared_ptr<Graphics::PipelineCache>	m_pipelineCache;
		std::shared_ptr<PipelineLayoutCache>		m_pipelineLayoutCache;
		std::shared_ptr<DescriptorTableCache>		m_descriptorTableCache;

#if	TEMPORAL
		DirectX::XMFLOAT4X4	m_worldViewProjs[FrameCount];
#endif

		ConstantBuffer		m_cbMatrices;
		ConstantBuffer		m_cbShadowMatrix;

		PipelineLayout		m_pipelineLayout;
		Pipeline			m_pipelines[NUM_PIPELINE];
		DescriptorTable		m_cbvTables[NUM_CBV];
		DescriptorTable		m_samplerTable;
		std::vector<DescriptorTable> m_srvTables;
	};
}
