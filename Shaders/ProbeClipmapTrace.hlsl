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

F_FrameData(ProbeClipmapConsts, Consts, 1);

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

	const float3 origin = ClipmapProbePosition(level, ClipmapSlotProbe(level, ClipmapJobSlot(job)));
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

		const float angle = float(job_index * 64u + ray) * 2.39996323;
		radiance = SDFPageShadeHit(position, normal, SDFPageTriangleAlbedo(page_triangle), float2(cos(angle), sin(angle)),
								   Consts.BakedSun, Consts.vLightIndices, Consts.vLights.x, Consts.vLights.y);

		// Backfaces are kept as negative distances, for telling probes inside walls apart later
		distance = select(is_backface, -hit.fDistance, hit.fDistance);
	}

	bRays[job_index * CLIPMAP_RAYS + ray] = float4(radiance, distance);
}
