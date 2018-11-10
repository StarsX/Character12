//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "CHDataSize.hlsli"
#include "CSSkinning.hlsli"

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const uint	g_uWeights	= APPEND_OFFSET(0, SIZE_OF_FLOAT3);
static const uint	g_uBones	= APPEND_OFFSET(g_uWeights, SIZE_OF_UNORM4);
static const uint	g_uNormIn	= APPEND_OFFSET(g_uBones, SIZE_OF_BYTE4);
static const uint	g_uTexIn	= APPEND_OFFSET(g_uNormIn, SIZE_OF_HALF4);
static const uint	g_uTanIn	= APPEND_OFFSET(g_uTexIn, SIZE_OF_HALF2);
static const uint	g_uBiNrmIn	= APPEND_OFFSET(g_uTanIn, SIZE_OF_HALF4);
static const uint	g_uStrideIn	= APPEND_OFFSET(g_uBiNrmIn, SIZE_OF_HALF4);

static const uint	g_uNormOut	= APPEND_OFFSET(0, SIZE_OF_FLOAT3);
static const uint	g_uTexOut	= APPEND_OFFSET(g_uNormOut, SIZE_OF_HALF4);
static const uint	g_uTanOut	= APPEND_OFFSET(g_uTexOut, SIZE_OF_HALF2);
static const uint	g_uBiNrmOut	= APPEND_OFFSET(g_uTanOut, SIZE_OF_HALF4);
static const uint	g_uStrideOut = APPEND_OFFSET(g_uBiNrmOut, SIZE_OF_HALF4);

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWByteAddressBuffer			g_rwVertices	: register (u0);
ByteAddressBuffer			g_roVertices	: register (t0);

//--------------------------------------------------------------------------------------
// Encode R16G16B16_FLOAT
//--------------------------------------------------------------------------------------
uint2 EncodeRGB16f(const float3 v)
{
	return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z));
}

//--------------------------------------------------------------------------------------
// Decode R8G8B8A8_UINT
//--------------------------------------------------------------------------------------
uint4 DecodeRGBA8u(const uint u)
{
	return uint4(u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff, u >> 24);
}

//--------------------------------------------------------------------------------------
// Decode R8G8B8A8_UNORM
//--------------------------------------------------------------------------------------
float4 DecodeRGBA8(const uint u)
{
	return DecodeRGBA8u(u) / 255.0;
}

//--------------------------------------------------------------------------------------
// Decode R16G16B16_FLOAT
//--------------------------------------------------------------------------------------
float3 DecodeRGB16f(const uint2 u)
{
	const uint2 v = { u.x & 0xffff, (u.x >> 16) & 0xffff };

	return float3(f16tof32(v), f16tof32(u.y));
}

//--------------------------------------------------------------------------------------
// Load vertex data
//--------------------------------------------------------------------------------------
VS_Input LoadVertex(const uint uIdx)
{
	VS_Input vertex;

	const uint uBaseIdx = g_uStrideIn * uIdx;
	const uint4 vPosWeight = g_roVertices.Load4(uBaseIdx);
	const uint4 vBoneNormTex = g_roVertices.Load4(uBaseIdx + g_uBones);
	const uint4 vTanBiNrm = g_roVertices.Load4(uBaseIdx + g_uTanIn);

	vertex.Pos = asfloat(vPosWeight.xyz);
	vertex.Weights = DecodeRGBA8(vPosWeight.w);
	vertex.Bones = DecodeRGBA8u(vBoneNormTex.x);
	vertex.Norm = DecodeRGB16f(vBoneNormTex.yz);
	vertex.Tan = DecodeRGB16f(vTanBiNrm.xy);
	vertex.BiNorm = DecodeRGB16f(vTanBiNrm.zw);
	vertex.Tex = vBoneNormTex.w;
	
	return vertex;
}

//--------------------------------------------------------------------------------------
// Store vertex data
//--------------------------------------------------------------------------------------
void StoreVertex(const SkinnedInfo skinned, const uint uIdx)
{
	const uint uBaseIdx = g_uStrideOut * uIdx;
	const uint3 vNormTex = { EncodeRGB16f(skinned.Norm), skinned.Tex };
	const uint4 vTanBiNrm = { EncodeRGB16f(skinned.Tan), EncodeRGB16f(skinned.BiNorm) };

	g_rwVertices.Store3(uBaseIdx, asuint(skinned.Pos.xyz));
	g_rwVertices.Store3(uBaseIdx + g_uNormOut, vNormTex);
	g_rwVertices.Store4(uBaseIdx + g_uTanOut, vTanBiNrm);
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
