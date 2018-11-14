//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "CSSkinning.hlsli"

//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct CS_Input
{
	float3	Pos;		// Position
	uint	Weights;	// Bone weights
	uint	Bones;		// Bone indices
	uint2	Norm;		// Normal
	uint	Tex;		// Texture coordinate
	uint2	Tan;		// Normalized Tangent vector
	uint2	BiNorm;		// Normalized BiNormal vector
};

struct CS_Output
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
RWStructuredBuffer<CS_Output>	g_rwVertices	: register (u0);
StructuredBuffer<CS_Input>		g_roVertices	: register (t0);

//--------------------------------------------------------------------------------------
// Encode R16G16B16_FLOAT
//--------------------------------------------------------------------------------------
uint2 EncodeRGB16f(float3 v)
{
	return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z));
}

//--------------------------------------------------------------------------------------
// Decode R8G8B8A8_UINT
//--------------------------------------------------------------------------------------
uint4 DecodeRGBA8u(uint u)
{
	return uint4(u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff, u >> 24);
}

//--------------------------------------------------------------------------------------
// Decode R8G8B8A8_UNORM
//--------------------------------------------------------------------------------------
float4 DecodeRGBA8(uint u)
{
	return DecodeRGBA8u(u) / 255.0;
}

//--------------------------------------------------------------------------------------
// Decode R16G16B16_FLOAT
//--------------------------------------------------------------------------------------
float3 DecodeRGB16f(uint2 u)
{
	const uint2 v = { u.x & 0xffff, (u.x >> 16) & 0xffff };

	return float3(f16tof32(v), f16tof32(u.y));
}

//--------------------------------------------------------------------------------------
// Load vertex data
//--------------------------------------------------------------------------------------
VS_Input LoadVertex(uint uIdx)
{
	VS_Input vertex;

	CS_Input vertexIn = g_roVertices[uIdx];
	vertex.Pos = vertexIn.Pos;
	vertex.Weights = DecodeRGBA8(vertexIn.Weights);
	vertex.Bones = DecodeRGBA8u(vertexIn.Bones);
	vertex.Norm = DecodeRGB16f(vertexIn.Norm);
	vertex.Tan = DecodeRGB16f(vertexIn.Tan);
	vertex.BiNorm = DecodeRGB16f(vertexIn.BiNorm);
	vertex.Tex = vertexIn.Tex;
	
	return vertex;
}

//--------------------------------------------------------------------------------------
// Store vertex data
//--------------------------------------------------------------------------------------
void StoreVertex(const SkinnedInfo skinned, uint uIdx)
{
	CS_Output output;
	output.Pos = skinned.Pos;
	output.Norm = EncodeRGB16f(skinned.Norm);
	output.Tex = skinned.Tex;
	output.Tan = EncodeRGB16f(skinned.Tan);
	output.BiNorm = EncodeRGB16f(skinned.BiNorm);

	g_rwVertices[uIdx] = output;
}

//--------------------------------------------------------------------------------------
// Compute shader used for skinning the mesh for stream out
//--------------------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void main(const uint3 DTid : SV_DispatchThreadID)
{
	VS_Input vertex = LoadVertex(DTid.x);
	StoreVertex(SkinVert(vertex), DTid.x);
}
