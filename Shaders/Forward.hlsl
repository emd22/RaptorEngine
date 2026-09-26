
#include "./Helper.hlsl"


///////////////////////////////////
// Vertex Shader
///////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    /// xyz is the tangent, w the bitangent's handedness. See Vertex<> in Src/Renderer/Vertex.hpp.
    float4 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
#ifdef USE_SKINNING
    uint4 vJointIndices : ATTR0;
    float4 vJointWeights : ATTR1;
#endif
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS : NORMAL;
    float2 vUV       : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    /// xyz is the world space tangent, w the bitangent's handedness. The bitangent is rebuilt in the pixel shader
    /// instead of interpolated, which also costs one interpolator less.
    float4 vTangentWS : TANGENT;
#endif

    /// Vertex position in world space
    float3 vPositionWS   : POSITION;

    uint uiMaterialIndex : ATTR0;

};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
    uint uiTileColumns;
    uint Flags;
    uint2 vTargetSize;
    uint uiBoneBase;
};

#ifdef USE_SKINNING

F_StructBuffer(bBones, BoneMtx, 3, 1);

#endif // USE_SKINNING

F_StructBuffer(bObjectBuffer, Object, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 world_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mWorld;

    // This pass is depth tested GREATER_OR_EQUAL against the DepthNormal prepass, so its clip position has to be
    // bit identical to the one DepthNormal.hlsl computes. Matrix multiplication is associative in exact arithmetic
    // but not in floating point: going via the world position instead of this concatenation shifts the depth by a
    // few ULPs, and every triangle that lands on the wrong side of the test is dropped. Do not "optimize" this.
    float4x4 MVP = mul(world_matrix, VSConst.mViewProjection);

#ifdef USE_SKINNING
    const uint bone_base = VSConst.uiBoneBase;

    float4x4 skin_xform = input.vJointWeights.x * bBones[bone_base + input.vJointIndices.x]
        + input.vJointWeights.y * bBones[bone_base + input.vJointIndices.y]
        + input.vJointWeights.z * bBones[bone_base + input.vJointIndices.z]
        + input.vJointWeights.w * bBones[bone_base + input.vJointIndices.w];

    // Posed vertex in model space
    float4 position_ms = mul(float4(input.vPosition, 1.0), skin_xform);

    output.vPosition = mul(position_ms, MVP);
    output.vNormalWS = normalize(mul(mul(input.vNormal, (float3x3)skin_xform), (float3x3)world_matrix));
#else
    float4 position_ms = float4(input.vPosition, 1.0);

    output.vPosition = mul(position_ms, MVP);
    output.vNormalWS = normalize(mul(input.vNormal, (float3x3)world_matrix));
#endif

#ifdef USE_NORMAL_MAPS
#ifdef USE_SKINNING
    const float3 tangent_ws = mul(mul(input.vTangent.xyz, (float3x3)skin_xform), (float3x3)world_matrix);
#else
    const float3 tangent_ws = mul(input.vTangent.xyz, (float3x3)world_matrix);
#endif
    // The handedness rides along untouched; the pixel shader rebuilds the bitangent from it
    output.vTangentWS = float4(normalize(tangent_ws), input.vTangent.w);
#endif

    output.vUV = input.vUV;

    // Uses the posed position so skinned meshes are lit (probes, lights, shadows) where they are drawn,
    // not at their bind pose.
    float4 position_ws = mul(position_ms, world_matrix);
	output.vPositionWS = position_ws.xyz;

	output.uiMaterialIndex = VSConst.uiMaterialIndex;

	return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)


struct FSOutput
{
    float4 vAlbedo : SV_TARGET0; /* Lit */
};

struct FSInput
{
	float4 vPosition : SV_POSITION;
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    /// xyz is the world space tangent, w the bitangent's handedness
    float4 vTangentWS : TANGENT;
#endif

	/// Vertex position in world space
	float3 vPositionWS : POSITION;

