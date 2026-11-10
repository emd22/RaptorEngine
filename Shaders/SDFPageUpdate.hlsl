F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "SDFPageCommon.hlsli"

/// Mirrors SDFPageScene::Job
struct SDFPageJob
{
	uint uiSlot;
	uint uiAtlas;
	uint uiFirstRef;
	uint uiRefCount;
	float4 vOriginAndTexel;
};

struct SDFPageUpdateConsts
{
	/// Job count, then the atlas size in pages
	uint4 vInfo;
	/// Margin as a fraction of a page
	float4 vParams;
};

F_StructBuffer(bJobs, SDFPageJob, 0, 0);
F_StructBuffer(bRefs, uint, 1, 0);
F_StructBuffer(bTriangles, SDFPageTriangle, 2, 0);
F_RWStructBuffer(bIndirection, uint, 3, 0);

[[vk::image_format("r8")]] F_RWTexture3D(tAtlas, float, 4, 0);
[[vk::image_format("r16ui")]] F_RWTexture3D(tNearest, uint, 5, 0);
F_RWStructBuffer(bAtlasFirstRef, uint, 6, 0);

F_FrameData(SDFPageUpdateConsts, Consts, 1);

#define SDF_PAGE_UPDATE_THREADS 64
#define SDF_PAGE_UPDATE_COLUMNS 1024
#define TEXELS_PER_THREAD ((SDF_PAGE_TEXELS * SDF_PAGE_TEXELS * SDF_PAGE_TEXELS) / SDF_PAGE_UPDATE_THREADS)

float Dot2(float3 v) { return dot(v, v); }

/// Squared distance from `p` to the page_triangle abc
float TriangleDistanceSq(float3 p, float3 a, float3 b, float3 c)
{
	const float3 ba = b - a;
	const float3 pa = p - a;
	const float3 cb = c - b;
	const float3 pb = p - b;
	const float3 ac = a - c;
	const float3 pc = p - c;
	const float3 normal = cross(ba, ac);

	const float sides = sign(dot(cross(ba, normal), pa)) + sign(dot(cross(cb, normal), pb)) +
						sign(dot(cross(ac, normal), pc));

	const float edge = min(min(Dot2(ba * saturate(dot(ba, pa) / Dot2(ba)) - pa),
							   Dot2(cb * saturate(dot(cb, pb) / Dot2(cb)) - pb)),
						   Dot2(ac * saturate(dot(ac, pc) / Dot2(ac)) - pc));

	const float plane = dot(normal, pa);
	const float face = plane * plane / Dot2(normal);

	return select(sides < 2.0, edge, face);
}

uint3 GetLocalTexel(uint thread, uint index)
{
	const uint texel = thread + index * SDF_PAGE_UPDATE_THREADS;

	return uint3(texel & (SDF_PAGE_TEXELS - 1), (texel / SDF_PAGE_TEXELS) & (SDF_PAGE_TEXELS - 1), texel / (SDF_PAGE_TEXELS * SDF_PAGE_TEXELS));
}

[numthreads(SDF_PAGE_UPDATE_THREADS, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint job_index = group.x + group.y * SDF_PAGE_UPDATE_COLUMNS;

	if (job_index >= Consts.vInfo.x) {
		return;
	}

	const SDFPageJob job = bJobs[job_index];

	if (and(job.uiRefCount > 0, job.uiAtlas < SDF_PAGE_PENDING)) {
		const float texel_size = job.vOriginAndTexel.w;
		const float3 origin = job.vOriginAndTexel.xyz;

		float3 positions[TEXELS_PER_THREAD];
		float best[TEXELS_PER_THREAD];

		// Each thread also finds the nearest page_triangle to the centre of one block of texels
		const uint3 block = uint3(thread % SDF_PAGE_NEAREST_SIZE, (thread / SDF_PAGE_NEAREST_SIZE) % SDF_PAGE_NEAREST_SIZE,
								  thread / (SDF_PAGE_NEAREST_SIZE * SDF_PAGE_NEAREST_SIZE));
		const float3 block_centre = origin + (float3(block * SDF_PAGE_NEAREST_BLOCK) + SDF_PAGE_NEAREST_BLOCK * 0.5) *
												 texel_size;
		float block_best = 1e30;
		uint block_ref = 0;

		[unroll]
		for (uint index = 0; index < TEXELS_PER_THREAD; index++) {
			positions[index] = origin + (float3(GetLocalTexel(thread, index)) + 0.5) * texel_size;
			best[index] = 1e30;
		}

		[loop]
		for (uint ref = 0; ref < job.uiRefCount; ref++) {
			const SDFPageTriangle page_triangle = bTriangles[bRefs[job.uiFirstRef + ref]];

			[unroll]
			for (uint index = 0; index < TEXELS_PER_THREAD; index++) {
				best[index] = min(best[index], TriangleDistanceSq(positions[index], page_triangle.vA.xyz, page_triangle.vB.xyz,
																  page_triangle.vC.xyz));
			}

			const float block_distance = TriangleDistanceSq(block_centre, page_triangle.vA.xyz, page_triangle.vB.xyz,
															page_triangle.vC.xyz);
			block_ref = select(block_distance < block_best, ref, block_ref);
			block_best = min(block_best, block_distance);
		}

		const float margin = texel_size * SDF_PAGE_TEXELS * Consts.vParams.x;
		const uint3 atlas_origin = uint3(SDFPageAtlasOrigin(job.uiAtlas));

		[unroll]
		for (uint index = 0; index < TEXELS_PER_THREAD; index++) {
			tAtlas[atlas_origin + GetLocalTexel(thread, index)] = saturate(sqrt(best[index]) / margin);
		}

		tNearest[atlas_origin / SDF_PAGE_NEAREST_BLOCK + block] = min(block_ref, 0xFFFFu);

		if (thread == 0) {
			bAtlasFirstRef[job.uiAtlas] = job.uiFirstRef;
		}
	}

	if (thread == 0) {
		bIndirection[job.uiSlot] = job.uiAtlas;
	}
}
