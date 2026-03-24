
// #ifndef RENDER_UNLIT
// #define RENDER_UNLIT
// #endif

#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
    int iVertexIndex : SV_VertexID;
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float2 vUV : TEXCOORD0;
};



VSOutput main(VSInput input)
{
    VSOutput output;

    float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);

    output.vUV = out_uv;
    output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

    return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float2 vUV : TEXCOORD0;
};


struct FSOutput
{
    float4 vColor : SV_TARGET0;
};


F_Texture2D(tDepth, 1);
F_Texture2D(tAlbedo, 2);
F_Texture2D(tNormal, 3);
F_Texture2D(tLighting, 4);

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

FSOutput main(FSInput input)
{
    FSOutput output;


#ifdef RENDER_UNLIT
    float4 albedo = F_Sample(tAlbedo, input.vUV);
    output.vColor = float4(albedo.rgb, 1.0);

#else

    float exposure = 1.0;
    float4 lighting = F_Sample(tLighting, input.vUV);

    output.vColor = float4(ACESFilm(lighting.rgb * exposure), 1.0);

#endif
    return output;

}
