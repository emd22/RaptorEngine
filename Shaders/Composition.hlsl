
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


struct PSPushConsts
{
	uint2 vFrameExtent;
};

[[vk::push_constant]] PSPushConsts PSConst;

F_Texture2D(tLighting, 2, 0);

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float3 MulRows(float3 row0, float3 row1, float3 row2, float3 v)
{
    return float3(dot(row0, v), dot(row1, v), dot(row2, v));
}

float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;

    return (15.5 * x4 * x2) - (40.14 * x4 * x) + (31.96 * x4) - (6.868 * x2 * x) + (0.4298 * x2) + (0.1191 * x) -
           0.00232;
}

float3 AgxTonemap(float3 color)
{
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;

    color = MulRows(float3(0.6274, 0.3293, 0.0433), float3(0.0691, 0.9195, 0.0113), float3(0.0164, 0.0880, 0.8956),
                    color);

    color = MulRows(float3(0.856627153315983, 0.0951212405381588, 0.0482516061458583),
                    float3(0.137318972929847, 0.761241990602591, 0.101439036467562),
                    float3(0.11189821299995, 0.0767994186031903, 0.811302368396859), color);

    color = log2(max(color, 1e-10));
    color = saturate((color - min_ev) / (max_ev - min_ev));

    color = AgxContrast(color);

    color = MulRows(float3(1.1271005818144368, -0.11060664309660323, -0.016493938717834573),
                    float3(-0.1413297634984383, 1.157823702216272, -0.016493938717834257),
                    float3(-0.14132976349843826, -0.11060664309660294, 1.2519364065950405), color);

    color = pow(max(color, 0.0), 2.2);

    color = MulRows(float3(1.6605, -0.5876, -0.0728), float3(-0.1246, 1.1329, -0.0083),
                    float3(-0.0182, -0.1006, 1.1187), color);

    return saturate(color);
}

FSOutput main(FSInput input)
{
    FSOutput output;

    float4 lighting = F_Sample(tLighting, input.vUV);

PERMIF(USE_AGX);
    output.vColor = float4(AgxTonemap(lighting.rgb), 1.0);
PERMELSE();
    output.vColor = float4(ACESFilm(lighting.rgb), 1.0);
PERMEND();

    return output;
}
