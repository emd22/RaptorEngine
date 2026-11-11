F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeClipmapConsts.hlsli"

F_StructBuffer(bJobs, uint, 0, 0);
F_StructBuffer(bRays, float4, 1, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(2, 0)]] RWTexture2D<float4> tIrradiance0;
[[vk::image_format("rgba16f")]] [[vk::binding(3, 0)]] RWTexture2D<float4> tIrradiance1;
[[vk::image_format("rgba16f")]] [[vk::binding(4, 0)]] RWTexture2D<float4> tIrradiance2;
[[vk::image_format("rg16f")]] [[vk::binding(5, 0)]] RWTexture2D<float2> tMoments0;
[[vk::image_format("rg16f")]] [[vk::binding(6, 0)]] RWTexture2D<float2> tMoments1;
[[vk::image_format("rg16f")]] [[vk::binding(7, 0)]] RWTexture2D<float2> tMoments2;

F_RWStructBuffer(bStats, uint, 8, 0);
F_RWStructBuffer(bProbeData, float4, 9, 0);

F_FrameData(ProbeClipmapConsts, Consts, 1);

/// Mirrors ProbeClipmap::scStat*
#define STAT_BLENDED 0
#define STAT_CHANGE 1
#define STAT_RESET 2
#define STAT_BLACK 3
#define STAT_BACKFACES 4
#define STAT_INACTIVE 5
#define STAT_MOVED 6
#define STAT_LIGHT 7

/// Fixed point scale of the summed probe light, and the most one probe adds, so the sum fits
#define STAT_LIGHT_SCALE 4.0
#define STAT_LIGHT_MAX 65536.0

/// Fixed point scale of the summed relative change
#define STAT_CHANGE_SCALE 10000.0

/// How much of each update goes into a texel's short term luminance, kept in the irradiance alpha. The blend speeds
/// up where it and the irradiance disagree, which is decided before this update's rays are looked at: a decision made
/// from the rays themselves blends bright outliers in faster than dark ones and brightens the probe over time.
#define CLIPMAP_SHORT_BLEND 0.25

groupshared float4 sRays[CLIPMAP_RAYS];
groupshared float3 sDirections[CLIPMAP_RAYS];
groupshared float4 sStored[CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE];
groupshared float4 sIrradiance[CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE];
groupshared float2 sMoments[CLIPMAP_MOMENTS_SIZE_0 * CLIPMAP_MOMENTS_SIZE_0];
groupshared uint sChange;
groupshared uint sPreviousSum;

float Luminance(float3 colour) { return dot(colour, float3(0.2126, 0.7152, 0.0722)); }

// The level is the same for the whole group, so these branches do not diverge

float4 LoadIrradiance(uint level, uint2 address)
{
	if (level == 0) {
		return tIrradiance0[address];
	}

	if (level == 1) {
		return tIrradiance1[address];
	}

	return tIrradiance2[address];
}

void StoreIrradiance(uint level, uint2 address, float4 value)
{
	if (level == 0) {
		tIrradiance0[address] = value;
	}
	else if (level == 1) {
		tIrradiance1[address] = value;
	}
	else {
		tIrradiance2[address] = value;
	}
}

float2 LoadMoments(uint level, uint2 address)
{
	if (level == 0) {
		return tMoments0[address];
	}

	if (level == 1) {
		return tMoments1[address];
	}

	return tMoments2[address];
}

void StoreMoments(uint level, uint2 address, float2 value)
{
	if (level == 0) {
		tMoments0[address] = value;
	}
	else if (level == 1) {
		tMoments1[address] = value;
	}
	else {
		tMoments2[address] = value;
	}
}

float3 TexelDirection(uint2 texel, uint size) { return ClipmapOctToDirection((float2(texel) + 0.5) / float(size) * 2.0 - 1.0); }

/// Mean and second moment of the ray distances around `normal`
float2 GatherMoments(float3 normal, float max_distance, out float out_weight)
{
	float2 sum = float2(0.0, 0.0);
	float weight_sum = 0.0;

	for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
		const float weight = pow(saturate(dot(normal, sDirections[ray])), Consts.vBlend.w);
		const float distance = min(abs(sRays[ray].w), max_distance);

		sum += float2(distance, distance * distance) * weight;
		weight_sum += weight;
	}

	out_weight = weight_sum;

	return sum / max(weight_sum, 1e-6);
}

