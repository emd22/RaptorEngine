
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
    float3 vTangent : TANGENT;
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
    float3 vTangentWS   : TANGENT;
    float3 vBitangentWS : BITANGENT;
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
    uint uiBoneSlot;
};

#ifdef USE_SKINNING

F_CBuffer(VSUniforms, 3, 1)
{
    BoneMtx bBones[BONE_COUNT * MAX_SKINNED_OBJECTS];
};

#endif // USE_SKINNING

F_StructBuffer(bObjectBuffer, Object, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 world_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mWorld;

    float4x4 MVP = mul(world_matrix, VSConst.mViewProjection);

#ifdef USE_SKINNING
    const uint bone_base = VSConst.uiBoneSlot * BONE_COUNT;

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
    output.vTangentWS = normalize(mul(mul(input.vTangent, (float3x3)skin_xform), (float3x3)world_matrix));
#else
    output.vTangentWS = normalize(mul(input.vTangent, (float3x3)world_matrix));
#endif
    output.vBitangentWS = cross(output.vNormalWS, output.vTangentWS);
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
    float3 vTangentWS   : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif

	/// Vertex position in world space
	float3 vPositionWS : POSITION;

	uint uiMaterialIndex : ATTR0;

};

#include "MaterialDef.hlsli"
#include "LightingCommon.hlsli"
#include "ProbeCommon.hlsli"

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

// Probe volume descriptor for spatial probe blending
F_StructBuffer(bProbeVolume, ProbeVolume, 7, 0);

// Per-probe depth moments cubemaps (6x16x16 mean + mean squared) for visibility
F_StructBuffer(bProbeDepth, ProbeInfo, 8, 0);

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
	uint uiBoneSlot;
	uint _uiPad0;
	/// Camera position in world space
	float4 vEyePosition;
};

[[vk::push_constant]] FSPushConsts FSConst;

#define SHADOW_BIAS -0.000005f

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

	uint atlas_width, atlas_height;
	F_TextureName(tShadowAtlas).GetDimensions(atlas_width, atlas_height);

	// Keep the clip inside of this light's region so it never picks up a neighbouring shadow map (bilinear sampling shizzle)
	const float2 half_texel = 0.5 / float2(atlas_width, atlas_height);
	const float2 region_min = atlas_rect.zw + half_texel;
	const float2 region_max = atlas_rect.zw + atlas_rect.xy - half_texel;

	const float2 atlas_uv = clamp(shadow_uv * atlas_rect.xy + atlas_rect.zw, region_min, region_max);

	return F_SampleCmpLevelZero(tShadowAtlas, atlas_uv, shadow_z + SHADOW_BIAS);
}

/// Lower bound on perceptual roughness. D_GGX() divides by roughness^4 at the
/// highlight peak, so fully glossy texels would otherwise blow up.
#define MIN_ROUGHNESS 0.045

/// Surface response shared by the direct and ambient lighting.
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


float3 GetSaturationColor(float value)
{
	const float LIMIT = (float)MAX_LIGHTS_PER_TILE;

	float ratio = saturate(value / LIMIT);

	return float3(ratio, 1.0 - ratio, 0.0);
}


