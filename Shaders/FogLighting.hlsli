float4 SampleFogProbes(float3 position_ws)
{
	const uint volume_count = GetProbeVolumeCount(bProbeVolume);

	if (volume_count == 0) {
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	const ProbeVolume volume = bProbeVolume[SelectProbeVolume(position_ws, bProbeVolume, volume_count)];

	uint3 base;
	float3 cell_frac;
	ProbeVolumeCell(position_ws, volume, base, cell_frac);

	float4 sum = float4(0.0, 0.0, 0.0, 0.0);
	float total_weight = 0.0;

	[unroll]
	for (uint corner = 0; corner < 8; corner++) {
		float trilinear;
		const uint index = ProbeCellCorner(volume, bProbeGrid, base, cell_frac, corner, trilinear);

		const bool placed = index < PROBE_GRID_SOLID;
		const uint safe_index = select(placed, index, 0u);
		const float weight = select(placed, trilinear, 0.0);

		const float3 radiance = bProbeBuffer[safe_index].SH[0].rgb * FOG_SH_L0_TO_RADIANCE;
		const float sun_visibility = bProbeBuffer[safe_index].SH[3].w;

		sum += float4(max(radiance, 0.0), sun_visibility) * weight;
		total_weight += weight;
	}

	const float4 average = sum / max(total_weight, 1e-6);

	return select(total_weight > 1e-6, average, float4(0.0, 0.0, 0.0, 1.0));
}

float ProbeSunVisibility(float4 probe_sample)
{
	const bool use_probe_sun = HAS_FLAG(Consts.uiFlags, FOG_FLAG_PROBE_SUN_VISIBILITY);

	return select(use_probe_sun, saturate(probe_sample.a), 1.0);
}

float3 DirectionalInscatter(TileLightData tile, float3 direction, float g)
{
	float3 sum = float3(0.0, 0.0, 0.0);

	for (uint tile_light = 0; tile_light < tile.Count; tile_light++) {
		const Light light = Lights[bLightIndexList[tile.StartIndex + tile_light]];

		const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;
		const float phase = PhaseHenyeyGreenstein(dot(light.vLightPosition, direction), g);
		const float weight = select(is_directional, light.fIntensity * phase, 0.0);

		sum += light.vLightColor * weight;
	}

	return sum;
}

float SampleShadowPcf(float2 atlas_uv, float4 atlas_rect, float compare_z, float2 rotation)
{
	const float2 texel = 1.0 / float2(SHADOW_ATLAS_WIDTH, SHADOW_ATLAS_HEIGHT);
	const float2 region_min = atlas_rect.zw + (0.5 * texel);
	const float2 region_max = atlas_rect.zw + atlas_rect.xy - (0.5 * texel);

	const float2 axis_a = rotation * (FOG_PCF_RADIUS * texel);
	const float2 axis_b = float2(-rotation.y, rotation.x) * (FOG_PCF_RADIUS * texel);

	float lit = F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv + axis_a, region_min, region_max), compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv - axis_a, region_min, region_max), compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv + axis_b, region_min, region_max), compare_z);
	lit += F_SampleCmpLevelZero(tShadowAtlas, clamp(atlas_uv - axis_b, region_min, region_max), compare_z);

	return lit * 0.25;
}

float SampleFogShadow(Light light, float3 position_ws, float outside_visibility, float2 rotation)
{
	const float4 atlas_rect = light.vShadowAtlasRect;

	if (atlas_rect.x <= 0.0) {
		return 1.0;
	}

	const float4 position_ls = mul(float4(position_ws, 1.0), light.LightCameraMatrix);
	const float3 position_ndc = position_ls.xyz / position_ls.w;

	const float2 shadow_uv = position_ndc.xy * 0.5 + 0.5;
	const float shadow_z = 1.0 - position_ndc.z;

	const bool behind = position_ls.w <= 0.0;
	const bool outside = or(behind, or(any(saturate(shadow_uv) != shadow_uv), shadow_z <= 0.0));

	const float2 atlas_uv = shadow_uv * atlas_rect.xy + atlas_rect.zw;
	const float lit = SampleShadowPcf(atlas_uv, atlas_rect, shadow_z + FOG_SHADOW_BIAS, rotation);

	return select(outside, outside_visibility, lit);
}

float SampleSunCascade(float4x4 light_matrix, float4 atlas_rect, float3 position_ws, float2 rotation,
					   out float coverage)
{
	const float4 position_ls = mul(float4(position_ws, 1.0), light_matrix);
	const float3 position_ndc = position_ls.xyz / position_ls.w;

	const float2 shadow_uv = position_ndc.xy * 0.5 + 0.5;
	const float shadow_z = 1.0 - position_ndc.z;

	const float2 edge_distance = min(shadow_uv, 1.0 - shadow_uv);
	const bool in_depth_range = and(shadow_z > 0.0, shadow_z <= 1.0);
	coverage = saturate(min(edge_distance.x, edge_distance.y) / FOG_CASCADE_FADE) * select(in_depth_range, 1.0, 0.0);

	const float2 atlas_uv = shadow_uv * atlas_rect.xy + atlas_rect.zw;

	return SampleShadowPcf(atlas_uv, atlas_rect, shadow_z + FOG_SHADOW_BIAS, rotation);
}

float SampleSunShadow(Light light, float3 position_ws, float fallback, float2 rotation)
{
	float near_coverage;
	float far_coverage;

	const float near_lit = SampleSunCascade(light.LightCameraMatrix, light.vShadowAtlasRect, position_ws, rotation,
											near_coverage);
	const float far_lit = SampleSunCascade(Consts.mFarSunMatrix, Consts.vFarSunAtlasRect, position_ws, rotation,
										   far_coverage);

	const bool has_near = light.vShadowAtlasRect.x > 0.0;
	const bool has_far = HAS_FLAG(Consts.uiFlags, FOG_FLAG_FAR_SUN_SHADOW);

	near_coverage = select(has_near, near_coverage, 0.0);
	far_coverage = select(has_far, far_coverage, 0.0);

	const float far_visibility = lerp(fallback, far_lit, far_coverage);

	const bool near_dynamic_only = HAS_FLAG(Consts.uiFlags, FOG_FLAG_NEAR_SUN_DYNAMIC_ONLY);
	const float blended = lerp(far_visibility, near_lit, near_coverage);
	const float combined = lerp(1.0, near_lit, near_coverage) * far_visibility;

	return select(near_dynamic_only, combined, blended);
}

float3 LightInscatter(Light light, float3 position_ws, float3 direction, float g, float sun_fallback,
					  float2 rotation)
{
	const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;
	const bool is_spot = light.uiLightType == FX_LIGHT_TYPE_SPOT;

	const float3 to_light = light.vLightPosition - position_ws;
	const float dist_sq = dot(to_light, to_light);

	const float3 L = select(is_directional, light.vLightPosition, to_light * rsqrt(max(dist_sq, 1e-8)));

	float attenuation = light.fIntensity * select(is_directional, 1.0, AttenuationSmooth(dist_sq, light.fInvRadiusSq));
	attenuation *= select(is_spot, AttenuationSpot(L, light), 1.0);

	float visibility;

	if (is_directional) {
		visibility = SampleSunShadow(light, position_ws, sun_fallback, rotation);
	}
	else {
		visibility = SampleFogShadow(light, position_ws, 1.0, rotation);
	}

	return light.vLightColor * (attenuation * visibility * PhaseHenyeyGreenstein(dot(L, direction), g));
}