	uint uiMaterialIndex : ATTR0;

	/// False when a double sided material is seen from behind
	bool bIsFrontFace : SV_IsFrontFace;

};

#include "MaterialDef.hlsli"
#include "LightingCommon.hlsli"
#include "ProbeCommon.hlsli"
#include "DecalCommon.hlsli"

F_CBuffer(FSLightBuffer, 4, 1)
{
	Light Lights[LIGHT_COUNT];
};

F_StructBuffer(bMaterialBuffer, Material, 1, 0);

// Forward+ tiled light lists
F_StructBuffer(bLightGrid, TileLightData, 2, 0);
F_StructBuffer(bLightIndexList, uint, 3, 0);

// SH irradiance probes
F_StructBuffer(bProbeBuffer, ProbeSHData, 6, 0);

// Probe volume descriptors for spatial probe blending, one per placed volume
F_StructBuffer(bProbeVolume, ProbeVolume, 7, 0);

// Per-probe depth moments (6x16x16 mean + standard deviation), one atlas strip per probe, for visibility
F_Texture2D(tProbeMoments, 8, 0)

// Clustered decals, the tiles' decal masks are written by the light culling pass
F_StructBuffer(bDecals, Decal, 9, 0);
F_StructBuffer(bDecalMasks, uint, 10, 0);
F_Texture2D(tDecalAtlas, 11, 0)
F_Texture2D(tDecalNormalAtlas, 12, 0)

F_Texture2D(tAlbedo, 0, 1)

#ifdef USE_NORMAL_MAPS
F_Texture2D(tNormalMap, 1, 1)
F_Texture2D(tMetallicRoughness, 2, 1)
#endif

F_ShadowTexture2D(tShadowAtlas, 4, 0);
F_Texture2D(tSSAO, 5, 0);

struct FSPushConsts
{
	float4x4 mViewProjection;
	uint uiObjectIndex;
	uint uiMaterialIndex;
	uint uiTileColumns;
	uint Flags;
	uint2 vTargetSize;
	uint uiBoneBase;
	/// Rows the light grid was dispatched with, paired with uiTileColumns
	uint uiTileRows;
	/// Camera position in world space
	float4 vEyePosition;
};

[[vk::push_constant]] FSPushConsts FSConst;

#define SHADOW_BIAS -0.000005f

/// Taps per axis for the shadow PCF kernel
#define SHADOW_PCF_TAPS 2

/// How much of `light` reaches `position_ws` according to the light's shadow map in the atlas, 1 is fully lit.
/// Lights without a shadow map, and positions outside of it, are fully lit.
float SampleShadowAtlas(Light light, float3 position_ws)
{
	const float4 atlas_rect = light.vShadowAtlasRect;

	if (atlas_rect.x <= 0.0) {
		return 1.0;
	}

	const float4 position_ls = mul(float4(position_ws, 1.0), light.LightCameraMatrix);

	// Behind a spot light
	if (position_ls.w <= 0.0) {
		return 1.0;
	}

	const float3 position_ndc = position_ls.xyz / position_ls.w;

	// The projection flips Y, so NDC +Y is already the bottom of the shadow map
	const float2 shadow_uv = position_ndc.xy * 0.5 + 0.5;

	// I should probably flip the viewport depth range... This is my fault from like a year ago.
	const float shadow_z = 1.0 - position_ndc.z;

	if (any(saturate(shadow_uv) != shadow_uv) || (shadow_z <= 0.0)) {
		return 1.0;
	}

	const float2 texel_size = 1.0 / float2(SHADOW_ATLAS_WIDTH, SHADOW_ATLAS_HEIGHT);

	// Keep every tap inside of this light's region so it never picks up a neighbouring shadow map (bilinear
	// sampling shizzle). The kernel widens the footprint, so the inset has to cover it, not just one texel.
	const float2 inset = texel_size * (0.5 * SHADOW_PCF_TAPS);
	const float2 region_min = atlas_rect.zw + inset;
	const float2 region_max = atlas_rect.zw + atlas_rect.xy - inset;

	const float2 base_uv = shadow_uv * atlas_rect.xy + atlas_rect.zw;
	const float compare_z = shadow_z + SHADOW_BIAS;

#if SHADOW_PCF_TAPS <= 1
	return F_SampleCmpLevelZero(tShadowAtlas, clamp(base_uv, region_min, region_max), compare_z);
#else
	// Taps are spaced a texel apart and centred on base_uv, so the kernel stays symmetric for any tap count
	const float first = -0.5 * (SHADOW_PCF_TAPS - 1);

	float sum = 0.0;

	[unroll]
	for (int y = 0; y < SHADOW_PCF_TAPS; y++) {
		[unroll]
		for (int x = 0; x < SHADOW_PCF_TAPS; x++) {
			const float2 offset = (first + float2(x, y)) * texel_size;
			const float2 tap_uv = clamp(base_uv + offset, region_min, region_max);

			sum += F_SampleCmpLevelZero(tShadowAtlas, tap_uv, compare_z);
		}
	}

	return sum / (SHADOW_PCF_TAPS * SHADOW_PCF_TAPS);
#endif
}


