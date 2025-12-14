module Composition;

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

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 albedo = sAlbedo.Sample(in.vUV);

#ifdef RENDER_UNLIT
    out.vColor = float4(albedo.rgb, 1.0);

#else

    float4 lighting = sLighting.Sample(in.vUV);

    float3 lit_color = lighting.rgb;

    float3 hdr_recip =  1.0 / (lit_color + float3(1.0));
    lit_color = lit_color * hdr_recip;

    let cfGamma : float = 1.0 / 2.2;
    lit_color = pow(lit_color, float3(cfGamma));

    out.vColor = float4(lit_color, 1.0);
#endif

    return out;

}
