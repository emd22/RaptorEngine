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
#ifdef ALPHA_MASK
    float2 vUV : TEXCOORD0;
#endif
};


struct VSPushConsts
{
    float4x4 mCameraMatrix;
    uint uiObjectIndex;
};


[[vk::push_constant]] VSPushConsts VSConst;

F_StructBuffer(bObjectBuffer, Object, 0, 0);

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mWorld;
    float4x4 MVP = mul(model_matrix, VSConst.mCameraMatrix);
    output.vPosition = mul(float4(input.vPosition, 1.0), MVP);
#ifdef ALPHA_MASK
    output.vUV = input.vUV;
#endif
    return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

#include "MaterialDef.hlsli"

F_StructBuffer(bMaterialBuffer, Material, 1, 0);


#ifdef ALPHA_MASK
// Object local textures
F_Texture2D(tAlbedo, 0, 1)
#endif

struct FSInput
{
#ifdef ALPHA_MASK
    float4 vPosition : SV_POSITION;
    float2 vUV : TEXCOORD0;
#endif
};

void main(FSInput input)
{
#ifdef ALPHA_MASK
    if (F_Sample(tAlbedo, input.vUV).a < ALPHA_CUTOFF) {
        discard;
    }
#endif
}
