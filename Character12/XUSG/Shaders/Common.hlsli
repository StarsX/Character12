//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "XUSGAdvanced.h"

//--------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------
#ifdef _BASEPASS_
#define _NORMAL_
#define _TANGENT_
#endif

#define GENERAL_SHADING	0
#define SKIN_SHADING	1
#define HAIR_SHADING	2

#if !defined(__HLSL_VERSION) || (__HLSL_VERSION < 2021)
#define and(a, b)		((a) && (b))
#define or(a, b)		((a) || (b))
#define select(c, a, b)	((c) ? (a) : (b))
#endif

#ifndef _NO_IO_STRUCT_
//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct IOStruct
{
	float4	Pos			: SV_POSITION;	// Position

#ifdef _POSWORLD_
	float3	WSPos		: POSWORLD;		// World space Pos
#endif

#ifdef _NORMAL_
	min16float3	Norm	: NORMAL;		// Normal
#endif

	float2	UV			: TEXCOORD;		// Texture coordinate

#ifdef _POSSCREEN_
	float4	SSPos		: POSSCREEN;	// Screen space Pos
#endif

#ifdef _TANGENT_
	min16float4	Tangent	: TANGENT;		// Normalized Tangent vector
#endif

#if defined(_BASEPASS_) && XUSG_TEMPORAL
	float4	CSPos		: POSCURRENT;	// Current motion-state position
	float4	TSPos		: POSHISTORY;	// Historical motion-state position
#endif

#ifdef _CLIP_
	float	Clip		: SV_ClipDistance;
#endif
};

//--------------------------------------------------------------------------------------
// Type definitions
//--------------------------------------------------------------------------------------		
typedef	IOStruct	VS_Output;
typedef IOStruct	PS_Input;
#endif

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame	: register (b1)
{
	float4x4 g_viewProj;
	float3	g_eyePt;
	float	g_time;
	float3	g_lightPt;
	float	g_timeStep;
	float4x4 g_viewProjI;
#if XUSG_TEMPORAL
	float4x4 g_viewProjPrev;
#endif
	float4x3 g_view;
	float4x3 g_shadowView;
};

uint GetShadingMode(float specularZ)
{
	return round(specularZ * 15.0);
}
