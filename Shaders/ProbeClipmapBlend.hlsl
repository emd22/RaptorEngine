F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeClipmapConsts.hlsli"

F_StructBuffer(bJobs, uint, 0, 0);
F_StructBuffer(bRays, float4, 1, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(2, 0)]] RWTexture2D<float4> tIrradiance;
[[vk::image_format("rg16f")]] [[vk::binding(5, 0)]] RWTexture2D<float2> tMoments;

F_RWStructBuffer(bProbes, uint4, 9, 0);

F_FrameData(ProbeClipmapConsts, Consts, 1);

#define CLIPMAP_SHORT_BLEND 0.25
/// Back face hits are kept this much nearer in the depth moments
#define CLIPMAP_BACKFACE_DEPTH 0.2
/// Back face rays are left out of a texel's irradiance, but a texel that saw mostly back faces is at most this many
/// times brighter for it
#define CLIPMAP_BACKFACE_BRIGHTEN 2.0
/// ...and only in a probe that saw fewer back faces than this. More is a probe partly in a wall, whose back face rays
/// are the wall and are kept as black, or it would be lit by what it sees through the gaps.
#define CLIPMAP_STRAY_BACKFACES 0.1

/// Weight of each of the six neighbouring probes' stored light against a probe's own new rays
#define CLIPMAP_NEIGHBOUR_SHARE 0.25
/// A neighbour is shared with when the probe's depth towards it reaches at least this much of the way
#define CLIPMAP_NEIGHBOUR_VISIBLE 0.9

/// A new probe starts from the next coarser level's light where it is, worth this many of its own updates
#define CLIPMAP_PRIOR_UPDATES 16
/// ...where that level's filled probes around it have at least this much of the trilinear weight
#define CLIPMAP_PRIOR_MIN_WEIGHT 0.25

static const int3 scNeighbourSteps[6] = { int3(1, 0, 0), int3(-1, 0, 0), int3(0, 1, 0),
										  int3(0, -1, 0), int3(0, 0, 1), int3(0, 0, -1) };

/// Each ray's radiance and signed distance, and its direction with the distance the depth moments take for it
groupshared float4 sRays[CLIPMAP_RAYS];
groupshared float4 sDirectionDepth[CLIPMAP_RAYS];
/// Per wave: the probe's stored luminance, its short term luminance, how many rays hit back faces
groupshared float3 sPartialSums[CLIPMAP_RAYS];
groupshared float4 sIrradiance[CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE];
groupshared float2 sMoments[CLIPMAP_MOMENTS_SIZE * CLIPMAP_MOMENTS_SIZE];
/// Per neighbour: its irradiance tile origin, and its weight (0 where it isn't shared with)
groupshared float4 sNeighbours[6];
/// Per corner of the next coarser level's cell around a new probe: its irradiance tile origin, and its weight
groupshared float4 sPrior[8];

float Luminance(float3 colour) { return dot(colour, float3(0.2126, 0.7152, 0.0722)); }

float3 TexelDirection(uint2 texel, uint size) { return ClipmapOctToDirection((float2(texel) + 0.5) / float(size) * 2.0 - 1.0); }

/// Mean and second moment of the ray distances around `normal`
float2 GatherMoments(float3 normal, out float out_weight)
{
	const float sharpness_log2e = Consts.vBlend.w * 1.44269504;

	float2 sum = float2(0.0, 0.0);
	float weight_sum = 0.0;

	for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
		const float4 direction_depth = sDirectionDepth[ray];

		// cos^n as exp(n (cos - 1)): the same lobe to within a few percent where it matters, for one exp2 and no log2
		const float weight = exp2(min(dot(normal, direction_depth.xyz) - 1.0, 0.0) * sharpness_log2e);
		const float distance = direction_depth.w;

		sum += float2(distance, distance * distance) * weight;
		weight_sum += weight;
	}

	out_weight = weight_sum;

	return sum / max(weight_sum, 1e-30);
}

