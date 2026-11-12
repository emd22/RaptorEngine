F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "SDFPageCommon.hlsli"
#include "LightingCommon.hlsli"
#include "FogConsts.hlsli"
#include "ProbeClipmapCommon.hlsli"
#include "ProbeClipmapConsts.hlsli"
#include "ScreenProbeCommon.hlsli"

// Screen probes: one probe on the surface of each tile of the screen, traced against the SDF pages for a short way, and
// read from the probe clipmap where a ray gets that far without a hit. Writes each probe's radiance as L1 spherical
// harmonics, which the GI resolve (ProbeClipmapResolve.hlsl) gathers per pixel and reprojects.

F_StructBuffer(bSDFPageIndirection, uint, 0, 0);
F_Texture3D(tSDFPageAtlas, 1, 0);
F_StructBuffer(bHitRecords, SDFPageHitRecord, 2, 0);
F_StructBuffer(bRefs, uint, 3, 0);
F_StructBuffer(bAtlasFirstRef, uint, 4, 0);
Texture3D<uint> tNearestTexture : register(t5, space0);
SamplerState tNearest : register(s5, space0);

F_CBuffer(FSLightBuffer, 6, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_ShadowTexture2D(tShadowAtlas, 7, 0);
F_ShadowTexture2D(tFogShadowAtlas, 8, 0);
F_StructBuffer(bFogShadowPages, FogShadowPage, 9, 0);

F_Texture2D(tIrradiance, 12, 0);
F_Texture2D(tMoments, 15, 0);
F_StructBuffer(bProbes, uint4, 19, 0);

F_Texture2D(tDepth, 20, 0);
F_Texture2D(tNormal, 21, 0);
F_RWStructBuffer(bScreenProbes, float4, 22, 0);

struct ScreenProbeTraceConsts
{
	ProbeClipmapConsts Base;
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Width, height, tile size, tiles across
	uint4 vScreen;
	/// Ray length through the SDF pages before the clipmap is read, cone slope, frame
	float4 vParams;
};

F_FrameData(ScreenProbeTraceConsts, Consts, 1);

#define CLIPMAP_SCENE Consts.Base.Clipmap
#include "ProbeClipmapSample.hlsli"
#include "ProbeClipmapResolve.hlsli"

#define SDF_PAGE_SCENE Consts.Base.SDFPages
#include "SDFPageTrace.hlsli"
#include "SDFPageShade.hlsli"

#define VIEWMODEL_STORED_DEPTH 0.99

groupshared float4 sRadiance[SCREEN_PROBE_RAYS];
/// The probe: position (W 1 where placed), normal, where its rays start, and the tangent frame its directions are in
groupshared float4 sPlacement[5];

float3 ScreenWorldPosition(int2 pixel)
{
	const int2 clamped = clamp(pixel, int2(0, 0), int2(Consts.vScreen.xy) - 1);
	const float stored_depth = F_SampleLoad(tDepth, int3(clamped, 0)).r;
	const float2 uv = (float2(clamped) + 0.5) / float2(Consts.vScreen.xy);
	const float4 world = mul(float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0), Consts.mInvViewProjection);

	return world.xyz / world.w;
}

bool ScreenHasSurface(int2 pixel)
{
	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	return and(stored_depth > 0.0, stored_depth < VIEWMODEL_STORED_DEPTH);
}

/**
 * Places the tile's probe, on the first wave: its lanes test the tile's centre and four points around it at once, then
 * fetch the chosen pixel and its eight neighbours for the geometric normal at once, where one lane used to do it all
 * while the rest waited. Lane 0 leaves the probe in sPlacement.
 */
