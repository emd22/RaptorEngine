F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeCommon.hlsli"
#include "ProbeClipmapCommon.hlsli"

struct ProbeClipmapDebugConsts
{
	ClipmapScene Clipmap;
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Target width and height, view
	uint4 vInfo;
	/// Pre-exposure
	float4 vParams;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_Texture2D(tIrradiance0, 2, 0);
F_Texture2D(tIrradiance1, 3, 0);
F_Texture2D(tIrradiance2, 4, 0);
F_Texture2D(tMoments0, 5, 0);
F_Texture2D(tMoments1, 6, 0);
F_Texture2D(tMoments2, 7, 0);
F_StructBuffer(bState, uint, 8, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(9, 0)]] RWTexture2D<float4> tOutput;

F_FrameData(ProbeClipmapDebugConsts, Consts, 1);

/// Mirrors ProbeClipmap::eDebugView
#define VIEW_SPHERES_0 1
#define VIEW_SURFACE 2
#define VIEW_LEVELS 3
#define VIEW_SPHERES_1 4
#define VIEW_SPHERES_2 5

#define VIEWMODEL_STORED_DEPTH 0.99
#define SPHERE_RADIUS 0.15
#define SPHERE_MAX_STEPS 192
/// Receivers are pushed this much of a probe spacing off their surface before the visibility test
#define NORMAL_BIAS 0.25

float3 WorldFromDepth(float2 uv, float stored_depth)
{
	const float4 ndc = float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0);
	const float4 world = mul(ndc, Consts.mInvViewProjection);

	return world.xyz / world.w;
}

float2 TileUV(uint slot, uint size, float3 direction, float2 image_size)
{
	const float2 origin = float2(ClipmapTileOrigin(slot, size + 2)) + 1.0;
	const float2 texel = origin + (ClipmapDirectionToOct(direction) * 0.5 + 0.5) * float(size);

	return texel / image_size;
}

float3 SampleIrradiance(uint level, uint slot, float3 direction)
{
	const float2 image_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_GRID_Z) * (CLIPMAP_IRRADIANCE_SIZE + 2);
	const float2 uv = TileUV(slot, CLIPMAP_IRRADIANCE_SIZE, direction, image_size);

	float3 value = F_SampleLevel(tIrradiance2, uv, 0.0).rgb;
	value = select(level == 1, F_SampleLevel(tIrradiance1, uv, 0.0).rgb, value);
	value = select(level == 0, F_SampleLevel(tIrradiance0, uv, 0.0).rgb, value);

	return value;
}

float2 SampleMoments(uint level, uint slot, float3 direction)
{
	const uint size = select(level == 0, CLIPMAP_MOMENTS_SIZE_0, CLIPMAP_MOMENTS_SIZE);
	const float2 image_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_GRID_Z) * float(size + 2);
	const float2 uv = TileUV(slot, size, direction, image_size);

	float2 value = F_SampleLevel(tMoments2, uv, 0.0).rg;
	value = select(level == 1, F_SampleLevel(tMoments1, uv, 0.0).rg, value);
	value = select(level == 0, F_SampleLevel(tMoments0, uv, 0.0).rg, value);

	return value;
}

bool IsValid(uint level, uint slot) { return HAS_FLAG(bState[level * CLIPMAP_PROBES_PER_LEVEL + slot], CLIPMAP_STATE_VALID); }

/// The finest level whose window holds every corner of the probe cell around `position`
uint FindLevel(float3 position, out bool found)
{
	const float3 last = float3(CLIPMAP_GRID_X - 1, CLIPMAP_GRID_Y - 1, CLIPMAP_GRID_Z - 1);

	found = false;
	uint chosen = CLIPMAP_LEVELS - 1;

	for (uint level = CLIPMAP_LEVELS; level > 0; level--) {
		const ClipmapLevel clip = Consts.Clipmap.Levels[level - 1];
		const float3 local = position * clip.vParams.y - float3(clip.vOrigin.xyz);
		const bool inside = and(all(local >= 0.0), all(local < last));

		chosen = select(inside, level - 1, chosen);
		found = or(found, inside);
	}

	return chosen;
}