/// Blends the new mean and second moment into the stored ones. A texel that few of this update's rays came near
/// takes less of it, since what it gathered then belongs to other directions.
float2 BlendMoments(float2 stored, float2 fresh, float fresh_weight, float hysteresis, bool reset)
{
	// The lobe's mean over the sphere, times the ray count
	const float expected_weight = float(CLIPMAP_RAYS) / (2.0 * (Consts.vBlend.w + 1.0));
	const float coverage = saturate(fresh_weight / expected_weight);
	const float2 blended = select(reset, fresh, lerp(stored, fresh, (1.0 - hysteresis) * coverage));

	return select(fresh_weight > 1e-6, blended, stored);
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
	const uint probe_index = ClipmapProbeDataIndex(level_index, probe);
	const float4 probe_data = bProbeData[probe_index];
	const uint probe_flags = asuint(probe_data.w);

	// New in its slot, or moved far since its tiles were traced
	const bool reset = or(ClipmapJobIsReset(job), HAS_FLAG(probe_flags, CLIPMAP_PROBE_RESET));

	// Irradiance: one texel per thread, the cosine weighted mean radiance (irradiance / pi)
	const uint2 irradiance_texel = uint2(thread % CLIPMAP_IRRADIANCE_SIZE, thread / CLIPMAP_IRRADIANCE_SIZE);
	const uint2 irradiance_origin = ClipmapTileOrigin(slot, CLIPMAP_IRRADIANCE_SIZE + 2);

	sRays[thread] = bRays[job_index * CLIPMAP_RAYS + thread];
	sDirections[thread] = ClipmapRayDirection(thread, Consts.mRayRotation);
	sStored[thread] = LoadIrradiance(level_index, irradiance_origin + irradiance_texel + 1);

	if (thread == 0) {
		sChange = 0;
		sPreviousSum = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	const float3 normal = TexelDirection(irradiance_texel, CLIPMAP_IRRADIANCE_SIZE);

	float3 radiance_sum = float3(0.0, 0.0, 0.0);
	float weight_sum = 0.0;

	// The probe's stored light and its short term light, over all of its texels (as many as there are rays)
	float long_sum = 0.0;
	float short_sum = 0.0;

	for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
		const float weight = saturate(dot(normal, sDirections[ray]));

		radiance_sum += sRays[ray].rgb * weight;
		weight_sum += weight;

		long_sum += Luminance(sStored[ray].rgb);
		short_sum += sStored[ray].a;
	}

	const float3 fresh = radiance_sum / max(weight_sum, 1e-6);
	const float3 previous = sStored[thread].rgb;

	// Levels that are traced less often take more of each update, so that all of them follow the light equally fast
	const float level_rate = Consts.vLevelRate[level_index];
	const float slow_hysteresis = pow(Consts.vBlend.x, level_rate);
	const float fast_hysteresis = pow(Consts.vBlend.y, level_rate);

	const float disagreement = abs(short_sum - long_sum) / max(max(short_sum, long_sum), 1e-4);
	const float adaptive_hysteresis = lerp(slow_hysteresis, fast_hysteresis,
										   smoothstep(Consts.vBlend.z, Consts.vBlend.z * 2.0, disagreement));

	// A probe whose light is known to have changed blends fastest, then eases back over its dirty updates, so it
	// does not stop on the noise of its last fast one
	const float dirty = float(ClipmapJobDirty(job)) / float(CLIPMAP_DIRTY_UPDATES);
	const float hysteresis = min(adaptive_hysteresis, lerp(slow_hysteresis, Consts.vLevelRate.w, dirty));
	const float3 blended = select(reset, fresh, lerp(fresh, previous, hysteresis));
	const float short_term = select(reset, Luminance(fresh), lerp(sStored[thread].a, Luminance(fresh), CLIPMAP_SHORT_BLEND));

	sIrradiance[thread] = float4(blended, short_term);

	// Relative change of the probe's summed luminance, a flicker measure for a still camera
	const uint change_fixed = uint(min(abs(Luminance(blended) - Luminance(previous)), 1e6) * 16.0);
	const uint previous_fixed = uint(min(Luminance(previous), 1e6) * 16.0);
	InterlockedAdd(sChange, change_fixed);
	InterlockedAdd(sPreviousSum, previous_fixed);

	// Depth moments
	const bool is_fine = level_index == 0;
	const uint moments_size = select(is_fine, CLIPMAP_MOMENTS_SIZE_0, CLIPMAP_MOMENTS_SIZE);
	const uint moments_tile = moments_size + 2;
	const uint2 moments_origin = ClipmapTileOrigin(slot, moments_tile);
	const uint moments_count = moments_size * moments_size;
	const float max_distance = level.vParams.z;

	for (uint index = thread; index < moments_count; index += CLIPMAP_RAYS) {
		const uint2 texel = uint2(index % moments_size, index / moments_size);
		const uint2 address = moments_origin + texel + 1;

		float fresh_weight;
		const float2 fresh_moments = GatherMoments(TexelDirection(texel, moments_size), max_distance, fresh_weight);

		const float2 stored = LoadMoments(level_index, address);

		sMoments[index] = BlendMoments(stored, fresh_moments, fresh_weight, slow_hysteresis, reset);
	}

	GroupMemoryBarrierWithGroupSync();

	// Interior and border texels, the border copying across the octahedral seams
	const uint irradiance_tile = CLIPMAP_IRRADIANCE_SIZE + 2;

	for (uint index = thread; index < irradiance_tile * irradiance_tile; index += CLIPMAP_RAYS) {
		const int2 texel = int2(index % irradiance_tile, index / irradiance_tile);
		const uint2 source = ClipmapBorderSource(texel, CLIPMAP_IRRADIANCE_SIZE);

		StoreIrradiance(level_index, irradiance_origin + uint2(texel),
						sIrradiance[source.x + source.y * CLIPMAP_IRRADIANCE_SIZE]);
	}

	for (uint index = thread; index < moments_tile * moments_tile; index += CLIPMAP_RAYS) {
		const int2 texel = int2(index % moments_tile, index / moments_tile);
		const uint2 source = ClipmapBorderSource(texel, int(moments_size));
		const float2 value = sMoments[source.x + source.y * moments_size];
		StoreMoments(level_index, moments_origin + uint2(texel), value);
	}

	if (thread == 0) {
		// Relocation and classification from this update's rays, after DDGI (Majercik et al. 2021)
		const float spacing = level.vParams.x;
		const bool relocate = HAS_FLAG(Consts.vInfo.z, CLIPMAP_FLAG_RELOCATE);

		// Where the trace started from, see ProbeClipmapTrace
		const float3 stored = select(relocate, ClipmapStoredOffset(probe_data, probe), float3(0.0, 0.0, 0.0));
		const float3 previous_offset = select(ClipmapJobIsReset(job), stored, probe_data.xyz);

		uint backface_count = 0;
		float closest_back = 1e30;
		float3 closest_back_direction = float3(0.0, 0.0, 0.0);
		float closest_front = 1e30;
		float3 closest_front_direction = float3(0.0, 0.0, 0.0);
		float farthest_front = 0.0;
		float3 farthest_front_direction = float3(0.0, 0.0, 0.0);

		for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
			const float signed_distance = sRays[ray].w;
			const float distance = abs(signed_distance);
			const float3 direction = sDirections[ray];

			if (signed_distance < 0.0) {
				backface_count++;

				if (distance < closest_back) {
					closest_back = distance;
					closest_back_direction = direction;
				}
			}
			else {
				if (distance < closest_front) {
					closest_front = distance;
					closest_front_direction = direction;
				}

				if (distance > farthest_front) {
					farthest_front = distance;
					farthest_front_direction = direction;
				}
			}
		}

		const float min_front = CLIPMAP_MIN_FRONTFACE * spacing;
		const float backface_ratio = float(backface_count) / float(CLIPMAP_RAYS);

		float3 offset = previous_offset;

		if (backface_ratio > CLIPMAP_RELOCATE_BACKFACES) {
			// Inside a wall: through the nearest back face and a little past it
			offset += closest_back_direction * (closest_back + min_front * 0.5);
		}
		else if (closest_front < min_front) {
			// Too close to a surface: towards the open space, unless that is where the surface is
			const bool is_open = dot(farthest_front_direction, closest_front_direction) <= 0.5;
			offset += select(is_open, farthest_front_direction * min(farthest_front, spacing * 0.5), float3(0.0, 0.0, 0.0));
		}

		const bool fits = all(abs(offset) <= CLIPMAP_MAX_OFFSET * spacing);
		offset = select(relocate, select(fits, offset, previous_offset), float3(0.0, 0.0, 0.0));

		const bool is_active = or(!relocate, backface_ratio <= CLIPMAP_INACTIVE_BACKFACES);
		const bool moved_far = any(abs(offset - previous_offset) > CLIPMAP_RESET_MOVE * spacing);

		uint flags = ClipmapProbeDataKey(probe);
		flags |= select(is_active, CLIPMAP_PROBE_ACTIVE, 0u);
		flags |= select(moved_far, CLIPMAP_PROBE_RESET, 0u);

		bProbeData[probe_index] = float4(offset, asfloat(flags));

		InterlockedAdd(bStats[STAT_INACTIVE], select(is_active, 0u, 1u));
		InterlockedAdd(bStats[STAT_MOVED], select(any(offset != 0.0), 1u, 0u));

		uint black = 1;

		for (uint texel = 0; texel < CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE; texel++) {
			black = select(any(sIrradiance[texel].rgb > 0.0), 0u, black);
		}

		uint backfaces = 0;

		for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
			backfaces += select(sRays[ray].w < 0.0, 1u, 0u);
		}

		const float relative = float(sChange) / max(float(sPreviousSum), 1.0);

		InterlockedAdd(bStats[STAT_BLENDED], select(reset, 0u, 1u));
		InterlockedAdd(bStats[STAT_CHANGE], select(reset, 0u, uint(min(relative, 10.0) * STAT_CHANGE_SCALE)));
		InterlockedAdd(bStats[STAT_RESET], select(reset, 1u, 0u));
		InterlockedAdd(bStats[STAT_BLACK], black);
		InterlockedAdd(bStats[STAT_BACKFACES], backfaces);

		float light = 0.0;

		for (uint texel = 0; texel < CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE; texel++) {
			light += Luminance(sIrradiance[texel].rgb);
		}

		light /= float(CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE);
		InterlockedAdd(bStats[STAT_LIGHT], uint(min(light, STAT_LIGHT_MAX) * STAT_LIGHT_SCALE));
	}
}
