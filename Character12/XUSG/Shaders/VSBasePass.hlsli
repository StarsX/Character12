//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct VS_Input
{
	float3		Pos		: POSITION;		// Position
	float3		Norm	: NORMAL;		// Normal
	min16float2	UV		: TEXCOORD;		// Texture coordinate
#ifdef _TANGENTS_
	float3		Tan		: TANGENT;		// Normalized Tangent vector
	float3		BiNorm	: BINORMAL;		// Normalized BiNormal vector
#endif
};

#if TEMPORAL
struct Vertex
{
	float3	Pos;	// Position
	uint2	Norm;	// Normal
	uint	UV;		// Texture coordinate
#ifdef _TANGENTS_
	uint2	Tan;	// Normalized Tangent vector
	uint2	BiNorm;	// Normalized BiNormal vector
#endif
};
#endif

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbMatrices : register (b0)
{
	float4x3 g_world;
	float3x3 g_worldIT;
#if TEMPORAL
	float4x3 g_previousWorld;
#endif
};

#if TEMPORAL_AA
cbuffer cbTemporalBias	: register (b3)
{
	float2	g_projBias;
};
#endif

#if TEMPORAL
//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
// Buffer for the historical moition state
StructuredBuffer<Vertex> g_roVertices;
#endif

//--------------------------------------------------------------------------------------
// Vertex shader used for the static mesh with shadow mapping
//--------------------------------------------------------------------------------------
VS_Output main(uint vid : SV_VertexID, VS_Input input)
{
	VS_Output output;
	float4 pos = { input.Pos, 1.0 };

#if defined(_BASEPASS_) && TEMPORAL	// Temporal tracking

#ifdef _CHARACTER_
	float4 hPos = { g_roVertices[vid].Pos, 1.0 };
#elif defined(_VEGETATION_)
	float4 hPos = pos;
	VegetationWave(hPos, 1.0);
#else
#define hPos pos
#endif
	hPos.xyz = mul(hPos, g_previousWorld);
	output.TSPos = mul(hPos, g_viewProjPrev);
#endif

#ifdef _VEGETATION_
	VegetationWave(pos);
#endif

	pos.xyz = mul(pos, g_world);
	output.Pos = mul(pos, g_viewProj);
#if defined(_BASEPASS_) && TEMPORAL
	output.CSPos = output.Pos;
#endif
#if TEMPORAL_AA
	output.Pos.xy += g_projBias * output.Pos.w;
#endif

#ifdef _POSWORLD_
	output.WSPos = pos.xyz;
#endif

#ifdef _NORMAL_
	output.Norm = min16float3(normalize(mul(input.Norm, g_worldIT)));
#endif

#ifdef _TANGENTS_
	output.Tangent = min16float3(normalize(mul(input.Tan, (float3x3)g_world)));
	output.BiNorm = min16float3(normalize(mul(input.BiNorm, (float3x3)g_world)));
#endif

	output.UV = input.UV;

#ifdef  _CLIP_
	output.Clip = dot(pos, g_clipPlane);
#endif

	return output;
}