/// Irradiance / pi at a surface, from the eight probes around it
float3 GatherIrradiance(float3 position, float3 normal, out uint out_level, out float out_valid)
{
	bool found;
	const uint level = FindLevel(position, found);
	const ClipmapLevel clip = Consts.Clipmap.Levels[level];

	const float3 biased = position + normal * (NORMAL_BIAS * clip.vParams.x);
	const float3 local = biased * clip.vParams.y;
	const float3 base = floor(local);
	const float3 cell_frac = local - base;

	float3 irradiance = float3(0.0, 0.0, 0.0);
	float total = 0.0;
	float valid = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
		const int3 probe = int3(base) + offset;
		const uint slot = ClipmapSlot(probe);

		const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
		const float trilinear = axis.x * axis.y * axis.z;

		const bool is_valid = IsValid(level, slot);
		valid += select(is_valid, trilinear, 0.0);

		const float3 probe_position = ClipmapProbePosition(clip, probe);
		const float3 to_receiver = biased - probe_position;
		const float distance = length(to_receiver);
		const float3 direction = to_receiver / max(distance, 1e-4);

		const float2 moments = SampleMoments(level, slot, direction);
		const float visibility = select(distance < 1e-4, 1.0, ProbeDepthChebyshev(distance, moments.x, moments.y * moments.y));

		float weight = max(ProbeBackfaceWeight(position, normal, probe_position) * visibility * visibility * visibility,
						   PROBE_MIN_WEIGHT);
		weight = select(weight < PROBE_WEIGHT_CRUSH, weight * (weight * weight / (PROBE_WEIGHT_CRUSH * PROBE_WEIGHT_CRUSH)),
						weight);
		weight *= select(is_valid, trilinear, 0.0);

		irradiance += SampleIrradiance(level, slot, normal) * weight;
		total += weight;
	}

	out_level = select(found, level, CLIPMAP_LEVELS);
	out_valid = valid;

	return irradiance / max(total, 1e-6);
}

float3 LevelColour(uint level)
{
	float3 colour = float3(0.3, 0.3, 0.3);
	colour = select(level == 2, float3(0.3, 0.4, 0.95), colour);
	colour = select(level == 1, float3(0.3, 0.9, 0.3), colour);
	colour = select(level == 0, float3(0.9, 0.3, 0.3), colour);

	return colour;
}

float3 ShadeSpheres(uint level, float3 eye, float3 direction, float max_distance, float3 background)
{
	const ClipmapLevel clip = Consts.Clipmap.Levels[level];
	const float spacing = clip.vParams.x;
	const float radius = SPHERE_RADIUS * spacing;
	const float step = spacing * 0.25;

	float3 colour = background;
	bool done = false;

	[loop]
	for (uint index = 0; index < SPHERE_MAX_STEPS; index++) {
		const float t = float(index) * step;
		const int3 probe = int3(round((eye + direction * t) * clip.vParams.y));
		const float3 centre = ClipmapProbePosition(clip, probe);

		const float3 to_centre = centre - eye;
		const float along = dot(to_centre, direction);
		const float miss_sq = dot(to_centre, to_centre) - along * along;
		const float hit_t = along - sqrt(max(radius * radius - miss_sq, 0.0));

		const int3 window = probe - clip.vOrigin.xyz;
		const bool in_window = and(all(window >= 0), all(window < int3(CLIPMAP_GRID_X, CLIPMAP_GRID_Y, CLIPMAP_GRID_Z)));
		const bool is_hit = and(and(miss_sq < radius * radius, hit_t > 0.0), and(hit_t < max_distance, in_window));

		if (and(is_hit, !done)) {
			const uint slot = ClipmapSlot(probe);
			const float3 normal = normalize(eye + direction * hit_t - centre);

			colour = select(IsValid(level, slot), SampleIrradiance(level, slot, normal) * Consts.vParams.x,
							float3(1.0, 0.0, 1.0));
			done = true;
		}

		if (or(done, t > max_distance)) {
			break;
		}
	}

	return colour;
}

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
	const uint2 pixel = thread.xy;

	if (any(pixel >= Consts.vInfo.xy)) {
		return;
	}

	const uint view = Consts.vInfo.z;
	const float2 uv = (float2(pixel) + 0.5) / float2(Consts.vInfo.xy);

	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	const float3 normal = normalize(F_SampleLoad(tNormal, int3(pixel, 0)).xyz);

	const bool has_surface = and(stored_depth > 0.0, stored_depth < VIEWMODEL_STORED_DEPTH);

	const float3 eye = Consts.vEyePosition.xyz;
	const float3 surface = WorldFromDepth(uv, stored_depth);
	const float3 direction = normalize(WorldFromDepth(uv, 0.5) - eye);
	const float surface_distance = select(has_surface, length(surface - eye), 1e4);

	float3 colour = tOutput[pixel].rgb;

	if (view == VIEW_SURFACE || view == VIEW_LEVELS) {
		uint level;
		float valid;
		const float3 irradiance = GatherIrradiance(surface, normal, level, valid);

		if (has_surface) {
			colour = select(view == VIEW_SURFACE, irradiance * Consts.vParams.x, LevelColour(level) * (0.25 + 0.75 * valid));
		}
	}
	else {
		uint level = 0;
		level = select(view == VIEW_SPHERES_1, 1u, level);
		level = select(view == VIEW_SPHERES_2, 2u, level);

		colour = ShadeSpheres(level, eye, direction, surface_distance, colour);
	}

	tOutput[pixel] = float4(colour, 1.0);
}
