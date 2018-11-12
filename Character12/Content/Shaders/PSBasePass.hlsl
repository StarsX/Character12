//--------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------
#define	_BASEPASS_
#define	_CHARACTER_

#include "SHCommon.hlsli"

Texture2D g_albedo;
SamplerState g_sampler;

float4 main(PS_Input input) : SV_TARGET
{
	const float4 albedo = g_albedo.Sample(g_sampler, input.Tex);

	const float3 norm = normalize(input.Norm);
	const float lightAmt = max(dot(norm, float3(-0.5, 1.0, -1.0)), 0.0);

	return float4(sqrt(albedo.xyz * (lightAmt + 0.25)), albedo.w);
}
