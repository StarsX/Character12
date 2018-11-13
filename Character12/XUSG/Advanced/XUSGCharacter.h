//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

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
			std::wstring		szMeshName;
			std::string			szBoneName;
			uint32_t			uBone;
		};

		Character(const Device &device, const GraphicsCommandList &commandList);
		virtual ~Character();

		void Init(const InputLayout &inputLayout,
			const std::shared_ptr<SDKMesh> &mesh,
			const std::shared_ptr<Shader::Pool> &shaderPool,
			const std::shared_ptr<Graphics::Pipeline::Pool> &pipelinePool,
			const std::shared_ptr<DescriptorTablePool> &descriptorTablePool,
			const std::shared_ptr<std::vector<SDKMesh>> &linkedMeshes = nullptr,
			const std::shared_ptr<std::vector<MeshLink>> &meshLinks = nullptr);
		void InitPosition(const DirectX::XMFLOAT4 &posRot);
		void FrameMove(double time);
		void FrameMove(double time, DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX *pWorld = nullptr,
			DirectX::FXMMATRIX *pShadow = nullptr, bool isTemporal = true);
		virtual void SetMatrices(DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX *pWorld = nullptr,
			DirectX::FXMMATRIX *pShadow = nullptr, bool isTemporal = true);
		void Skinning(bool reset = false);
		void RenderTransformed(SubsetFlags subsetFlags = SUBSET_FULL, bool isShadow = false, bool reset = false);

		const DirectX::XMFLOAT4 &GetPosition() const;
		DirectX::FXMMATRIX GetWorldMatrix() const;

		static std::shared_ptr<SDKMesh> LoadSDKMesh(const Device &device, const std::wstring &meshFileName,
			const std::wstring &animFileName, const TextureCache &textureCache,
			const std::shared_ptr<std::vector<MeshLink>> &meshLinks = nullptr,
			std::vector<SDKMesh> *pLinkedMeshes = nullptr);

	protected:
		enum DescriptorTableSlot : uint8_t
		{
			INPUT,
			OUTPUT
		};

		void createTransformedStates();
		void createTransformedVBs(std::vector<VertexBuffer> &vertexBuffers);
		void createBuffers();
		void createPipelineLayout();
		void createPipelines();
		void createDescriptorTables();
		virtual void setLinkedMatrices(uint32_t mesh, DirectX::CXMMATRIX world,
			DirectX::CXMMATRIX viewProj, DirectX::FXMMATRIX *pShadow, bool isTemporal);
		void skinning(bool reset);
		void renderTransformed(SubsetFlags subsetFlags, bool isShadow, bool reset);
		void renderLinked(uint32_t mesh, bool isShadow, bool reset);
		void setBoneMatrices(uint32_t mesh);
		void convertToDQ(DirectX::XMFLOAT4 &dqTran, DirectX::CXMVECTOR quat,
			const DirectX::XMFLOAT3 &tran) const;
		DirectX::FXMMATRIX getDualQuat(uint32_t mesh, uint32_t influence) const;

		std::shared_ptr<std::vector<SDKMesh>>	m_linkedMeshes;
		std::shared_ptr<std::vector<MeshLink>>	m_meshLinks;

		std::vector<VertexBuffer> m_transformedVBs[2];
		DirectX::XMFLOAT4X4	m_mWorld;
		DirectX::XMFLOAT4	m_vPosRot;

#if	TEMPORAL
		std::vector<DirectX::XMFLOAT4X4> m_linkedWorldViewProjs[2];
#endif

		double m_time;

		std::vector<StructuredBuffer> m_boneWorlds;
		std::vector<ConstantBuffer> m_cbLinkedMatrices;
		std::vector<ConstantBuffer> m_cbLinkedShadowMatrices;

		PipelineLayout	m_skinningPipelineLayout;
		PipelineState	m_skinningPipeline;
		std::vector<DescriptorTable> m_srvSkinningTables;
		std::vector<DescriptorTable> m_uavSkinningTables[2];
	};
}