#define MIN_ROUGHNESS 0.212

struct SurfaceParams
{
	/// Diffuse albedo (already scaled down by whatever the specular lobe takes)
	float3 vDiffuse;
	/// Specular reflectance at normal incidence
	float3 vF0;
	/// Perceptual roughness
	float fRoughness;
};

/// Resolves the material's workflow into SurfaceParams. `surface_sample` is the
/// texel from the metallic/roughness slot, or white when no texture is bound.
SurfaceParams GetSurfaceParams(Material material, float3 albedo, float4 surface_sample)
{
	SurfaceParams surface;

	if (HAS_FLAG(material.Flags, MF_SPECULAR_GLOSSINESS)) {
		// KHR_materials_pbrSpecularGlossiness: specular colour in RGB, glossiness in A
		surface.vF0 = surface_sample.rgb * material.vSpecularFactor;
		surface.vDiffuse = albedo * (1.0 - max(surface.vF0.r, max(surface.vF0.g, surface.vF0.b)));
		surface.fRoughness = 1.0 - (surface_sample.a * material.fGlossinessFactor);
	}
	else {
		// glTF metallic/roughness: roughness in G, metallic in B
		const float metallic = surface_sample.b * material.fMetallicFactor;

		surface.vF0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
		surface.vDiffuse = albedo * (1.0 - metallic);
		surface.fRoughness = surface_sample.g * material.fRoughnessFactor;
	}

	surface.fRoughness = clamp(surface.fRoughness, MIN_ROUGHNESS, 1.0);

	return surface;
}


#define DECAL_FADE_COS_START 0.5
#define DECAL_FADE_COS_END 0.2

/// `v` normalized, or zero instead of NaN when it has no length
float3 SafeNormalize(float3 v)
{
	return v * rsqrt(max(dot(v, v), 1e-12));
}

