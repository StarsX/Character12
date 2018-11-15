//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
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
	uint	Tex		: TEXCOORD;		// Texture coordinate
	float3	Tan		: TANGENT;		// Normalized Tangent vector
	float3	BiNorm	: BINORMAL;		// Normalized BiNormal vector
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
	uint	Tex;
	float3	Tan;
	float3	BiNorm;
};

//--------------------------------------------------------------------------------------
// Translation with DQ
//--------------------------------------------------------------------------------------
float3 TranslateWithDQ(const float3 vVec, const float2x4 dual)
{
	float3 vDisp = cross(dual[0].xyz, dual[1].xyz);
	vDisp += dual[0].w * dual[1].xyz;
	vDisp -= dual[1].w * dual[0].xyz;

	return vDisp * 2.0f + vVec;
}

//--------------------------------------------------------------------------------------
// Rotations with DQ
//--------------------------------------------------------------------------------------
float3 RotateWithDQ(const float3 vVec, const float2x4 dual)
{
	float3 vDisp = cross(dual[0].xyz, cross(dual[0].xyz, vVec) + dual[0].w * vVec);

	return vDisp * 2.0f + vVec;
}

//--------------------------------------------------------------------------------------
// SkinVert skins a single vertex
//--------------------------------------------------------------------------------------
SkinnedInfo SkinVert(const VS_Input input)
{
	SkinnedInfo output;
	
	const float3x4 m[] =
	{
		g_roDualQuat[input.Bones.x],
		g_roDualQuat[input.Bones.y],
		g_roDualQuat[input.Bones.z],
		g_roDualQuat[input.Bones.w]
	};

	float fWeight = input.Weights[0];
	float4 vScl = fWeight * m[0][2];
	float2x4 dual = fWeight * (float2x4)m[0];

	[unroll]
	for (uint i = 1; i < 4; ++i)
	{
		fWeight = input.Weights[i];
		vScl += fWeight * m[i][2];
		dual += (dot(m[0][0], m[i][0]) < 0.0 ? -fWeight : fWeight) * (float2x4)m[i];
	}

	// fast dqs 
	dual /= length(dual[0]);
	float3 vNorm = input.Norm / vScl.xyz;
	float3 vTan = input.Tan * vScl.xyz;
	float3 vBiNorm = input.BiNorm * vScl.xyz;
	float3 vPos = input.Pos * vScl.xyz;
	vPos = RotateWithDQ(vPos, dual);
	vPos = TranslateWithDQ(vPos, dual);

	output.Pos = vPos;
	output.Norm = RotateWithDQ(vNorm, dual);
	output.Tan = RotateWithDQ(vTan, dual);
	output.BiNorm = RotateWithDQ(vBiNorm, dual);
	output.Tex = input.Tex;

	return output;
}
