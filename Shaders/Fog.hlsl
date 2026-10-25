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
#include "FogConsts.hlsli"

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

F_ShadowTexture2D(tFogShadowAtlas, 9, 0);
F_StructBuffer(bFogShadowPages, FogShadowPage, 10, 0);

F_FrameData(FogConsts, Consts, 1);

#include "FogMedium.hlsli"
#include "FogLighting.hlsli"

static const float scBayer4x4[16] = {
	0.0, 8.0, 2.0, 10.0,
	12.0, 4.0, 14.0, 6.0,
	3.0, 11.0, 1.0, 9.0,
	15.0, 7.0, 13.0, 5.0,
};

PERMNOT(FOG_VOLUMETRIC);

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
