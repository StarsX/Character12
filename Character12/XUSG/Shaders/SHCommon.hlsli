//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------
#ifdef _BASEPASS_
#define _NORMAL_
#define _TANGENTS_
#endif

//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct IOStruct
{
	float4	Pos			: SV_POSITION;	// Position

#ifdef _POSWORLD_
	float3	WSPos		: POSWORLD;		// World space Pos
#endif

#ifdef _SHADOW_MAP_
	float4	LSPos		: POSLIGHT;		// Light space Pos
#endif

#ifdef _NORMAL_
	min16float3	Norm	: NORMAL;		// Normal
#endif

	float2	Tex			: TEXCOORD;		// Texture coordinate

#ifdef _POSSCREEN_
	float4	SSPos		: POSSCREEN;	// Screen space Pos
#endif

#ifdef _TANGENTS_
	min16float3	Tangent	: TANGENT;		// Normalized Tangent vector
	min16float3	BiNorm	: BINORMAL;		// Normalized BiNormal vector
#endif

#if defined(_BASEPASS_) && TEMPORAL
	float4	CSPos		: POSCURRENT;	// Current motion-state position
	float4	TSPos		: POSHISTORY;	// Historical motion-state position
#endif

#ifdef _CLIP_
	float	Clip		: SV_CLIPDISTANCE;
#endif
};

//--------------------------------------------------------------------------------------
// Type definitions
//--------------------------------------------------------------------------------------		
typedef	IOStruct	VS_Output;
typedef IOStruct	PS_Input;
