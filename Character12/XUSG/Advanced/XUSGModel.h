//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSGPipelineLayout.h"
#include "Core/XUSGGraphicsState.h"
#include "Core/XUSGDescriptor.h"
#include "XUSGShaderCommon.h"
#include "XUSGSDKMesh.h"
#include "XUSGSharedConst.h"

#define MAX_SHADOW_CASCADES	8

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
			IMMUTABLE
		};

		enum CBVTableIndex : uint8_t
		{
			CBV_MATRICES,
			CBV_SHADOW_MATRIX,

			NUM_CBV_TABLE = CBV_SHADOW_MATRIX + MAX_SHADOW_CASCADES
		};

		Model(const Device &device, const CommandList &commandList, const wchar_t *name);
		virtual ~Model();

		bool Init(const InputLayout &inputLayout, const std::shared_ptr<SDKMesh> &mesh,
			const std::shared_ptr<ShaderPool> &shaderPool,
			const std::shared_ptr<Graphics::PipelineCache> &pipelineCache,
			const std::shared_ptr<PipelineLayoutCache> &pipelineLayoutCache,
			const std::shared_ptr<DescriptorTableCache> &descriptorTableCache);
		void Update(uint8_t frameIndex);
		void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::CXMMATRIX world, DirectX::FXMMATRIX *pShadowView = nullptr,
			DirectX::FXMMATRIX *pShadows = nullptr, uint8_t numShadows = 0, bool isTemporal = true);
		void SetPipelineLayout(PipelineLayoutIndex layout);
		void SetPipeline(PipelineIndex pipeline);
		void SetPipelineState(SubsetFlags subsetFlags, PipelineLayoutIndex layout);
		void Render(SubsetFlags subsetFlags, uint8_t matrixTableIndex,
			PipelineLayoutIndex layout = NUM_PIPE_LAYOUT, uint32_t numInstances = 1);

		static InputLayout CreateInputLayout(Graphics::PipelineCache &pipelineCache);
		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device &device, const std::wstring &meshFileName,
			const TextureCache &textureCache, bool isStaticMesh);

		static constexpr uint32_t GetFrameCount() { return FrameCount; }

	protected:
		struct CBMatrices
		{
			DirectX::XMMATRIX WorldViewProj;
			DirectX::XMMATRIX World;
			DirectX::XMMATRIX Normal;
			DirectX::XMMATRIX Shadow;
#if TEMPORAL
			DirectX::XMMATRIX WorldViewProjPrev;
#endif
		};

		bool createConstantBuffers();
		bool createPipelines(bool isStatic, const InputLayout &inputLayout, const Format *rtvFormats,
			uint32_t numRTVs, Format dsvFormat, Format shadowFormat);
		bool createDescriptorTables();
		void render(uint32_t mesh, SubsetFlags subsetFlags, PipelineLayoutIndex layout,
			uint32_t numInstances);

		Util::PipelineLayout initPipelineLayout(VertexShader vs, PixelShader ps);

		static const uint32_t FrameCount = FRAME_COUNT;

		Device		m_device;
		CommandList	m_commandList;

		std::wstring m_name;

		uint8_t		m_currentFrame;
		uint8_t		m_previousFrame;

		std::shared_ptr<SDKMesh>					m_mesh;
		std::shared_ptr<ShaderPool>					m_shaderPool;
		std::shared_ptr<Graphics::PipelineCache>	m_pipelineCache;
		std::shared_ptr<PipelineLayoutCache>		m_pipelineLayoutCache;
		std::shared_ptr<DescriptorTableCache>		m_descriptorTableCache;

#if TEMPORAL
		DirectX::XMFLOAT4X4	m_worldViewProjs[FrameCount];
#endif

		ConstantBuffer		m_cbMatrices;
		ConstantBuffer		m_cbShadowMatrices;

		PipelineLayout		m_pipelineLayouts[NUM_PIPE_LAYOUT];
		Pipeline			m_pipelines[NUM_PIPELINE];
		DescriptorTable		m_cbvTables[FrameCount][NUM_CBV_TABLE];
		DescriptorTable		m_samplerTable;
		std::vector<DescriptorTable> m_srvTables;
	};
}
