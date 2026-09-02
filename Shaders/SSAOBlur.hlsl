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
	float vOut : SV_TARGET0;
};

F_Texture2D(tSSAO, 0, 0);
F_Texture2D(tDepth, 1, 0);

struct BlurPushConsts
{
	float2 ScreenSize;
	float2 TexelSize;
	float DepthSharpness;
};

[[vk::push_constant]] BlurPushConsts Consts;

#define BLUR_KERNEL_RADIUS 2

float SampleDepth(float2 uv)
{
	return F_Sample(tDepth, uv).r;
}

FSOutput main(FSInput input)
{
	FSOutput output;

	float center_depth = SampleDepth(input.vUV);
	float center_ao = F_Sample(tSSAO, input.vUV).r;

	float total_ao = center_ao;
	float total_weight = 1.0;

	for (int x = -BLUR_KERNEL_RADIUS; x <= BLUR_KERNEL_RADIUS; ++x)
	{
		for (int y = -BLUR_KERNEL_RADIUS; y <= BLUR_KERNEL_RADIUS; ++y)
		{
			if (x == 0 && y == 0)
			{
				continue;
			}

			float2 offset = float2(x, y) * Consts.TexelSize;
			float2 sample_uv = input.vUV + offset;

			float sample_ao = F_Sample(tSSAO, sample_uv).r;
			float sample_depth = SampleDepth(sample_uv);

			// Bilateral weight: preserve edges by weighting by depth similarity
			float depth_diff = abs(center_depth - sample_depth) * Consts.DepthSharpness;
			float bilateral_weight = exp(-depth_diff * depth_diff);

			// Spatial Gaussian weight (sigma = kernel_radius)
			float r = length(float2(x, y));
			float sigma = float(BLUR_KERNEL_RADIUS) * 0.5;
			float spatial_weight = exp(-(r * r) / (2.0 * sigma * sigma));

			float weight = bilateral_weight * spatial_weight;
			total_ao += sample_ao * weight;
			total_weight += weight;
		}
	}

	output.vOut = total_ao / total_weight;

	return output;
}
