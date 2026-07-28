#include "./Helper.hlsl"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
#ifdef FX_LIGHT_DIRECTIONAL
	int iVertexIndex : SV_VertexID;
#else
	float3 vPosition : ATTR0;
#endif
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	uint uiLightIndex : ATTR0;
};


struct VSPushConsts
{
	float4x4 CameraMatrix;
	uint uiObjectIndex;
	uint uiLightIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

F_StructBuffer(bObjectBuffer, Object, 0, 1);

VSOutput main(VSInput input)
{
	VSOutput output;

#ifdef FX_LIGHT_DIRECTIONAL

	float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);
	output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);
#else
	float4x4 mvp = mul(VSConst.CameraMatrix, bObjectBuffer[VSConst.uiObjectIndex].mModel);
	output.vPosition = mul(mvp, float4(input.vPosition, 1.0));
#endif

	output.uiLightIndex = VSConst.uiLightIndex;

	return output;
}


//////////////////////////////////
// Pixel shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
	float4 vPosition : SV_POSITION;
	uint uiLightIndex : ATTR0;
};

struct FSOutput
{
	float4 vColor : SV_TARGET0;
};

F_Texture2D(tDepth, 0);
F_Texture2D(tAlbedo, 1);
F_Texture2D(tNormal, 2);
F_ShadowTexture2D(tShadow, 3);

#include "LightingCommon.hlsli"

F_CBuffer(FSUniforms, 4, 0)
{
	Light Lights[LIGHT_COUNT];
};


float3 WorldPosFromDepth(Light light, float2 uv, float depth)
{
	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);

	float4 world_space = mul(mul(light.mInvView, light.mInvProjection), ndc);

	return world_space.xyz / world_space.w;
}

FSOutput main(FSInput input)
{
	FSOutput output;

	Light light = Lights[input.uiLightIndex];

	float2 screen_uv = input.vPosition.xy / light.vCameraSize;

	float depth = 1.0 - F_Sample(tDepth, screen_uv).r;
	float4 albedo_rgba = F_Sample(tAlbedo, screen_uv);
	float3 albedo = albedo_rgba.rgb;

	float4 normal_rgba = F_Sample(tNormal, screen_uv);
	float3 world_position = WorldPosFromDepth(light, screen_uv, depth);

	float4 light_color = F_UnpackUIntToFloat4(light.uiLightColor);

	float roughness = normal_rgba.w;
	float metallic = albedo_rgba.w;

	float light_intensity = light_color.w * 255.0;

	float3 F0 = float3(0.04, 0.04, 0.04);
	F0 = lerp(F0, albedo, metallic);

	float visibility = 1.0f;

#ifdef FX_LIGHT_DIRECTIONAL
	float4 shadow_pos_light_space = mul(light.LightCameraMatrix, float4(world_position, 1.0));

	float2 shadow_uv;
	shadow_uv.x = 0.5f + (shadow_pos_light_space.x / shadow_pos_light_space.w * 0.5f);
	shadow_uv.y = 0.5f - (shadow_pos_light_space.y / shadow_pos_light_space.w * 0.5f);
	shadow_uv.y = 1.0 - shadow_uv.y;

	float shadow_z = 1.0 - shadow_pos_light_space.z / shadow_pos_light_space.w;

	// Check that the UV values are greater than 0.0 and less than 1.0
	if ((saturate(shadow_uv.x) == shadow_uv.x) && (saturate(shadow_uv.y) == shadow_uv.y) && (shadow_z > 0)) {
		visibility = F_SampleCmpLevelZero(tShadow, shadow_uv, shadow_z + 0.001f);
		visibility = clamp(visibility, 0.05f, 1.0f);
	}

	float3 L = normalize(light.vLightPosition);
#else
	float3 light_position_local = light.vLightPosition - world_position;
	float3 L = normalize(light_position_local);
#endif
	float3 N = normalize(normal_rgba.rgb);
	float3 V = normalize(light.vEyePosition - world_position);
	float3 H = normalize(V + L);

	float NdotL = DotC(N, L);
	float NdotV = abs(dot(N, V)) + 1e-5f;
	float NdotH = DotC(N, H);
	float LdotH = DotC(L, H);

#ifdef FX_LIGHT_DIRECTIONAL
	const float attenuation = light_intensity;
#else
	float dist_sq = dot(light_position_local, light_position_local);
	float light_distance = sqrt(dist_sq);

	float inv_radius_sq = 1.0 / (light.fLightRadius * light.fLightRadius);
	float attenuation = light_intensity * AttenuationSmooth(dist_sq, inv_radius_sq);
#endif

	float3 F = F_Schlick(F0, 1.0, LdotH);
	float3 diffuse_reflectance = albedo * (1.0 - metallic);

	float D = D_GGX(NdotH, roughness);
	float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
	float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

	float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

	float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
	float3 specular_term = Fr;

	float4 ambient = F_UnpackUIntToFloat4(light.uiAmbient) * float4(albedo, 1.0f);

	output.vColor = float4(attenuation * (visibility * diffuse_term + visibility * specular_term) * light_color.rgb * NdotL + ambient.rgb, 1.0);

	return output;
}
