#ifndef SDF_PAGE_SHADE_HLSLI
#define SDF_PAGE_SHADE_HLSLI

#include "SDFPageTrace.hlsli"
#include "BakedSunShadow.hlsli"


/// Mirrors scShadeMaxLights in Src/Renderer/SDFPageScene.hpp
#define SHADE_MAX_LIGHTS 4
#define SHADE_FLAG_BAKED_SUN 0x01
#define SHADE_SHADOW_OFFSET 0.05

/// The triangle nearest the block of texels a hit stopped in
SDFPageTriangle SDFPageNearestTriangle(SDFPageHit hit, float3 position)
{
	const SDFPageLevel level = SDF_PAGE_SCENE.Levels[hit.uiLevel];
	const float3 local = (position - float3(hit.vCoord) * level.vParams.x) * level.vParams.y * SDF_PAGE_TEXELS;
	const uint3 block = uint3(clamp(int3(floor(local / SDF_PAGE_NEAREST_BLOCK)), 0, SDF_PAGE_NEAREST_SIZE - 1));
	const uint3 texel = uint3(SDFPageAtlasOrigin(hit.uiAtlas)) / SDF_PAGE_NEAREST_BLOCK + block;
	const uint ref = F_SampleLoad(tNearest, int4(texel, 0));

	return bTriangles[bRefs[bAtlasFirstRef[hit.uiAtlas] + ref]];
}

/// Unit face normal of the triangle, turned to face against `direction`
float3 SDFPageHitNormal(SDFPageTriangle page_triangle, float3 direction)
{
	const float3 face = normalize(cross(page_triangle.vB.xyz - page_triangle.vA.xyz,
										page_triangle.vC.xyz - page_triangle.vA.xyz));

	return select(dot(face, direction) > 0.0, -face, face);
}

float SDFPageLocalShadow(Light light, float3 position_ws)
{
	const float4 atlas_rect = light.vShadowAtlasRect;
	const float4 position_ls = mul(float4(position_ws, 1.0), light.LightCameraMatrix);
	const float3 position_ndc = position_ls.xyz / position_ls.w;

	const float2 shadow_uv = position_ndc.xy * 0.5 + 0.5;
	const float shadow_z = 1.0 - position_ndc.z;

	const bool has_region = atlas_rect.x > 0.0;
	const bool outside = or(position_ls.w <= 0.0, or(any(saturate(shadow_uv) != shadow_uv), shadow_z <= 0.0));
	const float lit = F_SampleCmpLevelZero(tShadowAtlas, shadow_uv * atlas_rect.xy + atlas_rect.zw, shadow_z);

	return select(or(!has_region, outside), 1.0, lit);
}

/**
 * Radiance a lambertian hit sends back along the ray, from the light buffer slots in `light_indices` (the first
 * `light_count` of them) and, with SHADE_FLAG_BAKED_SUN in `flags`, the baked sun visibility
 */
float3 SDFPageShadeHit(float3 position, float3 normal, float3 albedo, float2 rotation, BakedSunShadowParams baked_sun,
					   uint4 light_indices, uint light_count, uint flags)
{
	const float3 shadow_position = position + normal * SHADE_SHADOW_OFFSET;
	const bool has_baked_sun = HAS_FLAG(flags, SHADE_FLAG_BAKED_SUN);
	const uint count = min(light_count, SHADE_MAX_LIGHTS);

	float3 irradiance = float3(0.0, 0.0, 0.0);

	[loop]
	for (uint index = 0; index < count; index++) {
		const Light light = Lights[light_indices[index]];

		const bool is_directional = light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL;
		const bool is_spot = light.uiLightType == FX_LIGHT_TYPE_SPOT;

		const float3 to_light = light.vLightPosition - position;
		const float dist_sq = dot(to_light, to_light);
		const float3 L = select(is_directional, light.vLightPosition, to_light * rsqrt(max(dist_sq, 1e-8)));

		float attenuation = light.fIntensity * select(is_directional, 1.0, AttenuationSmooth(dist_sq, light.fInvRadiusSq));
		attenuation *= select(is_spot, AttenuationSpot(L, light), 1.0);
		attenuation *= saturate(dot(normal, L));

		// The light is the same for the whole wave, so this does not diverge
		float visibility = 1.0;

		if (is_directional) {
			visibility = select(has_baked_sun, SampleBakedSunShadowPages(baked_sun, shadow_position, 1.0, rotation), 1.0);
		}
		else {
			visibility = SDFPageLocalShadow(light, shadow_position);
		}

		irradiance += light.vLightColor * (attenuation * visibility);
	}

	return albedo * irradiance * FX_MATH_1_OVER_PI;
}

#endif