FSOutput main(FSInput input)
{
    FSOutput output;

    float4 albedo_sample = F_Sample(tAlbedo, input.vUV);
    float3 albedo = albedo_sample.rgb;
    float tex_alpha = albedo_sample.a;

    Material material = bMaterialBuffer[input.uiMaterialIndex];
    float base_alpha = saturate(tex_alpha * material.fAlpha);

    if (base_alpha < ALPHA_CUTOFF) {
        discard;
    }

    output.vAlbedo = float4(albedo, base_alpha);

    if (HAS_FLAG(material.Flags, MF_UNLIT)) {

	    return output;
    }

#ifdef USE_NORMAL_MAPS
    float4 surface_sample = F_Sample(tMetallicRoughness, input.vUV);
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

    float3 N_final = normalize(normal_ws);
#else
	// No surface texture in this pipeline; the material factors alone describe the surface.
	float4 surface_sample = float4(1.0, 1.0, 1.0, 1.0);

    float3 N_final = input.vNormalWS;
#endif

	const SurfaceParams surface = GetSurfaceParams(material, albedo, surface_sample);
	const float roughness = surface.fRoughness;

	float4 accumulated_light = float4(0.0, 0.0, 0.0, 0.0);

	// Retrieve the light list for the tile that this pixel belongs to
	uint2 tile_xy = uint2(input.vPosition.xy / LIGHT_TILE_SIZE);
	uint tile_index = tile_xy.x + (tile_xy.y * FSConst.uiTileColumns);

	TileLightData tile_data = bLightGrid[tile_index];

	const float2 ssao_coords = float2(input.vPosition.xy / (float2(FSConst.vTargetSize)));

	// Probe capture bakes have no matching SSAO data.
	float ssao = HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE) ? 1.0 : F_Sample(tSSAO, ssao_coords);

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

		float3 L;
		float attenuation;

		if (light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL) {
			L = normalize(light.vLightPosition);
			attenuation = light_intensity;

			// Let a little of the sun into its own shadows
			visibility = clamp(SampleShadowAtlas(light, input.vPositionWS), 0.05f, 1.0f);
		}
		else {
			float3 light_position_local = light.vLightPosition - input.vPositionWS;
			L = normalize(light_position_local);

			float dist_sq = dot(light_position_local, light_position_local);

			float inv_radius_sq = 1.0 / (light.fLightRadius * light.fLightRadius);
			attenuation = light_intensity * AttenuationSmooth(dist_sq, inv_radius_sq);

			if (light.uiLightType == FX_LIGHT_TYPE_SPOT) {
				attenuation *= AttenuationSpot(L, light);
				visibility = SampleShadowAtlas(light, input.vPositionWS);
			}
		}

		float3 N = normalize(N_final);
		// The camera this draw is rendered from. The light's own vEyePosition is always the player's camera, which is
		// wrong for probe capture faces.
		float3 V = normalize(FSConst.vEyePosition.xyz - input.vPositionWS);
		float3 H = normalize(V + L);

		float NdotL = DotC(N, L);
		float NdotV = abs(dot(N, V)) + 1e-5f;
		float NdotH = DotC(N, H);
		float LdotH = DotC(L, H);

		float3 F = F_Schlick(surface.vF0, 1.0, LdotH);
		float3 diffuse_reflectance = surface.vDiffuse;

		float D = D_GGX(NdotH, roughness);
		float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
		float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

		float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

		float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
		float3 specular_term = Fr;

		accumulated_light += float4(attenuation * ((visibility * diffuse_term) + (visibility * specular_term)) * light_color.rgb * NdotL, 0.0);
	}

	float4 ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);

	float3 probe_irradiance = float3(0.0f, 0.0f, 0.0f);
	float probe_visibility = 1.0f;

	float3 probe_normal = normalize(N_final);

	if (!HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE)) {
		const ProbeSHData probe_sh = SampleProbeVolumeSH(input.vPositionWS, probe_normal, bProbeVolume[0], bProbeBuffer,
														 bProbeDepth, probe_visibility);

		probe_irradiance = EvalProbeIrradiance(probe_normal, probe_sh);

		const float3 V = normalize(FSConst.vEyePosition.xyz - input.vPositionWS);
		const float3 R = reflect(-V, probe_normal);
		const float NdotV = abs(dot(probe_normal, V)) + 1e-5f;

		const float3 probe_specular = EvalProbeIrradiance(R, probe_sh) * EnvBRDFApprox(surface.vF0, roughness, NdotV);

		ambient = float4(((probe_irradiance * surface.vDiffuse) + probe_specular) * ssao, 1.0f);
	}

	output.vAlbedo = float4(accumulated_light.rgb + ambient.rgb, base_alpha);

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_PROBE_CAPTURE)) {
		const float3 lp_ambient = float3(0.3f, 0.3f, 0.3f) * albedo;
		output.vAlbedo = float4(accumulated_light.rgb + lp_ambient, 1.0f);
	}

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_DEBUG_PROBE_IRRADIANCE)) {
		output.vAlbedo = float4(probe_irradiance, 1.0f);
	}

	if (HAS_FLAG(FSConst.Flags, DRAW_FLAG_DEBUG_PROBE_VISIBILITY)) {
		output.vAlbedo = float4(probe_visibility, probe_visibility, probe_visibility, 1.0f);
	}

    return output;
}
