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
PERMIF(FINAL_APPLY);
	float vOut : SV_TARGET0;
PERMELSE();
	float2 vOut : SV_TARGET0;
PERMEND();
};

/// R holds the visibility divided by GTAO_OCCLUSION_TERM_SCALE, G the packed edges from SSAO.hlsl
F_Texture2D(tSSAO, 0, 0);

struct BlurConsts
{
	float2 TexelSize;
	float BlurBeta;
};

F_FrameData(BlurConsts, Consts, 1);

#define GTAO_OCCLUSION_TERM_SCALE 1.5

float4 UnpackEdges(float packed_value)
{
	const uint packed = uint(packed_value * 255.5);

	return saturate(float4((packed >> 6) & 3, (packed >> 4) & 3, (packed >> 2) & 3, packed & 3) / 3.0);
}

float2 SampleTerm(float2 uv, float2 offset)
{
	const float2 half_texel = 0.5 * Consts.TexelSize;

	return F_Sample(tSSAO, clamp(uv + (offset * Consts.TexelSize), half_texel, 1.0 - half_texel)).rg;
}

// XeGTAO's edge aware denoise. Every pass but the last blurs harder and carries the edges along.
FSOutput main(FSInput input)
{
	FSOutput output;

PERMIF(FINAL_APPLY);
	const float blur_amount = Consts.BlurBeta;
PERMELSE();
	const float blur_amount = Consts.BlurBeta / 5.0;
PERMEND();

	const float diagonal_weight = 0.85 * 0.5;

	const float2 center = SampleTerm(input.vUV, float2(0.0, 0.0));
	const float2 left = SampleTerm(input.vUV, float2(-1.0, 0.0));
	const float2 right = SampleTerm(input.vUV, float2(1.0, 0.0));
	const float2 top = SampleTerm(input.vUV, float2(0.0, -1.0));
	const float2 bottom = SampleTerm(input.vUV, float2(0.0, 1.0));

	const float top_left = SampleTerm(input.vUV, float2(-1.0, -1.0)).r;
	const float top_right = SampleTerm(input.vUV, float2(1.0, -1.0)).r;
	const float bottom_left = SampleTerm(input.vUV, float2(-1.0, 1.0)).r;
	const float bottom_right = SampleTerm(input.vUV, float2(1.0, 1.0)).r;

	const float4 edges_left = UnpackEdges(left.g);
	const float4 edges_right = UnpackEdges(right.g);
	const float4 edges_top = UnpackEdges(top.g);
	const float4 edges_bottom = UnpackEdges(bottom.g);

	float4 edges_center = UnpackEdges(center.g);
	edges_center *= float4(edges_left.y, edges_right.x, edges_top.w, edges_bottom.z);

	const float leak_threshold = 2.5;
	const float leak_strength = 0.5;
	const float edginess =
		(saturate(4.0 - leak_threshold - dot(edges_center, float4(1.0, 1.0, 1.0, 1.0))) / (4.0 - leak_threshold)) *
		leak_strength;

	edges_center = saturate(edges_center + edginess);

	const float weight_top_left =
		diagonal_weight * ((edges_center.x * edges_left.z) + (edges_center.z * edges_top.x));
	const float weight_top_right =
		diagonal_weight * ((edges_center.z * edges_top.y) + (edges_center.y * edges_right.z));
	const float weight_bottom_left =
		diagonal_weight * ((edges_center.w * edges_bottom.x) + (edges_center.x * edges_left.w));
	const float weight_bottom_right =
		diagonal_weight * ((edges_center.y * edges_right.w) + (edges_center.w * edges_bottom.y));

	float sum_weight = blur_amount;
	float sum = center.r * blur_amount;

	sum += left.r * edges_center.x;
	sum += right.r * edges_center.y;
	sum += top.r * edges_center.z;
	sum += bottom.r * edges_center.w;
	sum += top_left * weight_top_left;
	sum += top_right * weight_top_right;
	sum += bottom_left * weight_bottom_left;
	sum += bottom_right * weight_bottom_right;

	sum_weight += edges_center.x + edges_center.y + edges_center.z + edges_center.w;
	sum_weight += weight_top_left + weight_top_right + weight_bottom_left + weight_bottom_right;

	const float term = sum / sum_weight;

PERMIF(FINAL_APPLY);
	output.vOut = term * GTAO_OCCLUSION_TERM_SCALE;
PERMELSE();
	output.vOut = float2(term, center.g);
PERMEND();

	return output;
}
