F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"
#include "ProbeCommon.hlsli"
#include "FogConsts.hlsli"

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

F_ShadowTexture2D(tFogShadowAtlas, 9, 0);
F_StructBuffer(bFogShadowPages, FogShadowPage, 10, 0);

[[vk::image_format("rgba16f")]] F_RWTexture3D(tFroxelLighting, float4, 8, 0);

F_Texture3D(tFroxelHistory, 11, 0);

[[vk::image_format("rgba16f")]] F_RWTexture3D(tFroxelSurface, float4, 12, 0);

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"
#include "FogLighting.hlsli"

#define FROXEL_SURFACE_MARGIN 0.1
#define FROXEL_DEPTH_TAPS 3
#define FROXEL_DEPTH_TAP_COUNT (FROXEL_DEPTH_TAPS * FROXEL_DEPTH_TAPS)
#define FROXEL_NO_SURFACE 60000.0
#define VIEWMODEL_MIN_RAW_DEPTH 0.99
#define FROXEL_SURFACE_GAP 0.25
#define FROXEL_SURFACE_RELATIVE_GAP 0.15
#define FROXEL_HISTORY_DEPTH_TOLERANCE 0.25
#define FROXEL_HISTORY_RELATIVE_DEPTH_TOLERANCE 0.1

void LoadColumnDepths(uint2 column, out float depths[FROXEL_DEPTH_TAP_COUNT])
{
	uint depth_width;
	uint depth_height;
	F_TextureName(tDepth).GetDimensions(depth_width, depth_height);

	const int2 depth_limit = int2(depth_width, depth_height) - int2(1, 1);
	const float2 cell_size = float2(depth_width, depth_height) / float2(FROXEL_GRID_X, FROXEL_GRID_Y);

	[unroll]
	for (uint tap = 0; tap < FROXEL_DEPTH_TAP_COUNT; tap++) {
		const uint2 tap_xy = uint2(tap % FROXEL_DEPTH_TAPS, tap / FROXEL_DEPTH_TAPS);
		const float2 fraction = (float2(tap_xy) + 0.5) / float(FROXEL_DEPTH_TAPS);
		const int2 pixel = min(int2((float2(column) + fraction) * cell_size), depth_limit);

		const float raw_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
		const bool is_viewmodel = raw_depth >= VIEWMODEL_MIN_RAW_DEPTH;

		depths[tap] = select(is_viewmodel, FROXEL_NO_SURFACE, LinearViewDepth(raw_depth));
	}
}

float NearestColumnDepth(float depths[FROXEL_DEPTH_TAP_COUNT])
{
	float nearest = FROXEL_NO_SURFACE;

	[unroll]
	for (uint tap = 0; tap < FROXEL_DEPTH_TAP_COUNT; tap++) {
		nearest = min(nearest, depths[tap]);
	}

	return nearest;
}

float ConnectedSurfaceLimit(float depths[FROXEL_DEPTH_TAP_COUNT], float nearest)
{
	float limit = nearest;

	[unroll]
	for (uint pass = 0; pass < FROXEL_DEPTH_TAP_COUNT; pass++) {
		[unroll]
		for (uint tap = 0; tap < FROXEL_DEPTH_TAP_COUNT; tap++) {
			const float gap = FROXEL_SURFACE_GAP + (FROXEL_SURFACE_RELATIVE_GAP * limit);
			const bool is_connected = depths[tap] <= limit + gap;

			limit = select(is_connected, max(limit, depths[tap]), limit);
		}
	}

	return limit;
}

float InterleavedGradientNoise(float2 position)
{
	return frac(52.9829189 * frac(dot(position, float2(0.06711056, 0.00583715))));
}

