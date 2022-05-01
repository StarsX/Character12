//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define RotQ DualQ[0]
#define rotQ dualQ[0]

//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct VS_Input
{
	float3	Pos		: POSITION;		// Position
	float4	Weights	: WEIGHTS;		// Bone weights
	uint4	Bones	: BONES;		// Bone indices
	float3	Norm	: NORMAL;		// Normal
	uint	UV		: TEXCOORD;		// Texture coordinate
#ifdef _TANGENT_
	float3	Tan		: TANGENT;		// Normalized Tangent vector
#endif
};

struct TRS
{
	row_major float2x4 DualQ;
	float4 Scale;
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
// Structured buffer for bone transforms
StructuredBuffer<TRS> g_roTransforms;

//--------------------------------------------------------------------------------------
// Helper struct for passing back skinned vertex information
//--------------------------------------------------------------------------------------
struct SkinnedInfo
{
	float3	Pos;
	float3	Norm;
	uint	UV;
#ifdef _TANGENT_
	float3	Tan;
#endif
};

//--------------------------------------------------------------------------------------
// Translation with DQ
//--------------------------------------------------------------------------------------
float3 TranslateWithDQ(float3 vec, float2x4 dualQ)
{
	float3 disp = cross(rotQ.xyz, dualQ[1].xyz);
	disp += rotQ.w * dualQ[1].xyz;
	disp -= dualQ[1].w * rotQ.xyz;

	return disp * 2.0 + vec;
}

//--------------------------------------------------------------------------------------
// Rotations with DQ
//--------------------------------------------------------------------------------------
float3 RotateWithDQ(float3 vec, float2x4 dualQ)
{
	float3 disp = cross(rotQ.xyz, cross(rotQ.xyz, vec) + rotQ.w * vec);

	return disp * 2.0 + vec;
}

//--------------------------------------------------------------------------------------
// Assign the sign of the src reference to the dst value
//--------------------------------------------------------------------------------------
float Sign(float src, float dst)
{
	//return src < 0.0 ? -dst : dst;
	return asfloat((asuint(src) & 0x80000000) | asuint(dst));
}

//--------------------------------------------------------------------------------------
// SkinVert skins a single vertex
//--------------------------------------------------------------------------------------
SkinnedInfo SkinVert(VS_Input input)
{
	SkinnedInfo output;

	const TRS trs0 = g_roTransforms[input.Bones.x];
	const float weight = input.Weights[0];
	float2x4 dualQ = weight * trs0.DualQ;
	float3 scale = weight * trs0.Scale.xyz;

	[unroll]
	for (uint i = 1; i < 4; ++i)
	{
		const TRS trs = g_roTransforms[input.Bones[i]];
		const float weight = input.Weights[i];
		dualQ += Sign(dot(trs0.RotQ, trs.RotQ), weight) * trs.DualQ;
		scale += weight * trs.Scale.xyz;
	}

	// fast dqs 
	dualQ /= length(rotQ);
	float3 normal = input.Norm / scale;
#ifdef _TANGENT_
	float3 tangent = input.Tan * scale;
#endif
	float3 pos = input.Pos * scale;
	pos = RotateWithDQ(pos, dualQ);
	pos = TranslateWithDQ(pos, dualQ);

	output.Pos = pos;
	output.Norm = RotateWithDQ(normal, dualQ);
#ifdef _TANGENT_
	output.Tan = RotateWithDQ(tangent, dualQ);
#endif
	output.UV = input.UV;

	return output;
}