/**
 * Blends the new mean and second moment into the stored ones, taking `rate` of the new. A texel that few of this
 * update's rays came near takes less of it, since what it gathered then belongs to other directions. The first update
 * of a probe takes all of it, whatever the coverage, as the stored moments are another probe's.
 */
float2 BlendMoments(float2 stored, float2 fresh, float fresh_weight, float rate, bool reset)
{
	// The lobe's mean over the sphere, times the ray count
	const float expected_weight = float(CLIPMAP_RAYS) / (2.0 * (Consts.vBlend.w + 1.0));
	const float coverage = saturate(fresh_weight / expected_weight);
	const float2 blended = lerp(stored, fresh, rate * coverage);

	return select(reset, fresh, select(fresh_weight > 1e-6, blended, stored));
}

[numthreads(CLIPMAP_RAYS, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint job_index = group.x + group.y * CLIPMAP_JOB_COLUMNS;

	if (job_index >= Consts.vInfo.x) {
		return;
	}

	const uint job = bJobs[job_index];
	const uint level_index = ClipmapJobLevel(job);
	const uint slot = ClipmapJobSlot(job);

	const ClipmapLevel level = Consts.Clipmap.Levels[level_index];

	const int3 probe = ClipmapSlotProbe(level, slot);
	const uint probe_index = ClipmapProbeIndex(level_index, slot);

	// A job flagged as new, or a slot whose record is another probe's, starts over
	ClipmapProbe data = ClipmapDecodeProbe(bProbes[probe_index], probe);
	const bool reset = or(ClipmapJobIsReset(job), !data.bValid);
	const uint age = select(reset, 0u, data.uiAge);
	const uint settle = select(reset, 0u, data.uiSettle);

	// Irradiance: one texel per thread, the cosine weighted mean radiance (irradiance / pi)
	const uint2 irradiance_texel = uint2(thread % CLIPMAP_IRRADIANCE_SIZE, thread / CLIPMAP_IRRADIANCE_SIZE);
	const uint2 irradiance_origin = ClipmapTileOrigin(level_index, slot, CLIPMAP_IRRADIANCE_TILE);

	const float4 own_ray = bRays[job_index * CLIPMAP_RAYS + thread];
	const float3 own_direction = ClipmapRayDirection(thread, ClipmapRayRotation(level_index, slot, Consts.vInfo.y));
	const float4 stored_irradiance = tIrradiance[irradiance_origin + irradiance_texel + 1];

	// A back face is the inside of something, so whatever lies past it can't be seen. Kept at its full distance, a
	// probe behind a wall sees the room side of that same wall as barely further off than the wall and lights it
	// through the wall. Pulled in (as DDGI does), it occludes the wall's whole far side.
	const float max_distance = level.vParams.z;
	const float own_depth = min(select(own_ray.w < 0.0, -own_ray.w * CLIPMAP_BACKFACE_DEPTH, own_ray.w), max_distance);

	sRays[thread] = own_ray;
	sDirectionDepth[thread] = float4(own_direction, own_depth);

	// The probe's stored light and its short term light over all of its texels (as many as there are threads), and
	// its back face count, summed once per wave here instead of by every thread
	const float3 wave_sums = WaveActiveSum(float3(Luminance(stored_irradiance.rgb), stored_irradiance.a,
												   select(own_ray.w < 0.0, 1.0, 0.0)));
	const uint wave_index = thread / WaveGetLaneCount();
	const uint wave_count = (CLIPMAP_RAYS + WaveGetLaneCount() - 1) / WaveGetLaneCount();

	if (WaveIsFirstLane()) {
		sPartialSums[wave_index] = wave_sums;
	}

	GroupMemoryBarrierWithGroupSync();

	float3 probe_sums = float3(0.0, 0.0, 0.0);

	for (uint wave = 0; wave < wave_count; wave++) {
		probe_sums += sPartialSums[wave];
	}

	const float long_sum = probe_sums.x;
	const float short_sum = probe_sums.y;
	const float stray_count = probe_sums.z;

	// Which of the six probes beside this one it shares light with: those that are filled and switched on, and that
	// it can see, by its depth towards them (its stored depth, or this update's for a new probe)
	if (thread < 6) {
		const int3 neighbour = probe + scNeighbourSteps[thread];
		const uint neighbour_slot = ClipmapSlot(neighbour);
		const ClipmapProbe neighbour_data = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level_index, neighbour_slot)],
															   neighbour);

		const float3 own_position = ClipmapProbePosition(level, probe) + select(reset, float3(0.0, 0.0, 0.0), data.vOffset);
		const float3 to_neighbour = ClipmapProbePosition(level, neighbour) + neighbour_data.vOffset - own_position;
		const float distance = length(to_neighbour);
		const float3 direction = to_neighbour / max(distance, 1e-4);

		float depth;

		if (reset) {
			float fresh_weight;
			depth = GatherMoments(direction, fresh_weight).x;
		}
		else {
			const uint2 texel = min(uint2((ClipmapDirectionToOct(direction) * 0.5 + 0.5) * float(CLIPMAP_MOMENTS_SIZE)),
									uint2(CLIPMAP_MOMENTS_SIZE - 1, CLIPMAP_MOMENTS_SIZE - 1));
			depth = tMoments[ClipmapTileOrigin(level_index, slot, CLIPMAP_MOMENTS_TILE) + texel + 1].x;
		}

		const bool is_shared = and(and(neighbour_data.bReady, neighbour_data.bActive),
								   depth >= distance * CLIPMAP_NEIGHBOUR_VISIBLE);
		const uint2 neighbour_origin = ClipmapTileOrigin(level_index, neighbour_slot, CLIPMAP_IRRADIANCE_TILE);

		sNeighbours[thread] = float4(float2(neighbour_origin), 0.0, select(is_shared, CLIPMAP_NEIGHBOUR_SHARE, 0.0));
	}

	// The next coarser level's cell around a new probe. A probe scrolled in at the edge of its level's window was a
	// random shade over its first updates, 64 rays each, which showed most while moving, as the finer level took over.
	if (and(thread >= 8, thread < 16)) {
		const uint corner = thread - 8;
		float weight = 0.0;
		uint2 origin = uint2(0, 0);

		if (and(reset, level_index + 1 < CLIPMAP_LEVELS)) {
			const uint coarse_index = level_index + 1;
			const ClipmapLevel coarse = Consts.Clipmap.Levels[coarse_index];
			const float3 local = ClipmapProbePosition(level, probe) * coarse.vParams.y;
			const float3 base = floor(local);
			const float3 cell_frac = local - base;
			const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
			const int3 coarse_probe = int3(base) + offset;
			const uint coarse_slot = ClipmapSlot(coarse_probe);
			const ClipmapProbe coarse_data = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(coarse_index, coarse_slot)],
																coarse_probe);
			const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));

			// Only coarse probes that see the new one, by their depth towards it, or one across a thin wall (the side
			// of a tunnel) starts it with the other side's light
			const float3 to_probe = ClipmapProbePosition(level, probe) -
									(ClipmapProbePosition(coarse, coarse_probe) + coarse_data.vOffset);
			const float distance = length(to_probe);
			const uint2 depth_texel = min(uint2((ClipmapDirectionToOct(to_probe / max(distance, 1e-4)) * 0.5 + 0.5) *
												float(CLIPMAP_MOMENTS_SIZE)),
										  uint2(CLIPMAP_MOMENTS_SIZE - 1, CLIPMAP_MOMENTS_SIZE - 1));
			const float depth = tMoments[ClipmapTileOrigin(coarse_index, coarse_slot, CLIPMAP_MOMENTS_TILE) + depth_texel + 1].x;
			const bool sees_probe = or(distance < 1e-3, depth >= distance * CLIPMAP_NEIGHBOUR_VISIBLE);

			weight = select(and(and(coarse_data.bReady, coarse_data.bActive), sees_probe), axis.x * axis.y * axis.z, 0.0);
			origin = ClipmapTileOrigin(coarse_index, coarse_slot, CLIPMAP_IRRADIANCE_TILE);
		}

		sPrior[corner] = float4(float2(origin), 0.0, weight);
	}

	GroupMemoryBarrierWithGroupSync();

	const float3 normal = TexelDirection(irradiance_texel, CLIPMAP_IRRADIANCE_SIZE);

	float3 radiance_sum = float3(0.0, 0.0, 0.0);
	float weight_sum = 0.0;
	float front_weight_sum = 0.0;

	for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
		const float4 ray_data = sRays[ray];
		const float weight = saturate(dot(normal, sDirectionDepth[ray].xyz));

		radiance_sum += ray_data.rgb * weight;
		weight_sum += weight;
		front_weight_sum += select(ray_data.w >= 0.0, weight, 0.0);
	}

	// Back face rays carry no light. A probe that is on (not in a wall) mostly sees them through cracks and past the
	// edges of one sided surfaces, where they would only darken it, so they are left out of the mean.
	const bool is_stray = stray_count < CLIPMAP_STRAY_BACKFACES * float(CLIPMAP_RAYS);
	const float lit_weight = select(is_stray, max(front_weight_sum, weight_sum / CLIPMAP_BACKFACE_BRIGHTEN), weight_sum);
	const float3 own_fresh = radiance_sum / max(lit_weight, 1e-6);

	// The neighbours' stored light along this texel's direction is mixed into what this update traced. 64 rays are too
	// few where a small, bright patch (a sunlit stripe) gives most of the light: each probe's average kept its own
	// share of how many rays happened to hit the patch, and every probe in front of a flat wall had its own shade.
	// Repeated every update, this settles into a smoothing about a probe spacing wide.
	float3 neighbour_sum = float3(0.0, 0.0, 0.0);
	float neighbour_weight = 0.0;

	[unroll]
	for (uint side = 0; side < 6; side++) {
		const float4 neighbour = sNeighbours[side];

		neighbour_sum += tIrradiance[uint2(neighbour.xy) + irradiance_texel + 1].rgb * neighbour.w;
		neighbour_weight += neighbour.w;
	}

	const float3 fresh = (own_fresh + neighbour_sum) / (1.0 + neighbour_weight);

	// All levels share the octahedral layout, so the same texel of a coarser probe is the same direction
	float3 prior_sum = float3(0.0, 0.0, 0.0);
	float prior_weight = 0.0;

	[unroll]
	for (uint corner = 0; corner < 8; corner++) {
		const float4 coarse = sPrior[corner];

		prior_sum += tIrradiance[uint2(coarse.xy) + irradiance_texel + 1].rgb * coarse.w;
		prior_weight += coarse.w;
	}

	const bool has_prior = and(reset, prior_weight >= CLIPMAP_PRIOR_MIN_WEIGHT);
	const float3 prior = prior_sum / max(prior_weight, 1e-6);
	const float3 previous = select(has_prior, prior, stored_irradiance.rgb);

	// A new probe with a prior starts as though it had already averaged that many updates
	const uint blend_age = select(reset, select(has_prior, uint(CLIPMAP_PRIOR_UPDATES), 0u), data.uiAge);

	// Levels that are traced less often take more of each update, so that all of them follow the light equally fast
	const float level_rate = Consts.vLevelRate[level_index];
	const float slow_hysteresis = pow(Consts.vBlend.x, level_rate);
	const float fast_hysteresis = pow(Consts.vBlend.y, level_rate);

	// A probe that is still settling has its own short history to go on, not a change in its light
	const bool is_settled = settle >= CLIPMAP_SETTLE_UPDATES;
	const float disagreement = abs(short_sum - long_sum) / max(max(short_sum, long_sum), 1e-4);
	const float adaptive_hysteresis =
		select(is_settled,
			   lerp(slow_hysteresis, fast_hysteresis, smoothstep(Consts.vBlend.z, Consts.vBlend.z * 2.0, disagreement)),
			   slow_hysteresis);

	// A probe whose light is known to have changed blends fastest, then eases back over its dirty updates, so it
	// does not stop on the noise of its last fast one
	const float dirty = float(ClipmapJobDirty(job)) / float(CLIPMAP_DIRTY_UPDATES);
	const float dirty_hysteresis = lerp(slow_hysteresis, Consts.vLevelRate.w, dirty);

	// Over its first updates a probe keeps the plain mean of all it has traced, so a new probe is as good as its
	// rays allow from the start instead of being one update's noise faded out over dozens
	const float age_hysteresis = float(blend_age) / float(blend_age + 1);
	const float hysteresis = min(min(adaptive_hysteresis, dirty_hysteresis), age_hysteresis);

	const float3 blended = lerp(fresh, previous, hysteresis);
	const float short_start = select(has_prior, Luminance(prior), stored_irradiance.a);
	const float short_term = select(and(reset, !has_prior), Luminance(fresh),
									lerp(short_start, Luminance(fresh), CLIPMAP_SHORT_BLEND));

	sIrradiance[thread] = float4(blended, short_term);

	// Depth moments, which settle the same way. They are most of the blend's work, and once a probe has settled they
	// only change with the geometry, so they are refreshed on every other update, taking twice as much each time.
	const uint2 moments_origin = ClipmapTileOrigin(level_index, slot, CLIPMAP_MOMENTS_TILE);
	const bool refresh_moments = or(!is_settled, ((age + slot) & 1u) == 0u);
	// From the probe's own updates, which a prior for its light doesn't count towards
	const float moments_rate = max(saturate((1.0 - slow_hysteresis) * 2.0), 1.0 / float(min(age, settle) + 1));

	if (refresh_moments) {
		for (uint index = thread; index < CLIPMAP_MOMENTS_SIZE * CLIPMAP_MOMENTS_SIZE; index += CLIPMAP_RAYS) {
			const uint2 texel = uint2(index % CLIPMAP_MOMENTS_SIZE, index / CLIPMAP_MOMENTS_SIZE);

			float fresh_weight;
			const float2 fresh_moments = GatherMoments(TexelDirection(texel, CLIPMAP_MOMENTS_SIZE), fresh_weight);
			const float2 stored = tMoments[moments_origin + texel + 1];

			sMoments[index] = BlendMoments(stored, fresh_moments, fresh_weight, moments_rate, reset);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	// Interior and border texels, the border copying across the octahedral seams
	for (uint index = thread; index < CLIPMAP_IRRADIANCE_TILE * CLIPMAP_IRRADIANCE_TILE; index += CLIPMAP_RAYS) {
		const int2 texel = int2(index % CLIPMAP_IRRADIANCE_TILE, index / CLIPMAP_IRRADIANCE_TILE);
		const uint2 source = ClipmapBorderSource(texel, CLIPMAP_IRRADIANCE_SIZE);

		tIrradiance[irradiance_origin + uint2(texel)] = sIrradiance[source.x + source.y * CLIPMAP_IRRADIANCE_SIZE];
	}

	for (uint index = select(refresh_moments, thread, CLIPMAP_MOMENTS_TILE * CLIPMAP_MOMENTS_TILE);
		 index < CLIPMAP_MOMENTS_TILE * CLIPMAP_MOMENTS_TILE; index += CLIPMAP_RAYS) {
		const int2 texel = int2(index % CLIPMAP_MOMENTS_TILE, index / CLIPMAP_MOMENTS_TILE);
		const uint2 source = ClipmapBorderSource(texel, CLIPMAP_MOMENTS_SIZE);

		tMoments[moments_origin + uint2(texel)] = sMoments[source.x + source.y * CLIPMAP_MOMENTS_SIZE];
	}

	if (thread == 0) {
		// Relocation and classification from the rays, after DDGI (Majercik et al. 2021). A probe is only moved out of
		// something it is inside, which the move itself takes away, so the per update noise of the rays can't nudge it
		// around.
		const float spacing = level.vParams.x;
		const bool relocate = HAS_FLAG(Consts.vInfo.z, CLIPMAP_FLAG_RELOCATE);

		// Where the trace started from, see ProbeClipmapTrace
		const float3 previous_offset = select(reset, float3(0.0, 0.0, 0.0), data.vOffset);

		const float min_front = min(CLIPMAP_MIN_FRONTFACE * spacing, CLIPMAP_MIN_FRONTFACE_METRES);
		const float touching = min_front * 0.25;

		uint backface_count = 0;
		uint touching_count = 0;
		float closest_back = 1e30;
		float3 closest_back_direction = float3(0.0, 0.0, 0.0);

		for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
			const float signed_distance = sRays[ray].w;
			const float distance = abs(signed_distance);
			const float3 direction = sDirectionDepth[ray].xyz;
			const bool is_touching = distance < touching;

			touching_count += select(is_touching, 1u, 0u);

			if (signed_distance < 0.0) {
				backface_count++;

				if (distance < closest_back) {
					closest_back = distance;
					closest_back_direction = direction;
				}
			}
		}

		// On a surface, which a grid aligned map puts many probes on: every ray stops where it starts, half of them on
		// the inside of the wall. Such a probe is no use to anything and is switched off, and the surface takes its
		// light from the probes half a cell in front of it. Moving it off the surface instead parked it right in front
		// of the wall, and in a corner in front of the other wall, where it took the light of a whole cone of it.
		const bool is_on_surface = touching_count > CLIPMAP_RAYS / 4;
		const float backface_ratio = select(is_on_surface, 1.0, float(backface_count) / float(CLIPMAP_RAYS));

		float3 offset = previous_offset;

		if (and(!is_on_surface, backface_ratio > CLIPMAP_RELOCATE_BACKFACES)) {
			// Inside a wall: through the nearest back face and a little past it
			offset += closest_back_direction * (closest_back + min_front * 0.5);
		}

		// A probe that is merely near a surface stays where it is too. Pushing it off the nearest front face (DDGI's
		// other rule) parked it in front of walls the same way.

		// A move out of a wall that doesn't fit is dropped, as the probe would stay in the wall
		const float max_offset = CLIPMAP_MAX_OFFSET * spacing;
		offset = select(all(abs(offset) <= max_offset), offset, previous_offset);

		// Not limited to the settling updates: a probe traced before the SDF pages around it were filled saw nothing
		// to move away from, and would otherwise stay in its wall for good
		const float3 new_offset = select(relocate, offset, float3(0.0, 0.0, 0.0));

		// What it has averaged so far was traced from where it was, so a probe that moved far starts over
		const bool moved_far = any(abs(new_offset - previous_offset) > CLIPMAP_RESET_MOVE * spacing);

		// Its share of back faces is the plain mean over its first updates, then a slow one, so it follows the SDF
		// pages filling in and the probe moving. Switching off and on again has a margin, so noise can't toggle it.
		const float backface_rate = max(1.0 / float(settle + 1), CLIPMAP_BACKFACE_RATE);
		const float backfaces = select(moved_far, backface_ratio, lerp(data.fBackfaces, backface_ratio, backface_rate));
		const bool was_active = or(reset, data.bActive);
		const float switch_at = select(was_active, CLIPMAP_INACTIVE_BACKFACES, CLIPMAP_ACTIVE_BACKFACES);
		const bool is_active = or(!relocate, backfaces <= switch_at);

		ClipmapProbe written;
		written.bValid = true;
		written.vOffset = new_offset;
		written.fBackfaces = backfaces;
		written.bActive = is_active;
		written.uiAge = select(moved_far, 0u, min(blend_age + 1, CLIPMAP_PROBE_MAX_AGE));
		written.uiSettle = min(settle + 1, CLIPMAP_PROBE_MAX_AGE);

		bProbes[probe_index] = ClipmapEncodeProbe(probe, written);
	}
}
