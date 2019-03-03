//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "VHBasePass.hlsli"
#include "SHCommon.hlsli"

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
#if TEMPORAL_AA
cbuffer cbTempBias	: register (b3)
{
	float2	g_vProjBias;
};
#endif

#if TEMPORAL
//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct Vertex
{
	float3	Pos;		// Position
	uint2	Norm;		// Normal
	uint	Tex;		// Texture coordinate
	uint2	Tan;		// Normalized Tangent vector
	uint2	BiNorm;		// Normalized BiNormal vector
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
// Buffer for the historical moition state
StructuredBuffer<Vertex>	g_roVertices	: register (t0);
#endif

//--------------------------------------------------------------------------------------
// Vertex shader used for the static mesh with shadow mapping
//--------------------------------------------------------------------------------------
VS_Output main(uint vid : SV_VERTEXID, VS_Input input)
{
	VS_Output output;
	float4 vPos = { input.Pos, 1.0 };

#if defined(_BASEPASS_) && TEMPORAL	// Temporal tracking

#ifdef _CHARACTER_
	const float4 vHPos = { g_roVertices[vid].Pos, 1.0 };
#elif defined(_VEGETATION_)
	float4 vHPos = vPos;
	VegetationWave(vHPos, 1.0);
#else
#define vHPos	vPos
#endif
	output.TSPos = mul(vHPos, g_mWVPPrev);
#endif

#ifdef _VEGETATION_
	VegetationWave(vPos);
#endif

	output.Pos = mul(vPos, g_mWorldViewProj);
#if defined(_BASEPASS_) && TEMPORAL
	output.CSPos = output.Pos;
#endif
#if TEMPORAL_AA
	output.Pos.xy += g_vProjBias * output.Pos.w;
#endif

#ifdef _SHADOW_MAP_
	output.LSPos = mul(vPos, g_mShadow);
#endif

#if defined(_POSWORLD_) || defined(_CLIP_)
	vPos = mul(vPos, g_mWorld);
#endif

#ifdef _POSWORLD_
	output.WSPos = vPos.xyz;
#endif

#ifdef _NORMAL_
	output.Norm = min16float3(normalize(mul(input.Norm, (float3x3)g_mNormal)));
#endif

#ifdef _TANGENTS_
	output.Tangent = min16float3(normalize(mul(input.Tan, (float3x3)g_mWorld)));
	output.BiNorm = min16float3(normalize(mul(input.BiNorm, (float3x3)g_mWorld)));
#endif

	output.Tex = input.Tex;

#ifdef  _CLIP_
	output.Clip = dot(vPos, g_vClipPlane);
#endif

	return output;
}
