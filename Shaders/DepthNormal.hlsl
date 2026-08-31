
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

    float3 vPositionWS   : POSITION;

    uint uiMaterialIndex : ATTR0;
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
    uint uiTileColumns;
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
#else
    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vNormalWS = normalize(mul((float3x3)world_matrix, input.vNormal));
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
    float4 vNormal : SV_TARGET0; /* Prepass normals, world-space */
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

	float3 vPositionWS : POSITION;

	uint uiMaterialIndex : ATTR0;
};

#include "MaterialDef.hlsli"

F_StructBuffer(bMaterialBuffer, Material, 1, 0);

// Object local textures
F_Texture2D(tAlbedo, 0, 1)

#ifdef USE_NORMAL_MAPS
F_Texture2D(tNormalMap, 1, 1)
F_Texture2D(tMetallicRoughness, 2, 1)
#endif

struct FSPushConsts
{
	float4x4 mViewProjection;
	uint uiObjectIndex;
	uint uiMaterialIndex;
	uint uiTileColumns;
};

[[vk::push_constant]] FSPushConsts FSConst;

FSOutput main(FSInput input)
{
    FSOutput output;

    output.vNormal = float4(0.0, 0.0, 0.0, 0.0);

    Material material = bMaterialBuffer[input.uiMaterialIndex];

    // Ignore normals for unlit objects
    if (HAS_FLAG(material.Flags, MF_UNLIT)) {
        return output;
    }

#ifdef USE_NORMAL_MAPS
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);
    output.vNormal = float4(normalize(normal_ws), 0.0);
#else
    output.vNormal = float4(input.vNormalWS, 0.0);
#endif

    return output;
}
