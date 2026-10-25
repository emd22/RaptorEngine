F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"
#include "ProbeCommon.hlsli"
#include "FogConsts.hlsli"

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

[[vk::image_format("rgba16f")]] F_RWTexture3D(tFroxelLighting, float4, 8, 0);

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"
#include "FogLighting.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 froxel : SV_DispatchThreadID)
{
	if (any(froxel >= uint3(FROXEL_GRID_X, FROXEL_GRID_Y, FROXEL_GRID_Z + 1))) {
		return;
	}

	const float2 uv = (float2(froxel.xy) + 0.5) / float2(FROXEL_GRID_X, FROXEL_GRID_Y);
	const float3 ray_unit_depth = FogScreenRay(uv);
	const float3 direction = normalize(ray_unit_depth);

	const bool is_tail = froxel.z == FROXEL_GRID_Z;
	const float slice = select(is_tail, float(FROXEL_GRID_Z), float(froxel.z) + 0.5);
	const float3 position_ws = FogEyePosition() + (ray_unit_depth * FroxelSliceToDepth(slice));

	const uint tile_columns = Consts.uiTileColumns;
	const uint2 tile_limit = max(uint2(tile_columns, Consts.uiTileRows), uint2(1, 1)) - uint2(1, 1);
	const uint2 tile_xy = min(uint2(uv * Consts.vScreenSize) / LIGHT_TILE_SIZE, tile_limit);

	const TileLightData tile = bLightGrid[tile_xy.x + (tile_xy.y * tile_columns)];

	const float g = Consts.fAnisotropy;

	const float rotation_angle = frac(dot(float3(froxel), float3(0.7548777, 0.5698403, 0.4142136)));
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

	tFroxelLighting[froxel] = float4(light_sum, 0.0);
}
