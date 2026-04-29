
#include "./Helper.hlsl"








#line 94

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
    float4 vNormal : SV_TARGET1;
};

struct FSInput
{
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#line 105
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
    
};



F_Texture2D(tAlbedo, 0)

#line 116
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)



[[vk::binding(0, 1)]] StructuredBuffer<Material> bMaterialBuffer;

FSOutput main(FSInput input)
{
    FSOutput output;

    output.vAlbedo = float4(F_Sample(tAlbedo, input.vUV).rgb, 1.0);

#line 131
    float2 roughness_metallic = F_Sample(tMetallicRoughness, input.vUV).gb;
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

    // XYZ=Normal, W=Roughness
    output.vNormal = float4(normalize(normal_ws), roughness_metallic.x);
    // Metalness
    output.vAlbedo.w = roughness_metallic.y;

    

    return output;
}
