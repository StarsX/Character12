//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#pragma once

#ifndef	BASIC_SHADER_IDS
#define	BASIC_SHADER_IDS

// Vertex shaders
enum VertexShader : uint8_t
{
	VS_SCREEN_QUAD,

	VS_BASE_PASS,
	VS_BASE_PASS_STATIC,
	VS_ALPHA_TEST,
	VS_DEPTH = VS_ALPHA_TEST,
	VS_SKINNING,

	VS_REFLECT,
	VS_BOUND
};

// Pixel shaders
enum PixelShader : uint8_t
{
	PS_DEFERRED_SHADE,

	PS_BASE_PASS,
	PS_BASE_PASS_STATIC,
	PS_ALPHA_TEST,
	PS_DEPTH		= PS_ALPHA_TEST,
	PS_ALPHA_TEST_STATIC,
	PS_DEPTH_STATIC	= PS_ALPHA_TEST_STATIC,
	PS_OCCLUSION,
	PS_OCCLUSION_STATIC,

	PS_SS_REFLECT,
	PS_SKY,
	PS_WATER,
	PS_RESAMPLE,

	PS_POST_PROC,
	PS_TONE_MAP,
	PS_TEMPORAL_AA,
	PS_UNSHARP,
	PS_FXAA,

	PS_REFLECT,
	PS_BOUND
};

// Compute shaders
enum ComputeShader : uint8_t
{
	CS_SKINNING,
	CS_LUM_ADAPT
};

#endif