void PlaceScreenProbe(uint lane, int2 tile_origin, uint tile_size)
{
	const int2 last_pixel = int2(Consts.vScreen.xy) - 1;
	const int2 centre_pixel = tile_origin + int2(tile_size / 2, tile_size / 2);

	// Lane 0 the centre, lanes 1-4 the corners of the tile's middle half
	const int quarter = int(tile_size / 4);
	const uint corner = lane - 1u;
	const int2 corner_offset = int2(select((corner & 1u) != 0u, quarter, -quarter),
									select((corner & 2u) != 0u, quarter, -quarter));
	const int2 candidate = min(centre_pixel + select(lane == 0u, int2(0, 0), corner_offset), last_pixel);

	const uint4 surface_ballot = WaveActiveBallot(and(lane < 5u, ScreenHasSurface(candidate)));
	const bool has_surface = surface_ballot.x != 0u;
	const int2 chosen = WaveReadLaneAt(candidate, select(has_surface, firstbitlow(surface_ballot.x), 0u));

	// Lane 0 the chosen pixel, lanes 1-4 one and two pixels left then right of it, lanes 5-8 above then below,
	// as GI_RESOLVE_GEOMETRIC_NORMAL takes them
	const uint neighbour = lane - 1u;
	const int step = int(1u + (neighbour & 1u)) * select((neighbour & 2u) != 0u, 1, -1);
	const int2 neighbour_offset = select((neighbour >> 2) == 0u, int2(step, 0), int2(0, step));
	const int2 fetch_pixel = chosen + select(and(lane >= 1u, lane <= 8u), neighbour_offset, int2(0, 0));
	const float3 fetched = ScreenWorldPosition(fetch_pixel);

	const float3 centre = WaveReadLaneAt(fetched, 0u);
	const float3 eye = Consts.vEyePosition.xyz;
	const float eye_distance = length(eye - centre);
	const float3 view_direction = (eye - centre) / max(eye_distance, 1e-6);
	const float3 prepass_normal = normalize(F_SampleLoad(tNormal, int3(chosen, 0)).xyz);

	const float3 geometric_normal = GIResolveGeometricNormal(
		centre, WaveReadLaneAt(fetched, 1u), WaveReadLaneAt(fetched, 2u), WaveReadLaneAt(fetched, 3u),
		WaveReadLaneAt(fetched, 4u), WaveReadLaneAt(fetched, 5u), WaveReadLaneAt(fetched, 6u),
		WaveReadLaneAt(fetched, 7u), WaveReadLaneAt(fetched, 8u), view_direction, eye_distance * 0.05, prepass_normal);

	const bool is_placed = and(has_surface, and(GIIsFinite(centre), GIIsFinite(geometric_normal)));
	const float3 normal = select(is_placed, geometric_normal, float3(0.0, 1.0, 0.0));

	// Off the surface, and as far towards the eye (so not into the other wall of a crease), by twice the texel of the
	// SDF page level there. The field is unsigned and a ray stops within 0.75 texel of any surface, so a ray starting
	// closer than that "hits" its own surface at once, from behind, and brings back black: past 8 m from the camera
	// the pages are 6 cm, past 16 m 12.5 cm, and a fixed 6 cm offset blacked out everything far away.
	const float bias = max(SCREEN_PROBE_SURFACE_BIAS, SDFPageTexelAt(centre) * SCREEN_PROBE_BIAS_TEXELS);
	const float3 origin = centre + (normal + view_direction) * bias;

	const float3 up = select(abs(normal.y) < 0.999, float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0));
	const float3 tangent = normalize(cross(up, normal));
	const float3 bitangent = cross(normal, tangent);

	if (lane == 0u) {
		sPlacement[0] = select(is_placed, float4(centre, 1.0), float4(0.0, 0.0, 0.0, 0.0));
		sPlacement[1] = float4(normal, 0.0);
		sPlacement[2] = float4(origin, 0.0);
		sPlacement[3] = float4(tangent, 0.0);
		sPlacement[4] = float4(bitangent, 0.0);
	}
}

