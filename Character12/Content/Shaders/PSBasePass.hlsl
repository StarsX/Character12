//--------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------
#define	_BASEPASS_
#define	_CHARACTER_

#include "SHCommon.hlsli"

float4 main(PS_Input input) : SV_TARGET
{
	const float3 norm = normalize(input.Norm);
	const float lightAmt = max(dot(norm, float3(-0.5, 1.0, -1.0)), 0.0);

	return float4((lightAmt + 0.25).xxx, 1.0);
}