void ApplyDecals(inout SurfaceParams surface, inout float3 shading_normal, TileLightData tile_data, uint tile_index,
				 float3 position_ws, float3 geometric_normal, float3 position_ddx, float3 position_ddy)
{
	for (uint word = tile_data.DecalWordStart; word < tile_data.DecalWordEnd; word++) {
		uint mask = bDecalMasks[(tile_index * DECAL_MASK_WORDS) + word];

		while (mask != 0) {
			const uint decal_index = (word * 32) + firstbitlow(mask);
			mask &= (mask - 1);

			const Decal decal = bDecals[decal_index];

			const float3 position_ds = mul(float4(position_ws, 1.0), decal.mWorldToDecal).xyz;

			if (any(abs(position_ds) > 0.5)) {
				continue;
			}

			const float facing = dot(geometric_normal, -decal.vAxisZ);
			const float angle_fade = saturate((facing - DECAL_FADE_COS_END) / (DECAL_FADE_COS_START - DECAL_FADE_COS_END));

			const float depth_fade = saturate((0.5 - abs(position_ds.z)) * 4.0);

			// Decal space +Y is up, atlas V goes down
			const float2 decal_uv = float2(position_ds.x + 0.5, 0.5 - position_ds.y);
			const float2 atlas_uv = (decal_uv * decal.vAtlasRect.xy) + decal.vAtlasRect.zw;

			const float3x3 world_to_decal = (float3x3)decal.mWorldToDecal;
			const float2 uv_scale = float2(1.0, -1.0) * decal.vAtlasRect.xy;

			const float2 atlas_ddx = mul(position_ddx, world_to_decal).xy * uv_scale;
			const float2 atlas_ddy = mul(position_ddy, world_to_decal).xy * uv_scale;

			const float4 decal_sample = F_SampleGrad(tDecalAtlas, atlas_uv, atlas_ddx, atlas_ddy);
			const float4 tint = F_UnpackUIntToFloat4(decal.uiColor);

			const float alpha = decal_sample.a * tint.a * angle_fade * depth_fade;

			if (alpha <= 0.0) {
				continue;
			}

			surface.vDiffuse = lerp(surface.vDiffuse, decal_sample.rgb * tint.rgb, alpha);
			surface.vF0 = lerp(surface.vF0, float3(0.04, 0.04, 0.04), alpha);
			surface.fRoughness = lerp(surface.fRoughness, decal.fRoughness, alpha * decal.fRoughnessWeight);

			if (decal.fNormalStrength > 0.0) {
				// Tangent space, green up
				const float2 normal_ts = F_SampleGrad(tDecalNormalAtlas, atlas_uv, atlas_ddx, atlas_ddy).xy * 2.0 - 1.0;
				const float2 bend = normal_ts * (decal.fNormalStrength * alpha);

				const float3 decal_right = decal.vAxisX;
				const float3 decal_up = decal.vAxisY;

				const float3 tangent = SafeNormalize(decal_right - shading_normal * dot(decal_right, shading_normal));
				const float3 bitangent = SafeNormalize(decal_up - shading_normal * dot(decal_up, shading_normal) -
													   tangent * dot(decal_up, tangent));

				shading_normal = normalize(shading_normal + (tangent * bend.x) + (bitangent * bend.y));
			}
		}
	}

	surface.fRoughness = clamp(surface.fRoughness, MIN_ROUGHNESS, 1.0);
}


float3 GetSaturationColor(float value)
{
	const float LIMIT = (float)MAX_LIGHTS_PER_TILE;

	float ratio = saturate(value / LIMIT);

	return float3(ratio, 1.0 - ratio, 0.0);
}


