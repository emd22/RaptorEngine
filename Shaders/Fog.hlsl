#include "./Helper.hlsl"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
	int iVertexIndex : SV_VertexID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);

	output.vUV = out_uv;
	output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

	return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

#include "LightingCommon.hlsli"
#include "ProbeCommon.hlsli"

struct FSInput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

struct FSOutput
{
	float4 vFog : SV_TARGET0;
};

F_Texture2D(tDepth, 0, 0);

F_CBuffer(FSLightBuffer, 1, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_StructBuffer(bLightGrid, TileLightData, 2, 0);
F_StructBuffer(bLightIndexList, uint, 3, 0);

F_ShadowTexture2D(tShadowAtlas, 4, 0);

F_StructBuffer(bProbeBuffer, ProbeSHData, 5, 0);
F_StructBuffer(bProbeVolume, ProbeVolume, 6, 0);
F_StructBuffer(bProbeGrid, uint, 7, 0);

struct FogConsts
{
	float4x4 mFarSunMatrix;
	float4 vFarSunAtlasRect;
	float4 vRays[4];
	float4 vOriginAndDensity;
	float4 vCameraOffsetAndHaze;
	float4 vAlbedoAndFalloff;
	float fDepthA;
	float fDepthB;
	float fMarchDistance;
	float fPreExposure;
	float fAnisotropy;
	float fAmbientScale;
	uint uiTileColumns;
	uint uiTileRows;
	uint uiStepCount;
	uint uiFlags;
	float fJitterOffset;
	uint uiNoiseShift;
	float2 vPad;
};

F_FrameData(FogConsts, Consts, 1);

#define FOG_FLAG_PROBE_SUN_VISIBILITY 0x01
#define FOG_FLAG_FAR_SUN_SHADOW 0x02
#define FOG_FLAG_NEAR_SUN_DYNAMIC_ONLY 0x04

#define FOG_CASCADE_FADE 0.1

#define FOG_FAST_SEGMENTS 8

#define FOG_SHADOW_BIAS -0.000005f

#define FOG_PCF_RADIUS 1.5

#define FOG_SH_L0_TO_RADIANCE (0.282095 * FX_MATH_1_OVER_PI)

static const float scBayer4x4[16] = {
	0.0, 8.0, 2.0, 10.0,
	12.0, 4.0, 14.0, 6.0,
	3.0, 11.0, 1.0, 9.0,
	15.0, 7.0, 13.0, 5.0,
};

struct FogRay
{
	float3 vOrigin;
	float3 vDirection;
	float fOriginDensity;
	float fFalloffAlongRay;
	float fFalloff;
	float fHaze;
};

FogRay MakeFogRay(float3 direction)
{
	FogRay ray;

	ray.vOrigin = Consts.vOriginAndDensity.xyz;
	ray.vDirection = direction;
	ray.fFalloff = Consts.vAlbedoAndFalloff.w;
	ray.fFalloffAlongRay = ray.fFalloff * direction.y;
	ray.fHaze = Consts.vCameraOffsetAndHaze.w;
	ray.fOriginDensity = Consts.vOriginAndDensity.w;

	return ray;
}

float FogOpticalDepth(FogRay ray, float t)
{
	const float x = max(ray.fFalloffAlongRay * t, -80.0);
	const float exact = (1.0 - exp(-x)) / ray.fFalloffAlongRay;
	const float series = t * (1.0 - (0.5 * x));
	const float integral = select(abs(x) < 1e-4, series, exact);

	return (ray.fOriginDensity * integral) + (ray.fHaze * t);
}

float FogTransmittance(FogRay ray, float t)
{
	return exp(-FogOpticalDepth(ray, t));
}

float PhaseHenyeyGreenstein(float cos_theta, float g)
{
	const float g2 = g * g;
	const float denom = max(1.0 + g2 - (2.0 * g * cos_theta), 1e-4);

	return (1.0 - g2) / (4.0 * FX_MATH_PI * denom * sqrt(denom));
}

float LinearViewDepth(float raw_depth)
{
	return Consts.fDepthB / max(raw_depth - Consts.fDepthA, 1e-12);
}

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

PERMIF(FOG_VOLUMETRIC);

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

PERMELSE();

float3 DirectionalInscatterShadowed(TileLightData tile, float3 position_ws, float3 direction, float g,
									float sun_fallback, float2 rotation)
{
	float3 sum = float3(0.0, 0.0, 0.0);

	for (uint tile_light = 0; tile_light < tile.Count; tile_light++) {
		const Light light = Lights[bLightIndexList[tile.StartIndex + tile_light]];

		const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;

		if (!is_directional) {
			continue;
		}

		const float phase = PhaseHenyeyGreenstein(dot(light.vLightPosition, direction), g);
		const float visibility = SampleSunShadow(light, position_ws, sun_fallback, rotation);

		sum += light.vLightColor * (light.fIntensity * phase * visibility);
	}

	return sum;
}

PERMEND();

FSOutput main(FSInput input)
{
	FSOutput output;

	uint depth_width;
	uint depth_height;
	F_TextureName(tDepth).GetDimensions(depth_width, depth_height);

	const int2 depth_size = int2(depth_width, depth_height);
	const int2 depth_pixel = min(int2(input.vPosition.xy) * 2 + 1, depth_size - 1);

	const float raw_depth = F_SampleLoad(tDepth, int3(depth_pixel, 0)).r;
	const float view_depth = LinearViewDepth(raw_depth);

	const float2 uv = (float2(depth_pixel) + 0.5) / float2(depth_size);
	const float3 ray_top = lerp(Consts.vRays[0].xyz, Consts.vRays[1].xyz, uv.x);
	const float3 ray_bottom = lerp(Consts.vRays[2].xyz, Consts.vRays[3].xyz, uv.x);
	const float3 ray_unit_depth = lerp(ray_top, ray_bottom, uv.y);

	const float3 origin_to_surface = Consts.vCameraOffsetAndHaze.xyz + (ray_unit_depth * view_depth);
	const float distance = length(origin_to_surface);
	const float3 direction = origin_to_surface / max(distance, 1e-6);

	const FogRay ray = MakeFogRay(direction);

	const float g = Consts.fAnisotropy;
	const float ambient_scale = Consts.fAmbientScale;

	const uint tile_columns = Consts.uiTileColumns;
	const uint tile_rows = Consts.uiTileRows;

	uint2 tile_xy = uint2(depth_pixel) / LIGHT_TILE_SIZE;
	const uint2 tile_limit = max(uint2(tile_columns, tile_rows), uint2(1, 1)) - uint2(1, 1);
	tile_xy = min(tile_xy, tile_limit);

	const TileLightData tile = bLightGrid[tile_xy.x + (tile_xy.y * tile_columns)];

	const uint2 noise_shift = uint2(Consts.uiNoiseShift & 3u, (Consts.uiNoiseShift >> 2) & 3u);
	const uint2 noise_pixel = (uint2(input.vPosition.xy) + noise_shift) % 4;
	const float jitter = (scBayer4x4[(noise_pixel.y * 4) + noise_pixel.x] + Consts.fJitterOffset) / 16.0;

	float2 pcf_rotation;
	sincos(jitter * (2.0 * FX_MATH_PI), pcf_rotation.y, pcf_rotation.x);

	float3 inscatter = float3(0.0, 0.0, 0.0);
	float transmittance = 1.0;

PERMIF(FOG_VOLUMETRIC);
	const float3 sun_inscatter = DirectionalInscatter(tile, direction, g);

	const float march_end = min(distance, Consts.fMarchDistance);
	const uint step_count = max(Consts.uiStepCount, 1u);
	const float inv_steps = 1.0 / float(step_count);

	float segment_start_transmittance = 1.0;

	for (uint march_step = 0; march_step < step_count; march_step++) {
		const float u0 = float(march_step) * inv_steps;
		const float u1 = u0 + inv_steps;

		const float t0 = march_end * u0 * u0;
		const float t1 = march_end * u1 * u1;
		const float t_sample = lerp(t0, t1, jitter);

		const float segment_end_transmittance = FogTransmittance(ray, t1);
		const float weight = segment_start_transmittance - segment_end_transmittance;
		segment_start_transmittance = segment_end_transmittance;

		const float3 position_ws = ray.vOrigin + (direction * t_sample);
		const float4 probe = SampleFogProbes(position_ws);
		const float sun_fallback = ProbeSunVisibility(probe);

		float3 step_light = probe.rgb * ambient_scale;

		for (uint tile_light = 0; tile_light < tile.Count; tile_light++) {
			const Light light = Lights[bLightIndexList[tile.StartIndex + tile_light]];

			step_light += LightInscatter(light, position_ws, direction, g, sun_fallback, pcf_rotation);
		}

		inscatter += step_light * weight;
	}

	const float march_transmittance = segment_start_transmittance;
	const float4 end_probe = SampleFogProbes(ray.vOrigin + (direction * march_end));

	transmittance = FogTransmittance(ray, distance);

	const float tail_weight = march_transmittance - transmittance;
	const float3 tail_light = (end_probe.rgb * ambient_scale) + (sun_inscatter * ProbeSunVisibility(end_probe));

	inscatter += tail_light * tail_weight;
PERMELSE();
	const float inv_segments = 1.0 / float(FOG_FAST_SEGMENTS);

	[unroll]
	for (uint segment = 0; segment < FOG_FAST_SEGMENTS; segment++) {
		const float u0 = float(segment) * inv_segments;
		const float u1 = u0 + inv_segments;

		const float t0 = distance * u0 * u0;
		const float t1 = distance * u1 * u1;
		const float t_sample = lerp(t0, t1, jitter);

		const float segment_end_transmittance = FogTransmittance(ray, t1);
		const float weight = transmittance - segment_end_transmittance;
		transmittance = segment_end_transmittance;

		const float3 position_ws = ray.vOrigin + (direction * t_sample);
		const float4 probe = SampleFogProbes(position_ws);
		const float3 sun = DirectionalInscatterShadowed(tile, position_ws, direction, g, ProbeSunVisibility(probe),
														pcf_rotation);
		const float3 segment_light = (probe.rgb * ambient_scale) + sun;

		inscatter += segment_light * weight;
	}
PERMEND();

	const float3 albedo = Consts.vAlbedoAndFalloff.rgb;
	const float pre_exposure = Consts.fPreExposure;

	output.vFog = float4(inscatter * albedo * pre_exposure, transmittance);

	return output;
}
