
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
    uint _Padding0;
    uint2 vTargetSize;
};

#ifdef USE_SKINNING

F_CBuffer(VSUniforms, 3, 1)
{
    BoneMtx bBones[BONE_COUNT];
};

#endif // USE_SKINNING

F_StructBuffer(bObjectBuffer, Object, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 world_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mWorld;

    float4x4 MVP = mul(VSConst.mViewProjection, world_matrix);

#ifdef USE_SKINNING
    float4x4 skin_xform = input.vJointWeights.x * bBones[input.vJointIndices.x]
        + input.vJointWeights.y * bBones[input.vJointIndices.y]
        + input.vJointWeights.z * bBones[input.vJointIndices.z]
        + input.vJointWeights.w * bBones[input.vJointIndices.w];

    output.vPosition = mul(MVP, mul(skin_xform, float4(input.vPosition, 1.0)));
    output.vNormalWS = normalize(mul((float3x3)world_matrix, mul((float3x3)skin_xform, input.vNormal)));
    // output.vDebugColor = input.vJointWeights;
#else
    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vNormalWS = normalize(mul((float3x3)world_matrix, input.vNormal));
    // output.vDebugColor = float4(1.0, 1.0, 1.0, 1.0);
#endif

#ifdef USE_NORMAL_MAPS
    output.vTangentWS = normalize(mul((float3x3)world_matrix, input.vTangent));
    output.vBitangentWS = cross(output.vNormalWS, output.vTangentWS);
#endif

    output.vUV = input.vUV;

    float4 position_ws = mul(world_matrix, float4(input.vPosition, 1.0));
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

F_CBuffer(FSLightBuffer, 4, 1)
{
	Light Lights[LIGHT_COUNT];
};

F_StructBuffer(bMaterialBuffer, Material, 1, 0);

// Forward+ tiled light lists
F_StructBuffer(bLightGrid, TileLightData, 2, 0);
F_StructBuffer(bLightIndexList, uint, 3, 0);

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
	uint _Padding0;
	uint2 vTargetSize;
};

[[vk::push_constant]] FSPushConsts FSConst;

#define ROUGHNESS roughness_metallic.x
#define METALLIC  roughness_metallic.y

#define SHADOW_BIAS 0.00005f


float3 GetSaturationColor(float value)
{
	const float LIMIT = (float)MAX_LIGHTS_PER_TILE;

	float ratio = saturate(value / LIMIT);

	return float3(ratio, 1.0 - ratio, 0.0);
}


FSOutput main(FSInput input)
{
    FSOutput output;

    // Material material_info = bMaterialBuffer[input.uiMaterialIndex];
    // float4 material_color = F_UnpackUIntToFloat4(material_info.uiBaseColor);

    float3 albedo = F_Sample(tAlbedo, input.vUV).rgb;

    output.vAlbedo = float4(albedo, 1.0);

    Material material = bMaterialBuffer[input.uiMaterialIndex];

    if (HAS_FLAG(material.Flags, MF_UNLIT)) {
	    return output;
    }

#ifdef USE_NORMAL_MAPS
    float2 roughness_metallic = F_Sample(tMetallicRoughness, input.vUV).gb;
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

	const float roughness = ROUGHNESS;
	const float metallic = METALLIC;

    // XYZ=Normal, W=Roughness
    float3 N_final = normalize(normal_ws);
    // Metalness
    output.vAlbedo.a = 1.0;
#else
	const float roughness = 0.5;
	const float metallic = 0.5;

    float3 N_final = input.vNormalWS;
    output.vAlbedo.a = 1.0;
#endif

	float4 accumulated_light = float4(0.0, 0.0, 0.0, 0.0);

	// Retrieve the light list for the tile that this pixel belongs to
	uint2 tile_xy = uint2(input.vPosition.xy / LIGHT_TILE_SIZE);
	uint tile_index = tile_xy.x + (tile_xy.y * FSConst.uiTileColumns);

	TileLightData tile_data = bLightGrid[tile_index];

	const float2 ssao_coords = float2(input.vPosition.xy / float2(FSConst.vTargetSize));
	float ssao = F_Sample(tSSAO, ssao_coords);

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

			// Calculate shadows
			float4 shadow_pos_light_space = mul(light.LightCameraMatrix, float4(input.vPositionWS, 1.0));

			float2 shadow_uv;
			shadow_uv.x = 0.5f + (shadow_pos_light_space.x / shadow_pos_light_space.w * 0.5f);
			shadow_uv.y = 0.5f - (shadow_pos_light_space.y / shadow_pos_light_space.w * 0.5f);
			shadow_uv.y = 1.0 - shadow_uv.y;

			float shadow_z = 1.0 - shadow_pos_light_space.z / shadow_pos_light_space.w;

			// Check that the UV values are greater than 0.0 and less than 1.0
			if ((saturate(shadow_uv.x) == shadow_uv.x) && (saturate(shadow_uv.y) == shadow_uv.y) && (shadow_z > 0)) {
				visibility = F_SampleCmpLevelZero(tShadowAtlas, shadow_uv, shadow_z + SHADOW_BIAS);
				visibility = clamp(visibility, 0.05f, 1.0f);
			}
		}
		else {
			float3 light_position_local = light.vLightPosition - input.vPositionWS;
			L = normalize(light_position_local);

			float dist_sq = dot(light_position_local, light_position_local);

			float inv_radius_sq = 1.0 / (light.fLightRadius * light.fLightRadius);
			attenuation = light_intensity * AttenuationSmooth(dist_sq, inv_radius_sq);
		}

		float3 F0 = float3(0.04, 0.04, 0.04);
		F0 = lerp(F0, albedo, metallic);

		float3 N = normalize(N_final);
		float3 V = normalize(light.vEyePosition - input.vPositionWS);
		float3 H = normalize(V + L);

		float NdotL = DotC(N, L);
		float NdotV = abs(dot(N, V)) + 1e-5f;
		float NdotH = DotC(N, H);
		float LdotH = DotC(L, H);

		float3 F = F_Schlick(F0, 1.0, LdotH);
		float3 diffuse_reflectance = albedo * (1.0 - metallic);

		float D = D_GGX(NdotH, roughness);
		float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
		float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

		float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

		float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
		float3 specular_term = Fr;

		accumulated_light += float4(attenuation * ((visibility * diffuse_term) + (visibility * specular_term)) * light_color.rgb * NdotL, material.fAlpha);
	}

	float4 ambient = F_UnpackUIntToFloat4(Lights[0].uiAmbient) * float4(albedo, 1.0f) * (ssao);

	output.vAlbedo = accumulated_light + float4(ambient.rgb, 1.0);

    return output;
}
