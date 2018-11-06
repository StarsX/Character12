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
		using LPCPDXInputLayout = std::add_pointer_t<CPDXInputLayout>;

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

		using vMeshLink			= std::vector<MeshLink>;
		using svMeshLink		= std::shared_ptr<vMeshLink>;
		using pvCPDXSRV			= std::add_pointer_t<vCPDXSRV>;
		using pvCPDXUAV			= std::add_pointer_t<vCPDXUAV>;

		Character(const CPDXDevice &pDXDevice);
		virtual ~Character();

		virtual void Init(const CPDXInputLayout &pVertexLayout, const spMesh &pSkinnedMesh,
			const spShader &pShader, const spState &pState, const bool bReadVB = false,
			const CPDXInputLayout pSkinnedVertexLayout = nullptr);
		void Init(const CPDXInputLayout &pVertexLayout, const spMesh &pSkinnedMesh,
			const svMesh &pvLinkedMeshes, const svMeshLink &pvMeshLinks, const spShader &pShader,
			const spState &pState, const bool bReadVB = false,
			const CPDXInputLayout pSkinnedVertexLayout = nullptr);
		void InitPosition(const DirectX::XMFLOAT4 &vPosRot);
		void FrameMove(const double fTime);
		void FrameMove(const double fTime, DirectX::CXMMATRIX mWorld, DirectX::CXMMATRIX mViewProj);
		void SetMatrices(DirectX::CXMMATRIX mViewProj,
			const LPCMATRIX pMatShadow = nullptr, bool bTemporal = true);
		virtual void SetMatrices(DirectX::CXMMATRIX mWorld, DirectX::CXMMATRIX mViewProj,
			const LPCMATRIX pMatShadow = nullptr, bool bTemporal = true);
		void Skinning(const bool bReset = false);
		void RenderTransformed(const SubsetType uMaskType = SUBSET_FULL,
			const uint8_t uVS = g_uVSBasePass, const uint8_t uGS = NULL_SHADER,
			const uint8_t uPS = g_uPSBasePass, const bool bReset = false);

		const DirectX::XMFLOAT4 &GetPosition() const;
		DirectX::FXMMATRIX GetWorldMatrix() const;

		static void LoadSDKMesh(const CPDXDevice &pDXDevice, const std::wstring &szMeshFileName,
			const std::wstring &szAnimFileName, spMesh &pSkinnedMesh);
		static void LoadSDKMesh(const CPDXDevice &pDXDevice, const std::wstring &szMeshFileName,
			const std::wstring &szAnimFileName, spMesh &pSkinnedMesh,
			svMesh &pvLinkedMeshes, const svMeshLink &pvMeshLinkage);
		static void InitLayout(const CPDXDevice &pDXDevice, CPDXInputLayout &pVertexLayout,
			const spShader &pShader, const bool bDualQuat, const uint8_t uVSShading = g_uVSBasePass,
			LPCPDXInputLayout ppSkinnedVertexLayout = nullptr, const uint8_t uVSSkinning = g_uVSSkinning);
		static void SetSkinningShader(const CPDXContext &pDXContext, const spShader &pShader,
			const uint8_t uCS, const uint8_t uVS = NULL_SHADER);

		static const D3D11_SO_DECLARATION_ENTRY m_pSODecl[];
		static const uint8_t m_uNumSODecls;

	protected:
		void createTransformedStates(const uint8_t uBindFlags = 0);
		void createTransformedVBs(vuRawBuffer &vpVBs, const uint8_t uBindFlags);
		void createConstBuffers();
		void setResourceSlots(const bool bReadVB);
		virtual void setLinkedMatrices(const uint32_t uMesh, DirectX::CXMMATRIX mWorld,
			DirectX::CXMMATRIX mViewProj, const LPCMATRIX pMatShadow, bool bTemporal);
		void skinning(const bool bReset);
		void skinningSO(const bool bReset);
		void renderTransformed(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
			const SubsetType uMaskType, const bool bReset);
		void renderLinked(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
			const uint32_t uMesh, const bool bReset);
		void setBoneMatrices(const uint32_t uMesh);
		void convertToDQ(DirectX::XMFLOAT4 &vDQTran,
			const DirectX::CXMVECTOR &vQuat, const DirectX::XMFLOAT3 &vTran) const;
		DirectX::FXMMATRIX getDualQuat(const uint32_t uMesh, const uint32_t uInfluence) const;

		static void createSkinnedLayout(const CPDXDevice &pDXDevice,
			CPDXInputLayout &pSkinnedVertexLayout, const spShader &pShader, const uint8_t uVSSkinning);

		svMesh				m_pvLinkedMeshes;
		svMeshLink			m_pvMeshLinks;

		vuRawBuffer			m_pvpTransformedVBs[2];
		DirectX::XMFLOAT4X4	m_mWorld;
		DirectX::XMFLOAT4	m_vPosRot;

#if	TEMPORAL
		vfloat4x4			m_vmLinkedWVPs[2];
#endif

		double				m_fTime;

		uint8_t				m_uUAVertices;
		uint8_t				m_uSRVertices;
		uint8_t				m_uSRBoneWorld;

		upStructuredBuffer	m_pSBBoneWorld;
		vCPDXBuffer			m_vpCBLinkedMats;

		CPDXInputLayout		m_pSkinnedVertexLayout;

		static bool			m_bDualQuat;
	};

	using upCharacter = std::unique_ptr<Character>;
	using spCharacter = std::shared_ptr<Character>;
	using vuCharacter = std::vector<upCharacter>;
	using vpCharacter = std::vector<spCharacter>;

	using vMeshLink = Character::vMeshLink;
	using svMeshLink = Character::svMeshLink;
}
