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

F_FrameData(ProbeClipmapConsts, Consts, 1);

/// Mirrors ProbeClipmap::scStat*
#define STAT_BLENDED 0
#define STAT_CHANGE 1
#define STAT_RESET 2
#define STAT_BLACK 3
#define STAT_BACKFACES 4

/// Fixed point scale of the summed relative change
#define STAT_CHANGE_SCALE 10000.0

#define MOMENTS_PER_THREAD ((CLIPMAP_MOMENTS_SIZE_0 * CLIPMAP_MOMENTS_SIZE_0) / CLIPMAP_RAYS)

groupshared float4 sRays[CLIPMAP_RAYS];
groupshared float3 sDirections[CLIPMAP_RAYS];
groupshared float3 sIrradiance[CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE];
groupshared float2 sMoments[CLIPMAP_MOMENTS_SIZE_0 * CLIPMAP_MOMENTS_SIZE_0];
groupshared uint sChange;
groupshared uint sPrevious;

float Luminance(float3 colour) { return dot(colour, float3(0.2126, 0.7152, 0.0722)); }

// The level is the same for the whole group, so these branches do not diverge

float3 LoadIrradiance(uint level, uint2 address)
{
	if (level == 0) {
		return tIrradiance0[address].rgb;
	}

	if (level == 1) {
		return tIrradiance1[address].rgb;
	}

	return tIrradiance2[address].rgb;
}

void StoreIrradiance(uint level, uint2 address, float3 value)
{
	if (level == 0) {
		tIrradiance0[address] = float4(value, 1.0);
	}
	else if (level == 1) {
		tIrradiance1[address] = float4(value, 1.0);
	}
	else {
		tIrradiance2[address] = float4(value, 1.0);
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

/// Blends the new mean and second moment into the stored mean and standard deviation
float2 BlendMoments(float2 stored, float2 fresh, float fresh_weight, float hysteresis, bool reset)
{
	const float2 previous = float2(stored.x, stored.y * stored.y + stored.x * stored.x);
	const float2 blended = select(reset, fresh, lerp(fresh, previous, hysteresis));
	const float2 kept = select(fresh_weight > 1e-6, blended, previous);

	return float2(kept.x, sqrt(max(kept.y - kept.x * kept.x, 0.0)));
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
	const bool reset = ClipmapJobIsReset(job);
	const ClipmapLevel level = Consts.Clipmap.Levels[level_index];

	sRays[thread] = bRays[job_index * CLIPMAP_RAYS + thread];
	sDirections[thread] = ClipmapRayDirection(thread, Consts.mRayRotation);

	if (thread == 0) {
		sChange = 0;
		sPrevious = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// Irradiance: one texel per thread, the cosine weighted mean radiance (irradiance / pi)
	const uint2 irradiance_texel = uint2(thread % CLIPMAP_IRRADIANCE_SIZE, thread / CLIPMAP_IRRADIANCE_SIZE);
	const float3 normal = TexelDirection(irradiance_texel, CLIPMAP_IRRADIANCE_SIZE);

	float3 radiance_sum = float3(0.0, 0.0, 0.0);
	float weight_sum = 0.0;

	for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
		const float weight = saturate(dot(normal, sDirections[ray]));

		radiance_sum += sRays[ray].rgb * weight;
		weight_sum += weight;
	}

	const float3 fresh = radiance_sum / max(weight_sum, 1e-6);

	const uint2 irradiance_origin = ClipmapTileOrigin(slot, CLIPMAP_IRRADIANCE_SIZE + 2);
	const float3 previous = LoadIrradiance(level_index, irradiance_origin + irradiance_texel + 1);

	const float change = abs(Luminance(fresh) - Luminance(previous)) / max(Luminance(previous), 1e-4);
	const float hysteresis = select(change > Consts.vBlend.z, Consts.vBlend.y, Consts.vBlend.x);
	const float3 blended = select(reset, fresh, lerp(fresh, previous, hysteresis));

	sIrradiance[thread] = blended;

	// Relative change of the probe's summed luminance, a flicker measure for a still camera
	const uint change_fixed = uint(min(abs(Luminance(blended) - Luminance(previous)), 1e6) * 16.0);
	const uint previous_fixed = uint(min(Luminance(previous), 1e6) * 16.0);
	InterlockedAdd(sChange, change_fixed);
	InterlockedAdd(sPrevious, previous_fixed);

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

		sMoments[index] = BlendMoments(stored, fresh_moments, fresh_weight, Consts.vBlend.x, reset);
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
		uint black = 1;

		for (uint texel = 0; texel < CLIPMAP_IRRADIANCE_SIZE * CLIPMAP_IRRADIANCE_SIZE; texel++) {
			black = select(any(sIrradiance[texel] > 0.0), 0u, black);
		}

		uint backfaces = 0;

		for (uint ray = 0; ray < CLIPMAP_RAYS; ray++) {
			backfaces += select(sRays[ray].w < 0.0, 1u, 0u);
		}

		const float relative = float(sChange) / max(float(sPrevious), 1.0);

		InterlockedAdd(bStats[STAT_BLENDED], select(reset, 0u, 1u));
		InterlockedAdd(bStats[STAT_CHANGE], select(reset, 0u, uint(min(relative, 10.0) * STAT_CHANGE_SCALE)));
		InterlockedAdd(bStats[STAT_RESET], select(reset, 1u, 0u));
		InterlockedAdd(bStats[STAT_BLACK], black);
		InterlockedAdd(bStats[STAT_BACKFACES], backfaces);
	}
}
