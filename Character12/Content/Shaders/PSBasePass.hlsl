//--------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------
#define	_BASEPASS_
#define	_CHARACTER_

#include "SHCommon.hlsli"

float4 main(PS_Input input) : SV_TARGET
{
	const float3 norm = normalize(input.Norm);

	return float4(dot(norm, float3(-0.5, 1.0, -1.0)).xxx, 1.0f);
}
