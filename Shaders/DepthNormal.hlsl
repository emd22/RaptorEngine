
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
    // Unused by this pass, but declared to keep field offsets aligned with the full DrawPushConstants layout so
    // uiBoneSlot below lands on the right bytes.
    uint uiFlags;
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

    {
        float tex_alpha = F_Sample(tAlbedo, input.vUV).a;
        float final_alpha = tex_alpha * material.fAlpha;

        if (final_alpha < ALPHA_CUTOFF) {
            discard;
        }
    }

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