#ifdef USE_PREPASS_DEPTH
// Depth tested EQUAL against the prepass without writing or discarding, so hidden fragments are never shaded
[earlydepthstencil]
#endif
FSOutput main(FSInput input)
{
    FSOutput output;

    // Double sided materials are lit from whichever side is seen
    if (!input.bIsFrontFace) {
        input.vNormalWS = -input.vNormalWS;
    }

#ifndef USE_SKINNING
    // Taken before anything can discard, for the decals' texture gradients
    const float3 position_ddx = ddx(input.vPositionWS);
    const float3 position_ddy = ddy(input.vPositionWS);
#endif

    Material material = bMaterialBuffer[input.uiMaterialIndex];

    float4 albedo_sample = F_Sample(tAlbedo, input.vUV);
    float3 albedo = albedo_sample.rgb * material.vBaseColorFactor;
    float tex_alpha = albedo_sample.a;

    float base_alpha = saturate(tex_alpha * material.fAlpha);

#ifndef USE_PREPASS_DEPTH
    // With the prepass, its discard leaves the depth of whatever is behind, which fails the EQUAL test here
    if (base_alpha < ALPHA_CUTOFF) {
        discard;
    }
#endif

    output.vAlbedo = float4(albedo, base_alpha);

    if (HAS_FLAG(material.Flags, MF_UNLIT)) {

	    return output;
    }

#ifdef USE_NORMAL_MAPS
    float4 surface_sample = F_Sample(tMetallicRoughness, input.vUV);
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    const float3 vertex_normal = normalize(input.vNormalWS);
    const float3 tangent =
        SafeNormalize(input.vTangentWS.xyz - (vertex_normal * dot(vertex_normal, input.vTangentWS.xyz)));
    const float3 bitangent = cross(vertex_normal, tangent) * input.vTangentWS.w;

    float3x3 TBN = float3x3(tangent, bitangent, vertex_normal);

    float3 N_final = normalize(mul(normal_ts, TBN));
#else
	// No surface texture in this pipeline; the material factors alone describe the surface.
	float4 surface_sample = float4(1.0, 1.0, 1.0, 1.0);

    float3 N_final = normalize(input.vNormalWS);
#endif

	SurfaceParams surface = GetSurfaceParams(material, albedo, surface_sample);

	uint2 tile_xy = uint2(input.vPosition.xy / LIGHT_TILE_SIZE);

	const uint2 tile_limit = max(uint2(FSConst.uiTileColumns, FSConst.uiTileRows), uint2(1, 1)) - uint2(1, 1);
	tile_xy = min(tile_xy, tile_limit);

	uint tile_index = tile_xy.x + (tile_xy.y * FSConst.uiTileColumns);

	TileLightData tile_data = bLightGrid[tile_index];

#ifndef USE_SKINNING
	// Decals stay where they are in the world, so skinned meshes would slide through them
	if (!HAS_FLAG(FSConst.Flags, DRAW_FLAG_NO_DECALS | DRAW_FLAG_PROBE_CAPTURE)) {
		// ApplyDecals() leaves the shading normal normalized, so N_final stays unit length through this
		ApplyDecals(surface, N_final, tile_data, tile_index, input.vPositionWS, normalize(input.vNormalWS),
					position_ddx, position_ddy);
	}
#endif

	const float roughness = surface.fRoughness;

	/// GGX alpha. The specular lobe is parameterised by the square of the perceptual roughness.
	const float specular_alpha = roughness * roughness;

	float3 accumulated_light = float3(0.0, 0.0, 0.0);

	const float3 N = N_final;
	const float3 V = normalize(FSConst.vEyePosition.xyz - input.vPositionWS);
	const float NdotV = abs(dot(N, V)) + 1e-5f;

	/// The unperturbed surface normal (already flipped for backfaces), for rejecting light the surface faces away from
	const float3 geometric_normal = normalize(input.vNormalWS);

	const float2 ssao_coords = float2(input.vPosition.xy / (float2(FSConst.vTargetSize)));

	// Probe capture bakes have no matching SSAO data.
	float ssao = HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE) ? 1.0 : F_Sample(tSSAO, ssao_coords).r;

#ifdef DEBUG_LIGHT_HEATMAP
	output.vAlbedo = float4(GetSaturationColor((float)tile_data.Count), 1.0);
	return output;
