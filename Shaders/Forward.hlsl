
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
    float3 vNormalWS   : NORMAL;
    float2 vUV       : TEXCOORD0;
#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
    uint uiMaterialIndex : ATTR0;
    // float4 vDebugColor : ATTR0;
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};

#ifdef USE_SKINNING

F_CBuffer(VSUniforms, 3, 0)
{
    BoneMtx bBones[BONE_COUNT];
};

#endif // USE_SKINNING

F_StructBuffer(bObjectBuffer, Object, 0, 1);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;

    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

#ifdef USE_SKINNING
    float4x4 skin_xform = input.vJointWeights.x * bBones[input.vJointIndices.x]
        + input.vJointWeights.y * bBones[input.vJointIndices.y]
        + input.vJointWeights.z * bBones[input.vJointIndices.z]
        + input.vJointWeights.w * bBones[input.vJointIndices.w];

    output.vPosition = mul(MVP, mul(skin_xform, float4(input.vPosition, 1.0)));
    output.vNormalWS = normalize(mul((float3x3)model_matrix, mul((float3x3)skin_xform, input.vNormal)));
    // output.vDebugColor = input.vJointWeights;
#else
    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vNormalWS = normalize(mul((float3x3)model_matrix, input.vNormal));
    // output.vDebugColor = float4(1.0, 1.0, 1.0, 1.0);
#endif

#ifdef USE_NORMAL_MAPS
    output.vTangentWS = normalize(mul((float3x3)model_matrix, input.vTangent));
    output.vBitangentWS = cross(output.vNormalWS, output.vTangentWS);
#endif

    output.uiMaterialIndex = VSConst.uiMaterialIndex;
    output.vUV = input.vUV;

    return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};

struct FSInput
{
	float4 vPosition : SV_POSITION;
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif

    // float4 vDebugColor : ATTR0;
};

#include "LightingCommon.hlsli"

F_CBuffer(FSLightBuffer, 4, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_Texture2D(tAlbedo, 0)

#ifdef USE_NORMAL_MAPS
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)
#endif

#define ROUGHNESS roughness_metallic.x
#define METALLIC  roughness_metallic.y


FSOutput main(FSInput input)
{
    FSOutput output;

    // Material material_info = bMaterialBuffer[input.uiMaterialIndex];
    // float4 material_color = F_UnpackUIntToFloat4(material_info.uiBaseColor);
    float4 material_color = float4(0.0, 0.0, 0.0, 1.0);

    float3 albedo = F_Sample(tAlbedo, input.vUV).rgb + material_color.rgb;

    output.vAlbedo = float4(F_Sample(tAlbedo, input.vUV).rgb + material_color.rgb, 1.0);

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
    output.vAlbedo.w = roughness_metallic.y;
#else
	const float roughness = 0.0;
	const float metallic = 0.0;

    float3 N_final = input.vNormalWS;
    output.vAlbedo.w = 0.0;
#endif




	Light light = Lights[0];

	float4 light_color = F_UnpackUIntToFloat4(light.uiLightColor);
	float light_intensity = light_color.w * 255.0;

	const float visibility = 1.0;

	float3 F0 = float3(0.04, 0.04, 0.04);
	F0 = lerp(F0, albedo, metallic);

	const float attenuation = light_intensity;


	float3 L = normalize(light.vLightPosition);
	float3 N = normalize(N_final);
	float3 V = normalize(light.vEyePosition - input.vPosition);
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

	float4 ambient = F_UnpackUIntToFloat4(light.uiAmbient) * float4(albedo, 1.0f);

	output.vAlbedo = float4(attenuation * (visibility * diffuse_term + visibility * specular_term) * light_color.rgb * NdotL + ambient.rgb, 1.0);

    return output;
}
