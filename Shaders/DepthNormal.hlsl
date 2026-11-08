
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

PERMIF(USE_SKINNING);
    uint4 vJointIndices : ATTR0;
    float4 vJointWeights : ATTR1;
PERMEND();

};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS : NORMAL;
    float2 vUV       : TEXCOORD0;

PERMIF(USE_NORMAL_MAPS);
    /// xyz is the world space tangent, w the bitangent's handedness. The bitangent is rebuilt in the pixel
    /// shader rather than interpolated.
    float4 vTangentWS : TANGENT;
PERMEND();

    float3 vPositionWS   : POSITION;

    uint uiMaterialIndex : ATTR0;

    float4 vClipPosition : ATTR1;
    float4 vPrevClipPosition : ATTR2;
};

struct ViewConsts
{
    float4x4 mViewProjection;
    float4 vEyePosition;
    uint2 vTargetSize;
    uint uiTileColumns;
    uint uiTileRows;
    uint Flags;
    float fPreExposure;
    uint2 vPadding;
    float4x4 mPrevViewProjection;
};

struct DrawConsts
{
    uint uiObjectIndex;
    uint uiMaterialIndex;
    uint uiBoneBase;
    uint uiSkinnedDecalStart;
    uint uiSkinnedDecalCount;
};

PERMIF(USE_SKINNING);
F_StructBuffer(bBones, BoneMtx, 3, 1);
PERMEND();

F_StructBuffer(bObjectBuffer, Object, 0, 0);

F_FrameData(ViewConsts, ViewConst, 2);

[[vk::push_constant]] DrawConsts DrawConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 world_matrix = bObjectBuffer[DrawConst.uiObjectIndex + input.uiInstanceId].mWorld;
    float4x4 prev_world_matrix = bObjectBuffer[DrawConst.uiObjectIndex + input.uiInstanceId].mPrevWorld;

PERMIF(USE_SKINNING);
    const uint bone_base = DrawConst.uiBoneBase;

    float4x4 skin_xform = input.vJointWeights.x * bBones[bone_base + input.vJointIndices.x]
        + input.vJointWeights.y * bBones[bone_base + input.vJointIndices.y]
        + input.vJointWeights.z * bBones[bone_base + input.vJointIndices.z]
        + input.vJointWeights.w * bBones[bone_base + input.vJointIndices.w];

    // Posed vertex in model space
    const float4 position_ms = SkinPointExact(float4(input.vPosition, 1.0), bBones[bone_base + input.vJointIndices.x],
                                              bBones[bone_base + input.vJointIndices.y],
                                              bBones[bone_base + input.vJointIndices.z],
                                              bBones[bone_base + input.vJointIndices.w], input.vJointWeights);

    // Exact, so that this and the other depth pass agree to the bit for the forward pass's EQUAL depth test
    output.vPosition = ProjectPointExact(position_ms, world_matrix, ViewConst.mViewProjection);
    output.vNormalWS = normalize(mul(mul(input.vNormal, (float3x3)skin_xform), (float3x3)world_matrix));
PERMELSE();
    float4 position_ms = float4(input.vPosition, 1.0);

    // Exact, so that this and the other depth pass agree to the bit for the forward pass's EQUAL depth test
    output.vPosition = ProjectPointExact(position_ms, world_matrix, ViewConst.mViewProjection);
    output.vNormalWS = normalize(mul(input.vNormal, (float3x3)world_matrix));
PERMEND();

PERMIF(USE_NORMAL_MAPS);
	PERMIF(USE_SKINNING);
	    const float3 tangent_ws = mul(mul(input.vTangent.xyz, (float3x3)skin_xform), (float3x3)world_matrix);
	PERMELSE();
	    const float3 tangent_ws = mul(input.vTangent.xyz, (float3x3)world_matrix);
	PERMEND();
    // The handedness rides along untouched; the pixel shader rebuilds the bitangent from it
    output.vTangentWS = float4(tangent_ws * rsqrt(max(dot(tangent_ws, tangent_ws), 1e-12)), input.vTangent.w);
