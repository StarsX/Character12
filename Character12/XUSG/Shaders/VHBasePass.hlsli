//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
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
	matrix	g_mWorldViewProj;
	matrix	g_mWorld;
	matrix	g_mNormal;
	matrix	g_mShadow;
#if TEMPORAL
	matrix	g_mWVPPrev;
#endif
};
