module Composition;

//#ifndef RENDER_UNLIT
//#define RENDER_UNLIT
//#endif

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSIn
{
    int iVertexIndex : SV_VertexID;
};

struct VSOut
{
    float4 vPosition : SV_POSITION;
    float2 vUV : TEXCOORD0;
};


[shader("vertex")]
VSOut VertexMain(VSIn in)
{
    VSOut out;

    float2 out_uv = float2((in.iVertexIndex << 1) & 2, in.iVertexIndex & 2);

    out.vUV = out_uv;
    out.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

    return out;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

struct FSIn
{
    float2 vUV : TEXCOORD0;
};


struct FSOut
{
    float4 vColor : SV_TARGET0;
};

layout(binding = 1) Sampler2D sDepth;
layout(binding = 2) Sampler2D sAlbedo;
layout(binding = 3) Sampler2D sNormal;
layout(binding = 4) Sampler2D sLighting;

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 albedo = sAlbedo.Sample(in.vUV);

#ifdef RENDER_UNLIT
    out.vColor = float4(albedo.rgb, 1.0);

#else

    let exposure: float = 1.0;

    float4 lighting = sLighting.Sample(in.vUV);

    float3 final_color = lerp(albedo.rgb, lighting.rgb, 0.75) * exposure;

    // float3 hdr_recip =  1.0 / (final_color + float3(1.0));

    // let cfGamma : float = 1.0 / 2.2;
    // final_color = pow(final_color, float3(cfGamma));

    out.vColor = float4(ACESFilm(final_color), 1.0);

#endif
    // out.vColor = float4(float3(sNormal.Sample(in.vUV).w), 1.0);

    return out;

}
