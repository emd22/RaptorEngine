F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "SDFPageCommon.hlsli"
#include "LightingCommon.hlsli"
#include "FogConsts.hlsli"

struct SDFPageDebugConsts
{
	SDFPageScene Scene;
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Target width and height, view, frame
	uint4 vInfo;
	/// Cone slope and length of the GI rays, pre-exposure
	float4 vParams;
	BakedSunShadowParams BakedSun;
	/// Light buffer slots shaded at each hit
	uint4 vLightIndices;
	/// Light count, flags (SHADE_FLAG_*), probe stride
	uint4 vLights;
	/// Radiance of the rays that miss
	float4 vSkyRadiance;
};

F_StructBuffer(bSDFPageIndirection, uint, 0, 0);
F_Texture3D(tSDFPageAtlas, 1, 0);
F_Texture2D(tDepth, 2, 0);
F_Texture2D(tNormal, 3, 0);
F_Texture2D(tMotion, 4, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(5, 0)]] RWTexture2D<float4> tOutput;

F_StructBuffer(bTriangles, SDFPageTriangle, 7, 0);
F_StructBuffer(bRefs, uint, 8, 0);
F_StructBuffer(bAtlasFirstRef, uint, 9, 0);
Texture3D<uint> tNearestTexture : register(t10, space0);
SamplerState tNearest : register(s10, space0);

F_CBuffer(FSLightBuffer, 11, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_ShadowTexture2D(tShadowAtlas, 12, 0);
F_ShadowTexture2D(tFogShadowAtlas, 13, 0);
F_StructBuffer(bFogShadowPages, FogShadowPage, 14, 0);

#include "BakedSunShadow.hlsli"

F_FrameData(SDFPageDebugConsts, Consts, 1);

#define SDF_PAGE_SCENE Consts.Scene
#include "SDFPageTrace.hlsli"
#include "SDFPageShade.hlsli"

/// Mirrors SDFPageScene::eDebugView
#define VIEW_NORMALS 1
#define VIEW_STEPS 2
#define VIEW_GI_RAYS 3
#define VIEW_DEPTH_CHECK 4
#define VIEW_MOTION 5
#define VIEW_ROUGHNESS 6
#define VIEW_LEVELS 7
#define VIEW_GI_STEPS 8
/// Every 8x8 thread group is one screen probe: its 64 rays leave the surface under the group's first pixel
#define VIEW_PROBE_RAYS 9
/// The probe rays of VIEW_PROBE_RAYS, with the light leaving each hit (albedo, sun, local lights) or the sky
#define VIEW_PROBE_SHADE 10
#define PROBE_SIZE 8

#define PRIMARY_MAX_DISTANCE 500.0
#define GI_MAX_DISTANCE 50.0
/// Stored depths at or above this are the view model, see VIEWMODEL_DEPTH_SCALE in SSAO.hlsl
#define VIEWMODEL_STORED_DEPTH 0.99
#define GI_AO_DISTANCE 2.0
#define PI 3.14159265

static const float3 scSkyColour = float3(0.05, 0.07, 0.1);

float3 WorldFromDepth(float2 uv, float stored_depth)
{
	// Stored depth is 1 - NDC depth (the viewport is flipped)
	const float4 ndc = float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0);
	const float4 world = mul(ndc, Consts.mInvViewProjection);

	return world.xyz / world.w;
}

uint Hash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float3 CosineDirection(float3 normal, float2 xi)
{
	// Branchless orthonormal basis (Duff et al. 2017)
	const float sign_z = select(normal.z >= 0.0, 1.0, -1.0);

	const float a = -1.0 / (sign_z + normal.z);
	const float b = normal.x * normal.y * a;

	const float3 tangent = float3(1.0 + sign_z * normal.x * normal.x * a, sign_z * b, -sign_z * normal.x);
	const float3 bitangent = float3(b, sign_z + normal.y * normal.y * a, -normal.y);

	const float radius = sqrt(xi.x);
	const float phi = 2.0 * PI * xi.y;

	return tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + normal * sqrt(max(1.0 - xi.x, 0.0));
}