#endif

	for (uint tile_light = 0; tile_light < tile_data.Count; tile_light++) {
		Light light = Lights[bLightIndexList[tile_data.StartIndex + tile_light]];

		float4 light_color = F_UnpackUIntToFloat4(light.uiLightColor);
		float light_intensity = light_color.w * 255.0;

		/// How much light is visible (not occluded) at this pixel
		float visibility = 1.0;
		/// Visibility for the diffuse term only. The specular lobe peaks far above diffuse, so any floor on it shows
		/// up as highlights in shadow.
		float diffuse_visibility_floor = 0.0;

		float3 L;
		float attenuation;

		if (light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL) {
			// Already normalized by LightDirectional::FillGpuData()
			L = light.vLightPosition;
			attenuation = light_intensity;

			visibility = SampleShadowAtlas(light, input.vPositionWS);

			// Let a little of the sun into its own shadows
			diffuse_visibility_floor = 0.05;
		}
		else {
			float3 light_position_local = light.vLightPosition - input.vPositionWS;
			L = normalize(light_position_local);

			float dist_sq = dot(light_position_local, light_position_local);

			attenuation = light_intensity * AttenuationSmooth(dist_sq, light.fInvRadiusSq);

			if (light.uiLightType == FX_LIGHT_TYPE_SPOT) {
				attenuation *= AttenuationSpot(L, light);
				visibility = SampleShadowAtlas(light, input.vPositionWS);
			}
		}

		// A normal map can tilt a texel towards a light that the surface itself faces away from (the top edges of
		// bricks on the back of a sunlit wall). Fade the light out as the geometric surface turns away from it.
		const float horizon = saturate(dot(geometric_normal, L) * 4.0);

		float3 H = normalize(V + L);

		float NdotL = DotC(N, L);
		float NdotH = DotC(N, H);
		float LdotH = DotC(L, H);

		float3 F = F_Schlick(surface.vF0, 1.0, LdotH);
		float3 diffuse_reflectance = surface.vDiffuse;

		// D_GGX() and V_SmithGGXCorrelated() take the GGX alpha, which is the perceptual roughness squared.
		// Fr_FrostbiteDisneyDiffuse() takes the perceptual roughness itself (Frostbite's "linearRoughness").
		float D = D_GGX(NdotH, specular_alpha);
		float Vis = V_SmithGGXCorrelated(NdotV, NdotL, specular_alpha);
		float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

		float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, roughness);

		float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
		float3 specular_term = Fr;

		const float diffuse_visibility = max(visibility * horizon, diffuse_visibility_floor);
		const float specular_visibility = visibility * horizon;

		accumulated_light += attenuation * ((diffuse_visibility * diffuse_term) + (specular_visibility * specular_term)) *
							 light_color.rgb * NdotL;
	}

	float3 ambient = float3(0.0f, 0.0f, 0.0f);

	float3 probe_irradiance = float3(0.0f, 0.0f, 0.0f);
	float probe_visibility = 1.0f;

	if (!HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE)) {
		const uint probe_volume_count = GetProbeVolumeCount(bProbeVolume);

		if (probe_volume_count > 0) {
			const uint volume_index = SelectProbeVolume(input.vPositionWS, bProbeVolume, probe_volume_count);

			// An L2 SH probe is blurry (duh) and a rough surface's lobe is centred nearer the normal than the reflection.
			// Sampling along R regardless is wrong for most instances where roughness sits at or near 1
			const float3 R = GetSpecularDominantDir(N, reflect(-V, N), roughness);

			float3 probe_radiance;
			SampleProbeVolumeIrradiance(input.vPositionWS, N, R, bProbeVolume[volume_index], bProbeBuffer,
										F_TextureName(tProbeMoments), tProbeMoments, probe_irradiance,
										probe_radiance, probe_visibility);

			const float3 probe_specular = probe_radiance * EnvBRDFApprox(surface.vF0, roughness, NdotV);

			ambient = ((probe_irradiance * surface.vDiffuse) + probe_specular) * ssao;
		}
	}

	output.vAlbedo = float4(accumulated_light + ambient, base_alpha);

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE)) {
		const float3 lp_ambient = float3(0.02f, 0.02f, 0.02f) * albedo;
		output.vAlbedo = float4(accumulated_light + lp_ambient, 1.0f);
	}

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_DEBUG_PROBE_IRRADIANCE)) {
		output.vAlbedo = float4(probe_irradiance, 1.0f);
	}

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_DEBUG_PROBE_VISIBILITY)) {
		output.vAlbedo = float4(probe_visibility, probe_visibility, probe_visibility, 1.0f);
	}

    return output;
}
