//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSGGraphicsState.h"
#include "XUSGShaderCommon.h"
#include "XUSGSDKMesh.h"
#include "XUSGSharedConst.h"

#define	MAX_SHADOW_CASCADES	8

namespace XUSG
{
	class Model
	{
	public:
		enum PipelineLayoutIndex : uint8_t
		{
			BASE_PASS,
			DEPTH_PASS,

			NUM_PIPE_LAYOUT
		};

		enum PipelineIndex : uint8_t
		{
			OPAQUE_FRONT,
			OPAQUE_FRONT_EQUAL,
			OPAQUE_TWO_SIDE,
			OPAQUE_TWO_SIDE_EQUAL,
			ALPHA_TWO_SIDE,
			DEPTH_FRONT,
			DEPTH_TWO_SIDE,
			REFLECTED,

			NUM_PIPELINE
		};

		enum DescriptorTableSlot : uint8_t
		{
			SAMPLERS,
			MATERIAL,
			SHADOW_MAP,
			ALPHA_REF = SHADOW_MAP,

#if TEMPORAL_AA
			TEMPORAL_BIAS,
#endif
			MATRICES,
			PER_OBJECT,
			PER_FRAME,
		};

		enum CBVTableIndex : uint8_t
		{
			CBV_MATRICES,
			CBV_SHADOW_MATRIX,

			NUM_CBV = CBV_SHADOW_MATRIX + MAX_SHADOW_CASCADES
		};

		Model(const Device &device, const CommandList &commandList);
		virtual ~Model();

		bool Init(const InputLayout &inputLayout, const std::shared_ptr<SDKMesh> &mesh,
			const std::shared_ptr<ShaderPool> &shaderPool,
			const std::shared_ptr<Graphics::PipelineCache> &pipelineCache,
			const std::shared_ptr<PipelineLayoutCache> &pipelineLayoutCache,
			const std::shared_ptr<DescriptorTableCache> &descriptorTableCache);
		void FrameMove();
		void SetMatrices(DirectX::CXMMATRIX world, DirectX::CXMMATRIX viewProj,
			DirectX::FXMMATRIX *pShadow = nullptr, uint8_t numShadows = 0,
			bool isTemporal = true);
		void SetPipeline(SubsetFlags subsetFlags, PipelineLayoutIndex layout);
		void SetPipeline(PipelineIndex pipeline, PipelineLayoutIndex layout);
		void Render(SubsetFlags subsetFlags, uint8_t matrixTableIndex, PipelineLayoutIndex layout = NUM_PIPE_LAYOUT);

		static InputLayout CreateInputLayout(Graphics::PipelineCache &pipelineCache);
		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device &device, const std::wstring &meshFileName,
			const TextureCache &textureCache, bool isStaticMesh);
		static void SetShadowMap(const CommandList &commandList, const DescriptorTable &shadowTable);

		static constexpr uint32_t GetFrameCount() { return FrameCount; }

	protected:
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
		virtual void createPipelineLayouts();
		void createDescriptorTables();
		void setPipelineState(SubsetFlags subsetFlags, PipelineLayoutIndex layout);
		void render(uint32_t mesh, SubsetFlags subsetFlags, PipelineLayoutIndex layout);

		Util::PipelineLayout initPipelineLayout(VertexShader vs, PixelShader ps);

		static const uint32_t FrameCount = FRAME_COUNT;

		Device		m_device;
		CommandList	m_commandList;

		uint8_t		m_currentFrame;
		uint8_t		m_previousFrame;

		std::shared_ptr<SDKMesh>					m_mesh;
		std::shared_ptr<ShaderPool>					m_shaderPool;
		std::shared_ptr<Graphics::PipelineCache>	m_pipelineCache;
		std::shared_ptr<PipelineLayoutCache>		m_pipelineLayoutCache;
		std::shared_ptr<DescriptorTableCache>		m_descriptorTableCache;

#if	TEMPORAL
		DirectX::XMFLOAT4X4	m_worldViewProjs[FrameCount];
#endif

		ConstantBuffer		m_cbMatrices;
		ConstantBuffer		m_cbShadowMatrices;

		PipelineLayout		m_pipelineLayouts[NUM_PIPE_LAYOUT];
		Pipeline			m_pipelines[NUM_PIPELINE];
		DescriptorTable		m_cbvTables[FrameCount][NUM_CBV];
		DescriptorTable		m_samplerTable;
		std::vector<DescriptorTable> m_srvTables;
	};
}
