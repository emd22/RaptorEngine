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
    uint vOut : SV_TARGET0;
};


F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);


FSOutput main(FSInput input)
{
    FSOutput output;

    output.vOut = 0;

    return output;
}
