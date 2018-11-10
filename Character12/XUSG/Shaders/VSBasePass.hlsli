//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "VHBasePass.hlsli"
#include "SHCommon.hlsli"
#include "CHDataSize.hlsli"

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
#if	TEMPORAL_AA
cbuffer cbTempBias	: register (b3)
{
	float2	g_vProjBias;
};
#endif

#if	TEMPORAL
//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
// Raw buffers for historical moition states
ByteAddressBuffer	g_roVertices	: register (t0);

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------		
static const uint	g_uNorm		= APPEND_OFFSET(0, SIZE_OF_FLOAT3);
static const uint	g_uTex		= APPEND_OFFSET(g_uNorm, SIZE_OF_HALF4);
static const uint	g_uTan		= APPEND_OFFSET(g_uTex, SIZE_OF_HALF2);
static const uint	g_uBiNrm	= APPEND_OFFSET(g_uTan, SIZE_OF_HALF4);
static const uint	g_uStride	= APPEND_OFFSET(g_uBiNrm, SIZE_OF_HALF4);
#endif

//--------------------------------------------------------------------------------------
// Vertex shader used for the static mesh with shadow mapping
//--------------------------------------------------------------------------------------
VS_Output main(const uint vid : SV_VERTEXID, const VS_Input input)
{
	VS_Output output;
	float4 vPos = { input.Pos, 1.0 };

#if defined(_BASEPASS_) && TEMPORAL	// Temporal tracking

#ifdef	_CHARACTER_
	const uint uIdx = g_uStride * vid;
	const uint3 vPosu = g_roVertices.Load3(uIdx);
	const float4 vHPos = { asfloat(vPosu), 1.0 };
#elif	defined(_VEGETATION_)
	float4 vHPos = vPos;
	VegetationWave(vHPos, 1.0);
#else
#define	vHPos	vPos
#endif
	output.TSPos = mul(vHPos, g_mWVPPrev);
#endif

#ifdef	_VEGETATION_
	VegetationWave(vPos);
#endif

	output.Pos = mul(vPos, g_mWorldViewProj);
#if	defined(_BASEPASS_) && TEMPORAL_AA
	output.CSPos = output.Pos;
#endif
#if	TEMPORAL_AA
	output.Pos.xy += g_vProjBias * output.Pos.w;
#endif

#ifdef	_SHADOW_MAP_
	output.LSPos = mul(vPos, g_mShadow);
#endif

#if	defined(_POSWORLD_) || defined(_CLIP_)
	vPos = mul(vPos, g_mWorld);
#endif

#ifdef	_POSWORLD_
	output.WSPos = vPos.xyz;
#endif

#ifdef	_NORMAL_
	output.Norm = min16float3(normalize(mul(input.Norm, (float3x3)g_mNormal)));
#endif

#ifdef	_TANGENTS_
	output.Tangent = min16float3(normalize(mul(input.Tan, (float3x3)g_mWorld)));
	output.BiNorm = min16float3(normalize(mul(input.BiNorm, (float3x3)g_mWorld)));
#endif

	output.Tex = input.Tex;

#ifdef  _CLIP_
	output.Clip = dot(vPos, g_vClipPlane);
#endif

	return output;
}
