F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "SDFPageCommon.hlsli"
#include "LightingCommon.hlsli"
#include "FogConsts.hlsli"
#include "ProbeClipmapCommon.hlsli"
#include "ProbeClipmapConsts.hlsli"

F_StructBuffer(bSDFPageIndirection, uint, 0, 0);
F_Texture3D(tSDFPageAtlas, 1, 0);
F_StructBuffer(bTriangles, SDFPageTriangle, 2, 0);
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

F_StructBuffer(bJobs, uint, 10, 0);
F_RWStructBuffer(bRays, float4, 11, 0);

// Last frame's probes, for the light that bounced before reaching each hit
F_Texture2D(tIrradiance, 12, 0);
F_Texture2D(tMoments, 15, 0);
F_StructBuffer(bProbes, uint4, 19, 0);

F_FrameData(ProbeClipmapConsts, Consts, 1);

#define CLIPMAP_SCENE Consts.Clipmap
#include "ProbeClipmapSample.hlsli"

#define SDF_PAGE_SCENE Consts.SDFPages
#include "SDFPageTrace.hlsli"
#include "SDFPageShade.hlsli"

[numthreads(CLIPMAP_RAYS, 1, 1)]
void main(uint3 group : SV_GroupID, uint ray : SV_GroupIndex)
{
	const uint job_index = group.x + group.y * CLIPMAP_JOB_COLUMNS;

	if (job_index >= Consts.vInfo.x) {
		return;
	}

	const uint job = bJobs[job_index];
	const uint level_index = ClipmapJobLevel(job);
	const ClipmapLevel level = Consts.Clipmap.Levels[level_index];

	const uint slot = ClipmapJobSlot(job);
	const int3 probe = ClipmapSlotProbe(level, slot);

	// A probe new to its slot starts on its grid point. The blend moves it from there over its first updates.
	const ClipmapProbe data = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level_index, slot)], probe);
	const float3 offset = select(ClipmapJobIsReset(job), float3(0.0, 0.0, 0.0), data.vOffset);
	const float3 origin = ClipmapProbePosition(level, probe) + offset;
	const float3 direction = ClipmapRayDirection(ray, ClipmapRayRotation(level_index, slot, Consts.vInfo.y));

	const float ray_length = level.vParams.z;
	const SDFPageHit coarse_hit = SDFPageTrace(origin, direction, ray_length, level.vParams.w);
	const SDFPageHit hit = SDFPageRefineHit(coarse_hit, origin, direction, ray_length);

	// A sphere traced ray stops once it is within a fraction of a texel of a surface, which for a ray grazing a wall or
	// ceiling is far short of where it meets it: up to that fraction over the sine of the grazing angle. Recorded as
	// is, a probe beside a ceiling sees the ceiling much closer than it is in the directions along it, and the depth
	// test then hides the ceiling further out from it, which shows as dark blotches. The depth the probe keeps is
	// carried on to the plane of the triangle it stopped at, when it meets it within that reach. The hit is still lit
	// where the ray stopped: near a corner the plane can lie past the wall the ray was about to reach, outside.
	float page_distance = hit.fDistance;
	float depth_distance = hit.fDistance;
	float3 page_face = float3(0.0, 1.0, 0.0);
	float3 page_albedo = float3(0.0, 0.0, 0.0);

	if (hit.bHit) {
		const SDFPageTriangle page_triangle = SDFPageNearestTriangle(hit, origin + direction * hit.fDistance);

		page_face = normalize(cross(page_triangle.vB.xyz - page_triangle.vA.xyz,
									page_triangle.vC.xyz - page_triangle.vA.xyz));
		page_albedo = SDFPageTriangleAlbedo(page_triangle);

		const float texel = Consts.SDFPages.Levels[hit.uiLevel].vParams.w;
		const float facing = dot(direction, page_face);
		const float abs_facing = max(abs(facing), 0.05);
		const float plane_distance = dot(page_triangle.vA.xyz - origin, page_face) / select(facing < 0.0, -abs_facing,
																							   abs_facing);
		// Capped, so a triangle that isn't the one the ray was heading for can't carry it far
		const float reach = min(texel * (SDF_PAGE_HIT_TEXELS + 1.0) / abs_facing, texel * 8.0);

		const bool meets_plane = and(abs(facing) > 1e-4,
									 and(plane_distance > hit.fDistance - texel, plane_distance < hit.fDistance + reach));

		depth_distance = clamp(select(meets_plane, plane_distance, hit.fDistance), 0.0, ray_length);
	}

	// The boxes of the objects tagged DynamicGI, which move and so are not in the SDF pages. The nearest of them and
	// the page hit is what the ray reached.
	ClipmapProxyHit nearest;
	nearest.fDistance = select(hit.bHit, page_distance, ray_length);
	nearest.vNormal = float3(0.0, 0.0, 0.0);
	nearest.vAlbedo = float3(0.0, 0.0, 0.0);
	nearest.bHit = false;

	[loop]
	for (uint proxy = 0; proxy < Consts.vInfo.w; proxy++) {
		ClipmapTraceProxy(Consts.Proxies[proxy], origin, direction, nearest);
	}

	float3 radiance = Consts.vSkyRadiance.rgb;
	float distance = ray_length;

	if (or(hit.bHit, nearest.bHit)) {
		const float hit_distance = nearest.fDistance;
		const float3 position = origin + direction * hit_distance;

		// A box hit has its own face, a page hit the triangle's
		const float3 face = select(nearest.bHit, nearest.vNormal, page_face);
		const float3 albedo = select(nearest.bHit, nearest.vAlbedo, page_albedo);

		const bool is_backface = dot(face, direction) > 0.0;
		const float3 normal = select(is_backface, -face, face);
		const float angle = float(ClipmapHash(job_index * 64u + ray + Consts.vInfo.y * 0x9E3779B9u) & 0xFFFFu) *
							(6.28318530718 / 65536.0);
		radiance = float3(0.0, 0.0, 0.0);

		// The back of a surface is inside something, or outside the map, and sends nothing back, as in the bake, so
		// it is neither shaded nor gathered for
		if (!is_backface) {
			radiance = SDFPageShadeHit(position, normal, albedo, float2(cos(angle), sin(angle)), Consts.BakedSun,
									   Consts.vLightIndices, Consts.vLights.x, Consts.vLights.y);

			// The light that bounced before reaching the hit, from last frame's probes
			if (HAS_FLAG(Consts.vInfo.z, CLIPMAP_FLAG_BOUNCE)) {
				const float3 bounced = ClipmapGatherBounce(position, normal, -direction);
				radiance += select(bounced.x >= 0.0, albedo * bounced, float3(0.0, 0.0, 0.0));
			}
		}

		// A box in front of the page hit is where the ray ends, otherwise the depth carried on to the plane
		const float recorded = select(nearest.bHit, hit_distance, depth_distance);

		// Backfaces are kept as negative distances, for telling probes inside walls apart. Never -0: a probe on a
		// surface hits it at distance 0, and -0 is not below zero, so its back faces went uncounted and it was never
		// moved off the wall, leaving a black probe on every grid aligned surface.
		distance = select(is_backface, -max(recorded, 1e-5), recorded);
	}

	bRays[job_index * CLIPMAP_RAYS + ray] = float4(radiance, distance);
}