[numthreads(8, 8, 1)]
void main(uint3 froxel : SV_DispatchThreadID)
{
	if (any(froxel >= uint3(FROXEL_GRID_X, FROXEL_GRID_Y, FROXEL_GRID_Z + 1))) {
		return;
	}

	const float jitter_amount = Consts.fJitterAmount;

	const float2 column_jitter =
		(frac(float(Consts.uiNoiseShift + 1) * float2(0.7548777, 0.5698403)) - 0.5) * jitter_amount;

	const float2 grid_size = float2(FROXEL_GRID_X, FROXEL_GRID_Y);
	const float2 center_uv = (float2(froxel.xy) + 0.5) / grid_size;
	const float2 uv = (float2(froxel.xy) + 0.5 + column_jitter) / grid_size;

	const float3 center_ray = FogScreenRay(center_uv);
	const float3 ray_unit_depth = FogScreenRay(uv);
	const float3 direction = normalize(ray_unit_depth);

	const float slice_noise = frac(InterleavedGradientNoise(float2(froxel.xy)) + (float(froxel.z) * 0.6180340));
	const float slice_jitter = 0.5 + ((frac(slice_noise + Consts.fJitterOffset) - 0.5) * jitter_amount);

	const bool is_tail = froxel.z == FROXEL_GRID_Z;
	const float slice = select(is_tail, float(FROXEL_GRID_Z), float(froxel.z) + slice_jitter);
	const float slice_depth = FroxelSliceToDepth(slice);

	float column_depths[FROXEL_DEPTH_TAP_COUNT];
	LoadColumnDepths(froxel.xy, column_depths);

	const float surface_depth = NearestColumnDepth(column_depths);
	const float clamp_depth = max(surface_depth - FROXEL_SURFACE_MARGIN, 0.0);

	[branch]
	if (froxel.z == 0) {
		const float surface_limit = ConnectedSurfaceLimit(column_depths, surface_depth);

		tFroxelSurface[uint3(froxel.xy, 0)] = float4(surface_depth, surface_limit, 0.0, 0.0);
	}

	const float sample_depth = select(is_tail, slice_depth, min(slice_depth, clamp_depth));
	const float3 position_ws = FogEyePosition() + (ray_unit_depth * sample_depth);

	const float history_depth = min(FroxelSliceToDepth(float(froxel.z) + 0.5), clamp_depth);
	const float3 history_ws = FogEyePosition() + (center_ray * history_depth);

	const uint tile_columns = Consts.uiTileColumns;
	const uint2 tile_limit = max(uint2(tile_columns, Consts.uiTileRows), uint2(1, 1)) - uint2(1, 1);
	const uint2 tile_xy = min(uint2(saturate(uv) * Consts.vScreenSize) / LIGHT_TILE_SIZE, tile_limit);

	const TileLightData tile = bLightGrid[tile_xy.x + (tile_xy.y * tile_columns)];

	const float g = Consts.fAnisotropy;

	const float rotation_angle =
		frac(dot(float3(froxel), float3(0.7548777, 0.5698403, 0.4142136)) + Consts.fJitterOffset);
	float2 pcf_rotation;
	sincos(rotation_angle * (2.0 * FX_MATH_PI), pcf_rotation.y, pcf_rotation.x);

	const float4 probe = SampleFogProbes(position_ws);
	const float sun_fallback = ProbeSunVisibility(probe);

	float3 light_sum = probe.rgb * Consts.fAmbientScale;

	const uint shadowed_count = select(is_tail, 0u, tile.Count);

	for (uint tile_light = 0; tile_light < shadowed_count; tile_light++) {
		const Light light = Lights[bLightIndexList[tile.StartIndex + tile_light]];

		light_sum += LightInscatter(light, position_ws, direction, g, sun_fallback, pcf_rotation);
	}

	const uint tail_count = select(is_tail, tile.Count, 0u);

	for (uint tail_light = 0; tail_light < tail_count; tail_light++) {
		const Light light = Lights[bLightIndexList[tile.StartIndex + tail_light]];

		const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;
		const float phase = PhaseHenyeyGreenstein(dot(light.vLightPosition, direction), g);
		const float weight = select(is_directional, light.fIntensity * phase * sun_fallback, 0.0);

		light_sum += light.vLightColor * weight;
	}

	const float4 prev_clip = mul(float4(history_ws, 1.0), Consts.mPrevCameraMatrix);
	const float prev_depth = prev_clip.w;
	const float2 prev_uv = ((prev_clip.xy / max(prev_depth, 1e-6)) * 0.5) + 0.5;

	const float3 history_uvw = float3(prev_uv, FroxelDepthToSlice(prev_depth) / float(FROXEL_GRID_Z + 1));
	const float4 history = F_SampleLevel(tFroxelHistory, history_uvw, 0);

	const float history_tolerance =
		FROXEL_HISTORY_DEPTH_TOLERANCE + (FROXEL_HISTORY_RELATIVE_DEPTH_TOLERANCE * prev_depth);

	const bool in_history_grid =
		and(and(prev_depth > 0.0, prev_depth < Consts.fMarchDistance), all(saturate(prev_uv) == prev_uv));
	const bool same_surface = history.a >= prev_depth - history_tolerance;
	const bool use_history =
		and(and(Consts.fHistoryBlend > 0.0, !is_tail), and(in_history_grid, same_surface));

	const float3 lighting = select(use_history, lerp(history.rgb, light_sum, Consts.fHistoryBlend), light_sum);

	tFroxelLighting[froxel] = float4(lighting, select(is_tail, 0.0, history_depth));
}
