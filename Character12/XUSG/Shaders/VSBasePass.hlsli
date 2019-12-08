//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "VHBasePass.hlsli"
#include "SHCommon.hlsli"

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
#if TEMPORAL_AA
cbuffer cbTempBias	: register (b3)
{
	float2	g_projBias;
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
StructuredBuffer<Vertex>	g_roVertices;
#endif

//--------------------------------------------------------------------------------------
// Vertex shader used for the static mesh with shadow mapping
//--------------------------------------------------------------------------------------
VS_Output main(uint vid : SV_VERTEXID, VS_Input input)
{
	VS_Output output;
	float4 pos = { input.Pos, 1.0 };

#if defined(_BASEPASS_) && TEMPORAL	// Temporal tracking

#ifdef _CHARACTER_
	const float4 hPos = { g_roVertices[vid].Pos, 1.0 };
#elif defined(_VEGETATION_)
	float4 hPos = pos;
	VegetationWave(hPos, 1.0);
#else
#define hPos	pos
#endif
	output.TSPos = mul(hPos, g_previousWVP);
#endif

#ifdef _VEGETATION_
	VegetationWave(pos);
#endif

	output.Pos = mul(pos, g_worldViewProj);
#if defined(_BASEPASS_) && TEMPORAL
	output.CSPos = output.Pos;
#endif
#if TEMPORAL_AA
	output.Pos.xy += g_projBias * output.Pos.w;
#endif

#ifdef _SHADOW_MAP_
	output.LSPos = mul(pos, g_shadow);
#endif

#if defined(_POSWORLD_) || defined(_CLIP_)
	pos = mul(pos, g_mWorld);
#endif

#ifdef _POSWORLD_
	output.WSPos = pos.xyz;
#endif

#ifdef _NORMAL_
	output.Norm = min16float3(normalize(mul(input.Norm, (float3x3)g_normal)));
#endif

#ifdef _TANGENTS_
	output.Tangent = min16float3(normalize(mul(input.Tan, (float3x3)g_world)));
	output.BiNorm = min16float3(normalize(mul(input.BiNorm, (float3x3)g_world)));
#endif

	output.Tex = input.Tex;

#ifdef  _CLIP_
	output.Clip = dot(pos, g_clipPlane);
#endif

	return output;
}