[numthreads(SCREEN_PROBE_RAYS, 1, 1)]
void main(uint3 group : SV_GroupID, uint ray : SV_GroupIndex)
{
	const uint tile_size = Consts.vScreen.z;
	const uint tiles_x = Consts.vScreen.w;
	const uint probe_index = group.x + group.y * tiles_x;
	const int2 tile_origin = int2(group.xy * tile_size);

	if (ray < WaveGetLaneCount()) {
		PlaceScreenProbe(ray, tile_origin, tile_size);
	}

	GroupMemoryBarrierWithGroupSync();

	const float4 placement = sPlacement[0];

	float3 radiance = float3(0.0, 0.0, 0.0);
	float3 direction = float3(0.0, 1.0, 0.0);

	// The same for the whole group
	if (placement.w > 0.0) {
		const float3 probe_normal = sPlacement[1].xyz;
		const float3 origin = sPlacement[2].xyz;
		const float3 tangent = sPlacement[3].xyz;
		const float3 bitangent = sPlacement[4].xyz;

		// Uniform over the hemisphere, stratified 8x8 and jittered per probe and frame. Unit length as it is: the
		// frame is orthonormal.
		const uint seed = ClipmapHash(probe_index * 0x9E3779B9u + asuint(Consts.vParams.z) * 0x85EBCA6Bu);
		const float jitter_u = float(ClipmapHash(seed ^ ray) & 0xFFFFu) / 65536.0;
		const float jitter_v = float(ClipmapHash(seed ^ (ray * 0x68E31DA4u)) & 0xFFFFu) / 65536.0;
		const float u = (float(ray % 8u) + jitter_u) / 8.0;
		const float v = (float(ray / 8u) + jitter_v) / 8.0;

		const float cos_theta = u;
		const float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
		float2 phi_sin_cos;
		sincos(v * 6.28318530718, phi_sin_cos.x, phi_sin_cos.y);
		direction = tangent * (phi_sin_cos.y * sin_theta) + bitangent * (phi_sin_cos.x * sin_theta) +
					probe_normal * cos_theta;

		const float ray_length = Consts.vParams.x;

		const SDFPageHit coarse_hit = SDFPageTrace(origin, direction, ray_length, Consts.vParams.y);
		const SDFPageHit hit = SDFPageRefineHit(coarse_hit, origin, direction, ray_length);

		ClipmapProxyHit nearest;
		nearest.fDistance = select(hit.bHit, hit.fDistance, ray_length);
		nearest.vNormal = float3(0.0, 0.0, 0.0);
		nearest.vAlbedo = float3(0.0, 0.0, 0.0);
		nearest.bHit = false;

		[loop]
		for (uint proxy = 0; proxy < Consts.Base.vInfo.w; proxy++) {
			ClipmapTraceProxy(Consts.Base.Proxies[proxy], origin, direction, nearest);
		}

		const bool is_hit = or(hit.bHit, nearest.bHit);
		const float3 position = origin + direction * nearest.fDistance;

		float3 face = nearest.vNormal;
		float3 albedo = nearest.vAlbedo;

		// Skips the triangle loads for misses and box hits
		if (and(hit.bHit, !nearest.bHit)) {
			const SDFPageHitRecord record = SDFPageNearestRecord(hit, position);
			face = SDFPageRecordFace(record, -direction);
			albedo = SDFPageRecordAlbedo(record);
		}

		const bool is_backface = dot(face, direction) > 0.0;
		const float3 normal = select(is_backface, -face, face);
		const bool is_lit = and(is_hit, !is_backface);

		float3 shaded = float3(0.0, 0.0, 0.0);

		if (is_lit) {
			const float angle = float(ClipmapHash(seed + ray * 0x2545F491u) & 0xFFFFu) * (6.28318530718 / 65536.0);
			shaded = SDFPageShadeHit(position, normal, albedo, float2(cos(angle), sin(angle)), Consts.Base.BakedSun,
									 Consts.Base.vLightIndices, Consts.Base.vLights.x, Consts.Base.vLights.y);
		}

		// One clipmap gather for hits and misses alike, so a wave with both doesn't run two after each other.
		// At a hit: the light that bounced before reaching it, the full gather (the cheap bounce gather has no depth
		// test and no coarser fallback, so near a crease or a wall it settles on the few probes inside it, which
		// hold black), moved off along the surface's own normal, not towards the probe (often less than the move
		// away, 0.8 m on the coarsest level, which then went through the wall the probe sits on).
		// At a miss: the light arriving from beyond, at the ray's end. Its irradiance along the ray over pi is the
		// mean radiance of the cone of directions around it, weighted to the probes behind the end, the ones the ray
		// has just come through, which are in open space it reached; the ones ahead may be past a window, outside
		// the map or beside blockers, and held far too little light.
		const float3 gather_position = select(is_hit, position, origin + direction * ray_length);
		const float3 gather_normal = select(is_hit, normal, -direction);
		const float3 gather_direction = select(is_hit, normal, direction);
		const ClipmapGatherResult gathered = ClipmapGatherAt(gather_position, gather_normal, gather_normal,
															 gather_direction, gather_direction, false);
		const bool in_clipmap = gathered.uiLevel < CLIPMAP_LEVELS;

		// Where no probe holds the end at all, the ray has left: it sees the sky, as a probe's own ray that misses
		// does. Normalized, those few weights gave whatever the least hidden probe held, often black.
		const float held = select(in_clipmap, saturate(gathered.fWeight / SCREEN_PROBE_CACHE_WEIGHT), 0.0);
		const float3 hit_radiance = shaded + select(in_clipmap, albedo * gathered.vDiffuse, float3(0.0, 0.0, 0.0));
		const float3 miss_radiance = lerp(Consts.Base.vSkyRadiance.rgb, gathered.vDiffuse, held);

		radiance = select(is_hit, select(is_lit, hit_radiance, float3(0.0, 0.0, 0.0)), miss_radiance);
	}

	radiance = select(GIIsFinite(radiance), radiance, float3(0.0, 0.0, 0.0));

	// L1 spherical harmonics, Monte Carlo over the hemisphere: each sample weighs 2 pi / N. Stored cosine convolved,
	// see SCREEN_PROBE_SH_STORE_0.
	const float3 contribution = radiance * (6.28318530718 / float(SCREEN_PROBE_RAYS));

	const float3 c0 = WaveActiveSum(contribution * SCREEN_PROBE_SH_STORE_0);
	const float3 c1x = WaveActiveSum(contribution * (SCREEN_PROBE_SH_STORE_1 * direction.x));
	const float3 c1y = WaveActiveSum(contribution * (SCREEN_PROBE_SH_STORE_1 * direction.y));
	const float3 c1z = WaveActiveSum(contribution * (SCREEN_PROBE_SH_STORE_1 * direction.z));

	// A 64 wide group is two waves on some GPUs: each wave's first lane leaves its sums in shared memory
	const uint wave = ray / WaveGetLaneCount();

	if (WaveIsFirstLane()) {
		sRadiance[wave * 4 + 0] = float4(c0, 0.0);
		sRadiance[wave * 4 + 1] = float4(c1x, 0.0);
		sRadiance[wave * 4 + 2] = float4(c1y, 0.0);
		sRadiance[wave * 4 + 3] = float4(c1z, 0.0);
	}

	GroupMemoryBarrierWithGroupSync();

	if (ray == 0) {
		const uint wave_count = (SCREEN_PROBE_RAYS + WaveGetLaneCount() - 1) / WaveGetLaneCount();
		float3 sums[4] = { float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0) };

		for (uint index = 0; index < wave_count; index++) {
			for (uint k = 0; k < 4; k++) {
				sums[k] += sRadiance[index * 4 + k].xyz;
			}
		}

		// Readers take every written probe as finite, so a broken one is written as unplaced and dark. An overflow
		// in the sums is the only way left to one.
		const bool is_finite = and(and(GIIsFinite(sums[0]), GIIsFinite(sums[1])),
								   and(GIIsFinite(sums[2]), GIIsFinite(sums[3])));
		const float4 zero = float4(0.0, 0.0, 0.0, 0.0);

		const uint base = probe_index * SCREEN_PROBE_STRIDE;
		bScreenProbes[base + 0] = select(is_finite, placement, zero);
		bScreenProbes[base + 1] = sPlacement[1];
		// Per channel: c0, c1 (x, y, z)
		bScreenProbes[base + 2] = select(is_finite, float4(sums[0].r, sums[1].r, sums[2].r, sums[3].r), zero);
		bScreenProbes[base + 3] = select(is_finite, float4(sums[0].g, sums[1].g, sums[2].g, sums[3].g), zero);
		bScreenProbes[base + 4] = select(is_finite, float4(sums[0].b, sums[1].b, sums[2].b, sums[3].b), zero);
	}
}
