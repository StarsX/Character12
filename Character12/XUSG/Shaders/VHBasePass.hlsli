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
	min16float2	Tex		: TEXCOORD;		// Texture coordinate
	float3		Tan		: TANGENT;		// Normalized Tangent vector
	float3		BiNorm	: BINORMAL;		// Normalized BiNormal vector
};

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbMatrices : register (b0)
{
	float4x4 g_worldViewProj;
	float4x3 g_world;
	float4x3 g_worldIT;
	float4x4 g_shadow;
#if TEMPORAL
	float4x4 g_previousWVP;
#endif
};
