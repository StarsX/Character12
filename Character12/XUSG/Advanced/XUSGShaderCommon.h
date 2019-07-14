//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#ifndef BASIC_SHADER_IDS
#define BASIC_SHADER_IDS

// Vertex shaders
enum VertexShader : uint8_t
{
	VS_SCREEN_QUAD,

	VS_BASE_PASS,
	VS_BASE_PASS_STATIC,
	VS_DEPTH,
	VS_DEPTH_STATIC,
	VS_SHADOW,
	VS_SHADOW_STATIC,
	VS_SKINNING,

	VS_WATER
};

// Pixel shaders
enum PixelShader : uint8_t
{
	PS_DEFERRED_SHADE,
	PS_AMBIENT_OCCLUSION,

	PS_BASE_PASS,
	PS_BASE_PASS_STATIC,
	PS_DEPTH,
	PS_ALPHA_TEST,
	PS_ALPHA_TEST_STATIC,

	PS_SKYDOME,
	PS_SS_REFLECT,
	PS_WATER,
	PS_RESAMPLE,

	PS_POST_PROC,
	PS_TONE_MAP,
	PS_TEMPORAL_AA,
	PS_UNSHARP,
	PS_FXAA,

	PS_NULL_INDEX
};

// Compute shaders
enum ComputeShader : uint8_t
{
	CS_SKINNING,
	CS_RESAMPLE,
	CS_LUM_ADAPT
};

#endif