float3 HeatColour(float value)
{
	const float x = saturate(value);
	return saturate(float3(x * 2.0 - 0.5, 1.0 - abs(x * 2.0 - 1.0) * 1.2, 1.0 - x * 2.0));
}

float3 LevelColour(uint level)
{
	static const float3 scColours[SDF_PAGE_MAX_LEVELS] = {
		float3(0.9, 0.3, 0.3), float3(0.3, 0.9, 0.3), float3(0.3, 0.4, 0.95), float3(0.9, 0.85, 0.3), float3(0.8, 0.3, 0.9),
	};

	return scColours[min(level, SDF_PAGE_MAX_LEVELS - 1)];
}


float3 ShadePixel(uint2 pixel, uint2 group, uint2 local)
{
	const uint2 group_pixel = group * PROBE_SIZE;
	const uint view = Consts.vInfo.z;
	const float2 uv = (float2(pixel) + 0.5) / float2(Consts.vInfo.xy);

	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	const float4 normal_sample = F_SampleLoad(tNormal, int3(pixel, 0));
	const float2 motion = F_SampleLoad(tMotion, int3(pixel, 0)).rg;

	const bool is_sky = stored_depth <= 0.0;
	const bool is_view_model = stored_depth >= VIEWMODEL_STORED_DEPTH;

	const float3 eye = Consts.vEyePosition.xyz;
	const float3 surface = WorldFromDepth(uv, stored_depth);
	const float3 far_point = WorldFromDepth(uv, 0.5);
	const float3 view_direction = normalize(far_point - eye);

	float3 colour = scSkyColour;

	if (view == VIEW_MOTION) {
		colour = float3(max(motion.x, 0.0), max(-motion.x, 0.0), abs(motion.y)) * 40.0;
	}
	else if (view == VIEW_ROUGHNESS) {
		colour = normal_sample.www;
	}
	else if (or(or(view == VIEW_GI_RAYS, view == VIEW_GI_STEPS), or(view == VIEW_PROBE_RAYS, view == VIEW_PROBE_SHADE))) {
		colour = float3(1.0, 1.0, 1.0);

		const bool is_probe = or(view == VIEW_PROBE_RAYS, view == VIEW_PROBE_SHADE);
		const uint group_columns = (Consts.vInfo.x + PROBE_SIZE - 1) / PROBE_SIZE;

		const bool is_skipped = and(is_probe, ((group.x + group.y * group_columns) % max(Consts.vLights.z, 1u)) != 0);

		const uint2 origin_pixel = select(is_probe, min(group_pixel, Consts.vInfo.xy - 1), pixel);
		const float2 origin_uv = (float2(origin_pixel) + 0.5) / float2(Consts.vInfo.xy);
		const float origin_depth = F_SampleLoad(tDepth, int3(origin_pixel, 0)).r;
		const float3 origin_surface = WorldFromDepth(origin_uv, origin_depth);
		const float4 origin_normal = F_SampleLoad(tNormal, int3(origin_pixel, 0));

		const float ray_length = Consts.vParams.y;

		// The skybox is far outside the pages, where its depth gives no usable position
		const bool is_traceable = length(origin_surface - eye) < GI_MAX_DISTANCE;
		const bool has_surface = and(origin_depth > 0.0, origin_depth < VIEWMODEL_STORED_DEPTH);

		if (and(and(has_surface, is_traceable), !is_skipped)) {
			const float3 normal = normalize(origin_normal.xyz);
			const uint seed = Hash(pixel.x + pixel.y * 7919u + Consts.vInfo.w * 104729u);
			const float2 jitter = float2(Hash(seed) & 0xFFFFu, Hash(seed ^ 0x9E3779B9u) & 0xFFFFu) / 65536.0;
			// A probe's rays are stratified over its hemisphere, one per cell of an 8x8 grid
			const float2 xi = select(is_probe, (float2(local) + jitter) / PROBE_SIZE, jitter);

			const float3 direction = CosineDirection(normal, xi);
			const float3 origin = origin_surface + normal * (SDFPageTexelAt(origin_surface) * 1.5);

			const SDFPageHit hit = SDFPageTrace(origin, direction, ray_length, Consts.vParams.x);
			const float visibility = select(hit.bHit, saturate(hit.fDistance / GI_AO_DISTANCE), 1.0);

			colour = visibility.xxx;

			if (view == VIEW_PROBE_SHADE) {
				float3 radiance = Consts.vSkyRadiance.rgb;

				if (hit.bHit) {
					const float3 position = origin + direction * hit.fDistance;
					const SDFPageTriangle page_triangle = SDFPageNearestTriangle(hit, position);
					const float3 face = normalize(cross(page_triangle.vB.xyz - page_triangle.vA.xyz, page_triangle.vC.xyz - page_triangle.vA.xyz));
					const float3 hit_normal = select(dot(face, direction) > 0.0, -face, face);

					const float angle = float(Hash(seed ^ 0x68E31DA4u) & 0xFFFFu) * (2.0 * PI / 65536.0);
					radiance = SDFPageShadeHit(position, hit_normal, SDFPageTriangleAlbedo(page_triangle),
											   float2(cos(angle), sin(angle)), Consts.BakedSun, Consts.vLightIndices,
											   Consts.vLights.x, Consts.vLights.y);
				}

				colour = radiance * Consts.vParams.z;
			}

			if (view == VIEW_GI_STEPS) {
				colour = select(hit.uiSteps >= SDF_PAGE_MAX_STEPS, float3(1.0, 0.0, 1.0), HeatColour(float(hit.uiSteps) / 96.0));
			}
		}
	}
	else {
		const SDFPageHit hit = SDFPageTrace(eye, view_direction, PRIMARY_MAX_DISTANCE, 0.0);

		if (view == VIEW_STEPS) {
			colour = HeatColour(float(hit.uiSteps) / 96.0);
		}
		else if (hit.bHit) {
			const float3 position = eye + view_direction * hit.fDistance;
			const float texel = Consts.Scene.Levels[hit.uiLevel].vParams.w;

			if (view == VIEW_LEVELS) {
				colour = LevelColour(hit.uiLevel) * (0.5 + 0.5 * abs(SDFPageNormal(position, texel).y));
			}
			else if (view == VIEW_NORMALS) {
				colour = SDFPageNormal(position, texel) * 0.5 + 0.5;
			}
		}

		if (view == VIEW_DEPTH_CHECK) {
			const float depth_distance = length(surface - eye);
			const float texel = Consts.Scene.Levels[hit.uiLevel].vParams.w;
			// The hit is short of the surface by up to the hit threshold, more at grazing angles
			const float tolerance = texel * 3.0 + depth_distance * 0.01;
			const float error = hit.fDistance - depth_distance;

			colour = float3(0.1, 0.7, 0.2);

			if (is_view_model) {
				colour = float3(0.2, 0.2, 0.2);
			}
			else if (is_sky) {
				colour = select(hit.bHit, float3(1.0, 0.1, 0.1), scSkyColour);
			}
			else if (!hit.bHit || error > tolerance) {
				colour = float3(0.1, 0.2, 1.0);
			}
			else if (error < -tolerance) {
				colour = float3(1.0, 0.1, 0.1);
			}
		}
	}

	return colour;
}

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
	const uint2 pixel = thread.xy;

	if (all(pixel < Consts.vInfo.xy)) {
		tOutput[pixel] = float4(ShadePixel(pixel, group.xy, local.xy), 1.0);
	}
}
