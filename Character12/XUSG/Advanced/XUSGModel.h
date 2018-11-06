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
		Model(const CPDXDevice &pDXDevice);
		virtual ~Model(void);

		void Init(const CPDXInputLayout &pVertexLayout, const spMesh &pMesh,
			const spShader &pShader, const spState &pState);
		void FrameMove();
		void SetMatrices(DirectX::CXMMATRIX mWorld, DirectX::CXMMATRIX mViewProj,
			const LPCMATRIX pMatShadow = nullptr, bool bTemporal = true);
		void Render(const SubsetType uMaskType,
			const uint8_t uVS = g_uVSBasePass,
			const uint8_t uGS = NULL_SHADER,
			const uint8_t uPS = g_uPSBasePass,
			const bool bReset = false);

		SubsetType MaskSubsetType(const SubsetType uMaskType, const bool bReset);

		static void LoadSDKMesh(const CPDXDevice &pDXDevice, const std::wstring &szMeshFileName, spMesh &pMesh);
		static void InitLayout(const CPDXDevice &pDXDevice, CPDXInputLayout &pVertexLayout,
			const spShader &pShader, const uint8_t uVSShading = g_uVSBasePass);
		static void SetShaders(const CPDXContext &pDXContext, const spShader &pShader,
			const uint8_t uVS, const uint8_t uGS, const uint8_t uPS);
		static void SetBlendStates(const CPDXContext &pDXContext, const CPDXBlendState &pBlendState,
			const CPDXRasterizerState &pCullState);
		static void SetShadowMap(const CPDXContext &pDXContext, const CPDXShaderResourceView &pSRVShadow);

	protected:
		void createConstBuffers();
		void setResourceSlots();
		void render(const uint32_t uMesh, const SubsetType uMaskType, const bool bReset);
		void setSubsetStates(const uint32_t uMesh, const uint32_t uSubset, const SubsetType uMaskType);
		void setShaders(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS, const bool bReset);
		void resetShaders(const uint8_t uVS, const uint8_t uGS, const uint8_t uPS,
			const SubsetType uMaskType, const bool bReset);

		uint8_t					m_uCBMatrices;
		uint8_t					m_uSRDiffuse;
		uint8_t					m_uSRNormal;
		uint8_t					m_uSmpAnisoWrap;
		uint8_t					m_uSmpLinearCmp;

		uint8_t					m_uTSIdx;			// Index of temporal states

		spMesh					m_pMesh;			// The mesh
		spShader				m_pShader;
		spState					m_pState;

#if	TEMPORAL
		DirectX::XMFLOAT4X4		m_pmWorldViewProjs[2];
#endif

		CPDXBuffer				m_pCBMatrices;

		CPDXInputLayout			m_pVertexLayout;

		CPDXDevice				m_pDXDevice;
		CPDXContext				m_pDXContext;

		static uint8_t			m_uSRShadow;

		static const LPDXBuffer	m_pNullBuffer;		// Helper to Clear Buffers
		static const uint32_t	m_uNullUint;		// Helper to Clear Buffers
		static const uint32_t	m_uNullStride;
		static const uint32_t	m_uOffset;
	};
}
