#ifndef BAKED_SUN_SHADOW_HLSLI
#define BAKED_SUN_SHADOW_HLSLI

// Needs FogConsts.hlsli, tFogShadowAtlas (a comparison texture) and bFogShadowPages
// (StructuredBuffer<FogShadowPage>), declared by the including shader

float SampleBakedSunShadowPages(BakedSunShadowParams params, float3 position_ws, float fallback, float2 rotation)
{
	const float3 right = params.vRight.xyz;
	const float3 up = params.vUp.xyz;
	const float3 forward = params.vForward.xyz;

	const float page_size = params.vRight.w;
	const float bias = params.vUp.w;
	const float pcf_radius = params.vForward.w;

	const float2 cell = floor(position_ws.xz / page_size) - params.vGrid.xy;
	const bool in_grid = and(all(cell >= 0.0), all(cell < params.vGrid.zw));

	const uint2 safe_cell = uint2(select(in_grid, cell, float2(0.0, 0.0)));
	const FogShadowPage page = bFogShadowPages[safe_cell.x + (safe_cell.y * uint(params.vGrid.z))];

	const float2 light = float2(dot(position_ws, right), dot(position_ws, up));
	const float2 page_uv = (light - page.vLightRect.xy) * page.vLightRect.zw;
	const float compare_depth = (dot(position_ws, forward) - page.vDepth.x - bias) * page.vDepth.y;

	const float2 half_texel = 0.5 * params.vAtlas.xy;
	const float2 region_min = page.vAtlasRect.zw + half_texel;
	const float2 region_max = page.vAtlasRect.zw + page.vAtlasRect.xy - half_texel;

	const float2 radius_uv = pcf_radius * page.vLightRect.zw;
	const float2 axis_a = rotation * radius_uv;
	const float2 axis_b = float2(-rotation.y, rotation.x) * radius_uv;

	const float2 offsets[4] = { axis_a, -axis_a, axis_b, -axis_b };

	float lit = 0.0;

	[unroll]
	for (uint tap = 0; tap < 4; tap++) {
		const float2 atlas_uv = ((page_uv + offsets[tap]) * page.vAtlasRect.xy) + page.vAtlasRect.zw;
		lit += F_SampleCmpLevelZero(tFogShadowAtlas, clamp(atlas_uv, region_min, region_max), compare_depth);
	}

	lit *= 0.25;

	const bool has_page = page.vDepth.z > 0.5;
	const bool outside_page = any(saturate(page_uv) != page_uv);

	const float page_visibility = select(or(!has_page, outside_page), 1.0, lit);

	return select(in_grid, page_visibility, fallback);
}

#endif
