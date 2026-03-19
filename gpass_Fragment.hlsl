
#include "./Util.hlsl"


///////////////////////////////////
// Vertex Shader
///////////////////////////////////

#line 64


struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
    float4 vNormal : SV_TARGET1;
};

struct FSInput
{
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
};

//

F_TEXTURE2D(tAlbedo, 0)

#ifdef USE_NORMAL_MAPS
F_TEXTURE2D(tNormalMap, 1)
F_TEXTURE2D(tMetallicRoughness, 2)
#endif


//
[[vk::binding(1, 0)]] StructuredBuffer<Material> bMaterialBuffer;

FSOutput main(FSInput input)
{
    FSOutput output;

    output.vAlbedo = float4(F_SAMPLE(tAlbedo, input.vUV).rgb, 1.0);

#ifdef USE_NORMAL_MAPS
    float2 roughness_metallic = F_SAMPLE(tMetallicRoughness, input.vUV).gb;

    float3 normal_ts = F_SAMPLE(tNormalMap, input.vUV).rgb;
    normal_ts = normal_ts * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

    // Roughness
    output.vNormal = float4(normalize(normal_ws), roughness_metallic.x);

    // Metalness
    output.vAlbedo.w = roughness_metallic.y;
#else
    output.vNormal = float4(input.vNormalWS, 0.5);
#endif

    return output;
}
