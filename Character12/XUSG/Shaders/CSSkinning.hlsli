//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

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
#ifdef _TANGENTS_
	float3	Tan		: TANGENT;		// Normalized Tangent vector
	float3	BiNorm	: BINORMAL;		// Normalized BiNormal vector
#endif
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
// Structured buffer for bone matrices
StructuredBuffer<float3x4>	g_roDualQuat;

//--------------------------------------------------------------------------------------
// Helper struct for passing back skinned vertex information
//--------------------------------------------------------------------------------------
struct SkinnedInfo
{
	float3	Pos;
	float3	Norm;
	uint	UV;
#ifdef _TANGENTS_
	float3	Tan;
	float3	BiNorm;
#endif
};

//--------------------------------------------------------------------------------------
// Translation with DQ
//--------------------------------------------------------------------------------------
float3 TranslateWithDQ(float3 vec, float2x4 dual)
{
	float3 disp = cross(dual[0].xyz, dual[1].xyz);
	disp += dual[0].w * dual[1].xyz;
	disp -= dual[1].w * dual[0].xyz;

	return disp * 2.0f + vec;
}

//--------------------------------------------------------------------------------------
// Rotations with DQ
//--------------------------------------------------------------------------------------
float3 RotateWithDQ(float3 vec, float2x4 dual)
{
	float3 disp = cross(dual[0].xyz, cross(dual[0].xyz, vec) + dual[0].w * vec);

	return disp * 2.0f + vec;
}

//--------------------------------------------------------------------------------------
// SkinVert skins a single vertex
//--------------------------------------------------------------------------------------
SkinnedInfo SkinVert(VS_Input input)
{
	SkinnedInfo output;
	
	const float3x4 m[] =
	{
		g_roDualQuat[input.Bones.x],
		g_roDualQuat[input.Bones.y],
		g_roDualQuat[input.Bones.z],
		g_roDualQuat[input.Bones.w]
	};

	float weight = input.Weights[0];
	float4 scale = weight * m[0][2];
	float2x4 dual = weight * (float2x4)m[0];

	[unroll]
	for (uint i = 1; i < 4; ++i)
	{
		weight = input.Weights[i];
		scale += weight * m[i][2];
		dual += (dot(m[0][0], m[i][0]) < 0.0 ? -weight : weight) * (float2x4)m[i];
	}

	// fast dqs 
	dual /= length(dual[0]);
	float3 normal = input.Norm / scale.xyz;
#ifdef _TANGENTS_
	float3 tangent = input.Tan * scale.xyz;
	float3 binorm = input.BiNorm * scale.xyz;
#endif
	float3 pos = input.Pos * scale.xyz;
	pos = RotateWithDQ(pos, dual);
	pos = TranslateWithDQ(pos, dual);

	output.Pos = pos;
	output.Norm = RotateWithDQ(normal, dual);
#ifdef _TANGENTS_
	output.Tan = RotateWithDQ(tangent, dual);
	output.BiNorm = RotateWithDQ(binorm, dual);
#endif
	output.UV = input.UV;

	return output;
}
