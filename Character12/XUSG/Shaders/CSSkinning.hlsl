//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define _TANGENT_

#include "XUSGAdvanced.h"
#include "CSSkinning.hlsli"

//--------------------------------------------------------------------------------------
// Input/Output structures
//--------------------------------------------------------------------------------------
struct CS_Input
{
	float3	Pos;		// Position
	uint	Weights;	// Bone weights
	uint2	Bones;		// Bone indices
	uint2	Norm;		// Normal
	uint2	UV;			// Texture coordinate
#ifdef _TANGENT_
	uint2	Tan;		// Normalized Tangent vector
#endif
};

struct CS_Output
{
	float4	Pos;		// Position
	uint2	Norm;		// Normal
	uint2	UV;			// Texture coordinate
#ifdef _TANGENT_
	uint4	Tan;		// Normalized Tangent vector
#endif
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
XUSG_VK_BINDING(0, XUSG_VK_SPACE_UAV)
RWStructuredBuffer<CS_Output>	g_rwVertices : register(u0);
XUSG_VK_BINDING(1, XUSG_VK_SPACE_SRV)
StructuredBuffer<CS_Input>		g_roVertices : register(t1);

//--------------------------------------------------------------------------------------
// Encode R16G16B16_FLOAT
//--------------------------------------------------------------------------------------
uint2 EncodeRGB16f(float3 v)
{
	return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z));
}

//--------------------------------------------------------------------------------------
// Encode R16G16B16A_FLOAT
//--------------------------------------------------------------------------------------
uint2 EncodeRGBA16f(float4 v)
{
	return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z) | (f32tof16(v.w) << 16));
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
	return f16tof32(uint3(u, u.x >> 16).xzy);
}

//--------------------------------------------------------------------------------------
// Decode R16G16B16A16_FLOAT
//--------------------------------------------------------------------------------------
float4 DecodeRGBA16f(uint2 u)
{
	return f16tof32(uint4(u, u >> 16).xzyw);
}

//--------------------------------------------------------------------------------------
// Load vertex data
//--------------------------------------------------------------------------------------
VS_Input LoadVertex(uint i)
{
	VS_Input vertex;

	CS_Input vertexIn = g_roVertices[i];
	vertex.Pos = vertexIn.Pos;
	vertex.Weights = DecodeRGBA8(vertexIn.Weights);
	vertex.Bones = DecodeRGBA8u(vertexIn.Bones.x);
	vertex.Norm = DecodeRGB16f(vertexIn.Norm);
#ifdef _TANGENT_
	vertex.Tan = DecodeRGBA16f(vertexIn.Tan);
#endif
	vertex.UV = vertexIn.UV.x;
	
	return vertex;
}

//--------------------------------------------------------------------------------------
// Store vertex data
//--------------------------------------------------------------------------------------
void StoreVertex(SkinnedInfo skinned, uint i)
{
	CS_Output output;
	output.Pos = float4(skinned.Pos, 0.0);
	output.Norm = EncodeRGB16f(skinned.Norm);
	output.UV = uint2(skinned.UV, 0);
#ifdef _TANGENT_
	output.Tan.xy = EncodeRGBA16f(skinned.Tan);
	output.Tan.zw = 0;
#endif

	g_rwVertices[i] = output;
}

//--------------------------------------------------------------------------------------
// Compute shader used for skinning the mesh for stream out
//--------------------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	VS_Input vertex = LoadVertex(DTid);
	StoreVertex(SkinVert(vertex), DTid);
}
