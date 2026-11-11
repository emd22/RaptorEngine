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
F_Texture2D(tIrradiance0, 12, 0);
F_Texture2D(tIrradiance1, 13, 0);
F_Texture2D(tIrradiance2, 14, 0);
F_Texture2D(tMoments0, 15, 0);
F_Texture2D(tMoments1, 16, 0);
F_Texture2D(tMoments2, 17, 0);
F_StructBuffer(bState, uint, 18, 0);
F_StructBuffer(bProbeData, float4, 19, 0);

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

	// A probe traced for the first time starts on its grid point
	const float3 offset = select(ClipmapJobIsReset(job), float3(0.0, 0.0, 0.0),
								 bProbeData[ClipmapProbeIndex(level_index, ClipmapJobSlot(job))].xyz);
	const float3 origin = ClipmapProbePosition(level, ClipmapSlotProbe(level, ClipmapJobSlot(job))) + offset;
	const float3 direction = ClipmapRayDirection(ray, Consts.mRayRotation);

	const float ray_length = level.vParams.z;
	const SDFPageHit hit = SDFPageTrace(origin, direction, ray_length, level.vParams.w);

	float3 radiance = Consts.vSkyRadiance.rgb;
	float distance = ray_length;

	if (hit.bHit) {
		const float3 position = origin + direction * hit.fDistance;
		const SDFPageTriangle page_triangle = SDFPageNearestTriangle(hit, position);

		const float3 face = normalize(cross(page_triangle.vB.xyz - page_triangle.vA.xyz,
											page_triangle.vC.xyz - page_triangle.vA.xyz));
		const bool is_backface = dot(face, direction) > 0.0;
		const float3 normal = select(is_backface, -face, face);

		const float3 albedo = SDFPageTriangleAlbedo(page_triangle);
		const float angle = float(job_index * 64u + ray) * 2.39996323;
		radiance = SDFPageShadeHit(position, normal, albedo, float2(cos(angle), sin(angle)), Consts.BakedSun,
								   Consts.vLightIndices, Consts.vLights.x, Consts.vLights.y);

		// The hit is seen from the probe, so the view bias points back along the ray
		const float dither = frac(float(job_index * 64u + ray + Consts.vInfo.y * 977u) * 0.6180339887);
		const ClipmapGatherResult bounced = ClipmapGatherDiffuse(position, normal, -direction, dither);
		const bool has_bounce = and(HAS_FLAG(Consts.vInfo.z, CLIPMAP_FLAG_BOUNCE), bounced.uiLevel < CLIPMAP_LEVELS);

		radiance += select(has_bounce, albedo * bounced.vDiffuse, float3(0.0, 0.0, 0.0));

		// The back of a surface is inside something, or outside the map, and sends nothing back, as in the bake
		radiance = select(is_backface, float3(0.0, 0.0, 0.0), radiance);

		// Backfaces are kept as negative distances, for telling probes inside walls apart later
		distance = select(is_backface, -hit.fDistance, hit.fDistance);
	}

	bRays[job_index * CLIPMAP_RAYS + ray] = float4(radiance, distance);
}
