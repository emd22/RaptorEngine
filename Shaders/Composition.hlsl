
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
    uint2 vFrameExtent : ATTR0;
};

struct VSPushConsts
{
	uint2 vFrameExtent;
};

[[vk::push_constant]] VSPushConsts VSConst;


VSOutput main(VSInput input)
{
    VSOutput output;

    float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);

    output.vUV = out_uv;
    output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

    output.vFrameExtent = VSConst.vFrameExtent;

    return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float2 vUV : TEXCOORD0;
    uint2 vFrameExtent : ATTR0;
};


struct FSOutput
{
    float4 vColor : SV_TARGET0;
};


F_Texture2D(tDepth, 1, 0);
F_Texture2D(tLighting, 2, 0);
F_Texture2D(tNormal, 3, 0);

// F_DataTexture2D(tSSAO, uint, 4, 0);
F_Texture2D(tSSAO, 4, 0);

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

    float exposure = 1.0;

    int3 ssao_coords = int3(input.vUV.x * input.vFrameExtent.x, input.vUV.y * input.vFrameExtent.y, 0);

    // uint ssao = F_SampleLoad(tSSAO, ssao_coords);
    float4 ssao_debug = F_Sample(tSSAO, input.vUV);
    // float ssao_value = float(ssao) / 255.0;

    exposure = ssao_debug.r;

    float4 lighting = F_Sample(tLighting, input.vUV);

    output.vColor = float4(ACESFilm(lighting.rgb * (exposure)), 1.0);

    // output.vColor = ssao_debug;

    return output;
}