PERMEND();

    output.vUV = input.vUV;

    float4 position_ws = mul(position_ms, world_matrix);
	output.vPositionWS = position_ws.xyz;

	output.uiMaterialIndex = DrawConst.uiMaterialIndex;

    // Skinned meshes have no previous pose, so only the object and camera motion is known
    output.vClipPosition = output.vPosition;
    output.vPrevClipPosition = mul(mul(position_ms, prev_world_matrix), ViewConst.mPrevViewProjection);

    return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSOutput
{
    /// World space normal in xyz, perceptual roughness in w
    float4 vNormal : SV_TARGET0;
    /// UV offset from this pixel to where the surface was last frame
    float2 vMotion : SV_TARGET1;
};

struct FSInput
{
	float4 vPosition : SV_POSITION;
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

PERMIF(USE_NORMAL_MAPS);
    /// xyz is the world space tangent, w the bitangent's handedness. The bitangent is rebuilt in the pixel
    /// shader rather than interpolated.
    float4 vTangentWS : TANGENT;
PERMEND();

	float3 vPositionWS : POSITION;

	uint uiMaterialIndex : ATTR0;

    float4 vClipPosition : ATTR1;
    float4 vPrevClipPosition : ATTR2;

	/// False when a double sided material is seen from behind
	bool bIsFrontFace : SV_IsFrontFace;
};

#include "MaterialDef.hlsli"

F_StructBuffer(bMaterialBuffer, Material, 1, 0);

// Object local textures
F_Texture2D(tAlbedo, 0, 1)

PERMIF(USE_NORMAL_MAPS);
F_Texture2D(tNormalMap, 1, 1)
F_Texture2D(tORM, 2, 1)
PERMEND();

FSOutput main(FSInput input)
{
    FSOutput output;

    output.vNormal = float4(0.0, 0.0, 0.0, 0.0);

    const float2 uv = (input.vClipPosition.xy / input.vClipPosition.w) * 0.5 + 0.5;
    const float2 prev_uv = (input.vPrevClipPosition.xy / input.vPrevClipPosition.w) * 0.5 + 0.5;
    output.vMotion = prev_uv - uv;

    // Double sided materials are lit from whichever side is seen
    const float facing = select(input.bIsFrontFace, 1.0, -1.0);

    input.vNormalWS *= facing;
PERMIF(USE_NORMAL_MAPS);
    input.vTangentWS *= facing;
PERMEND();

    Material material = bMaterialBuffer[input.uiMaterialIndex];

    {
        float tex_alpha = F_Sample(tAlbedo, input.vUV).a;
        float final_alpha = tex_alpha * material.fAlpha;

        if (final_alpha < GetAlphaCutoff(material)) {
            discard;
        }
    }

    // Ignore normals for unlit objects
PERMNOT(UNLIT);
PERMIF(USE_NORMAL_MAPS);
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    // Re-orthonormalised the same way Forward.hlsl does it, so the prepass normals the SSAO pass reads match
    // what the lighting pass shades with. rsqrt guards a degenerate tangent instead of producing a NaN.
    const float3 vertex_normal = normalize(input.vNormalWS);
    const float3 projected = input.vTangentWS.xyz - (vertex_normal * dot(vertex_normal, input.vTangentWS.xyz));
    const float3 tangent = projected * rsqrt(max(dot(projected, projected), 1e-12));
    const float3 bitangent = cross(vertex_normal, tangent) * input.vTangentWS.w;

    float3x3 TBN = float3x3(tangent, bitangent, vertex_normal);

    // Same as GetSurfaceParams() in Forward.hlsl, before decals
    const float roughness = saturate(F_Sample(tORM, input.vUV).g * material.fRoughnessFactor);

    output.vNormal = float4(normalize(mul(normal_ts, TBN)), roughness);
PERMELSE()
    output.vNormal = float4(input.vNormalWS, saturate(material.fRoughnessFactor));
PERMEND();
PERMEND();

    return output;
}
