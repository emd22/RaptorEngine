F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeClipmapCommon.hlsli"
#include "ScreenProbeCommon.hlsli"

// Filters the screen probes across their 3x3 neighbours ([1 2 1] each way), keeping the ones on the probe's plane and
// facing its way, so the resolve can read the 2x2 around a pixel bilinearly: together that is the 4x4 tent it read
// before, for a quarter of the loads per pixel.

F_StructBuffer(bScreenProbes, float4, 0, 0);
F_RWStructBuffer(bFilteredProbes, float4, 1, 0);

struct ScreenProbeFilterConsts
{
	float4 vEyePosition;
	/// Tiles across and down
	uint4 vTiles;
};

F_FrameData(ScreenProbeFilterConsts, Consts, 1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const int2 tiles = int2(Consts.vTiles.xy);
	const int2 centre_tile = int2(id.xy);

	if (any(centre_tile >= tiles)) {
		return;
	}

	const uint centre = uint(centre_tile.x + centre_tile.y * tiles.x) * SCREEN_PROBE_STRIDE;
	const float4 position = bScreenProbes[centre];
	const float4 normal = bScreenProbes[centre + 1];

	const float eye_distance = length(Consts.vEyePosition.xyz - position.xyz);
	const float plane_tolerance = eye_distance * SCREEN_PROBE_PLANE_SHARE + 0.01;

	float4 sh_r = float4(0.0, 0.0, 0.0, 0.0);
	float4 sh_g = float4(0.0, 0.0, 0.0, 0.0);
	float4 sh_b = float4(0.0, 0.0, 0.0, 0.0);
	float total = 0.0;

	[unroll]
	for (uint neighbour = 0; neighbour < 9; neighbour++) {
		const int2 offset = int2(neighbour % 3, neighbour / 3) - 1;
		const int2 tile = centre_tile + offset;
		const bool in_bounds = and(all(tile >= 0), all(tile < tiles));
		const int2 clamped = clamp(tile, int2(0, 0), tiles - 1);
		const uint index = uint(clamped.x + clamped.y * tiles.x) * SCREEN_PROBE_STRIDE;

		const float4 other_position = bScreenProbes[index];
		const float3 other_normal = bScreenProbes[index + 1].xyz;

		const float spatial = float((2 - abs(offset.x)) * (2 - abs(offset.y)));
		const float plane = abs(dot(other_position.xyz - position.xyz, normal.xyz));
		const float plane_weight = saturate(1.0 - plane / plane_tolerance);
		const float normal_weight = GIPow8(saturate(dot(normal.xyz, other_normal)));
		const bool is_usable = and(in_bounds, other_position.w > 0.0);
		const float weight = select(is_usable, spatial * plane_weight * normal_weight, 0.0);

		sh_r += bScreenProbes[index + 2] * weight;
		sh_g += bScreenProbes[index + 3] * weight;
		sh_b += bScreenProbes[index + 4] * weight;
		total += weight;
	}

	// A placed probe always counts itself fully, so only an unplaced one has no weight, and its coefficients are zero
	const float inv_total = 1.0 / max(total, 1e-6);
	const uint filtered = uint(centre_tile.x + centre_tile.y * tiles.x) * SCREEN_PROBE_FILTERED_STRIDE;
	const float4 plane = float4(normal.xyz, dot(normal.xyz, position.xyz));

	// An unplaced probe's zero plane gives it no normal weight in the resolve
	bFilteredProbes[filtered] = select(position.w > 0.0, plane, float4(0.0, 0.0, 0.0, 0.0));
	bFilteredProbes[filtered + 1] = asfloat(uint4(ScreenProbePackHalf4(sh_r * inv_total),
												  ScreenProbePackHalf4(sh_g * inv_total)));
	bFilteredProbes[filtered + 2] = asfloat(uint4(ScreenProbePackHalf4(sh_b * inv_total), 0u, 0u));
}
