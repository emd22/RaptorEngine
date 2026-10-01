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

PERMIF(ALPHA_MASK);
    float2 vUV : TEXCOORD0;
PERMEND();
};


struct VSPushConsts
{
    float4x4 mCameraMatrix;
    uint uiObjectIndex;
    uint uiBoneBase;
};


[[vk::push_constant]] VSPushConsts VSConst;

F_StructBuffer(bObjectBuffer, Object, 0, 0);

PERMIF(USE_SKINNING);
F_StructBuffer(bBones, BoneMtx, 3, 1);
PERMEND();

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mWorld;
    float4x4 MVP = mul(model_matrix, VSConst.mCameraMatrix);

PERMIF(USE_SKINNING);
    const uint bone_base = VSConst.uiBoneBase;

    float4x4 skin_xform = input.vJointWeights.x * bBones[bone_base + input.vJointIndices.x]
        + input.vJointWeights.y * bBones[bone_base + input.vJointIndices.y]
        + input.vJointWeights.z * bBones[bone_base + input.vJointIndices.z]
        + input.vJointWeights.w * bBones[bone_base + input.vJointIndices.w];

    float4 position_ms = mul(float4(input.vPosition, 1.0), skin_xform);
PERMELSE();
    float4 position_ms = float4(input.vPosition, 1.0);
PERMEND();

    output.vPosition = mul(position_ms, MVP);

PERMIF(ALPHA_MASK);
    output.vUV = input.vUV;
PERMEND();

	return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

#include "MaterialDef.hlsli"

F_StructBuffer(bMaterialBuffer, Material, 1, 0);


PERMIF(ALPHA_MASK);
// Object local textures
F_Texture2D(tAlbedo, 0, 1)
PERMEND();

struct FSInput
{
PERMIF(ALPHA_MASK)
    float4 vPosition : SV_POSITION;
    float2 vUV : TEXCOORD0;
PERMEND();
};

void main(FSInput input)
{
PERMIF(ALPHA_MASK);
    if (F_Sample(tAlbedo, input.vUV).a < ALPHA_CUTOFF) {
        discard;
    }
PERMEND();
}
