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
	// uint vOut : SV_TARGET0;
	float4 vOut : SV_TARGET0;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_DataTexture2D(tNoise, uint, 2, 0);

struct SSAOPushConsts
{
	float4x4 InvProjection;
	float4x4 Projection;
	float2 ScreenSize;
	float Radius;
	float Bias;
};

[[vk::push_constant]] SSAOPushConsts Consts;

#define SSAO_KERNEL_SIZE 32
#define SSAO_NOISE_DIM 64
#define PI 3.14159265

#define GOLDEN_ANGLE 2.39996323

float3 ReconstructViewPos(float2 uv, float depth)
{
	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
	float4 view_pos = mul(Consts.InvProjection, ndc);
	return view_pos.xyz / view_pos.w;
}

float3 ReconstructNormal(float2 uv)
{
	float4 normal = F_Sample(tNormal, uv);
	return normal.rgb;

	// float2 texel = 1.0 / Consts.ScreenSize;

	// float3 pos_c = ReconstructViewPos(uv, F_Sample(tDepth, uv).r);
	// float3 pos_r = ReconstructViewPos(uv + float2(texel.x, 0), F_Sample(tDepth, uv + float2(texel.x, 0)).r);
	// float3 pos_u = ReconstructViewPos(uv + float2(0, texel.y), F_Sample(tDepth, uv + float2(0, texel.y)).r);

	// return normalize(cross(pos_u - pos_c, pos_r - pos_c));
}

float2 GetNoiseVector(float2 uv)
{
	int2 coord = int2(uv * Consts.ScreenSize) % SSAO_NOISE_DIM;
	uint noise_packed = F_SampleLoad(tNoise, int3(coord, 0));

	float2 noise;
	noise.x = float(noise_packed & 0xFFFF) / 32768.0 - 1.0;
	noise.y = float((noise_packed >> 16) & 0xFFFF) / 32768.0 - 1.0;
	return noise;
}

float4 ComputeSSAO(float2 uv)
{
	float raw_depth = F_Sample(tDepth, uv).r;
	if (raw_depth <= 0.001)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const float depth = 1.0 - raw_depth;

	float3 pos = ReconstructViewPos(uv, depth);
	float3 normal = ReconstructNormal(uv);

	float2 noise = GetNoiseVector(uv);
	float3 random_vec = float3(noise, 0.0);

	float3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	float occlusion = 0.0;

	for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
	{
		float fi = (float(i) + 0.5) / float(SSAO_KERNEL_SIZE);

		// Cosine-weighted elevation: biases samples toward the normal (z near 1),
		// which matters more for AO than samples near the tangent plane.
		float cos_theta = sqrt(1.0 - fi);
		float sin_theta = sqrt(fi); // since sin^2 = 1 - cos^2 = fi

		// Golden angle azimuth avoids correlating with elevation, giving even spiral coverage
		float phi = GOLDEN_ANGLE * float(i);

		float3 sample_offset;
		sample_offset.x = cos(phi) * sin_theta;
		sample_offset.y = sin(phi) * sin_theta;
		sample_offset.z = cos_theta;

		float dist_scale = lerp(0.1, 1.0, fi * fi);

		float3 sample_dir = mul((float3x3)TBN, sample_offset);
		float3 sample_pos = pos + sample_dir * Consts.Radius * dist_scale;

		float4 clip = mul(Consts.Projection, float4(sample_pos, 1.0));
		float2 sample_uv = (clip.xy / clip.w) * 0.5 + 0.5;

		if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0)
			continue;

		float sample_depth = 1.0 - F_Sample(tDepth, sample_uv).r;
		float3 sample_view = ReconstructViewPos(sample_uv, sample_depth);

		float range_check = smoothstep(0.0, 1.0, Consts.Radius / abs(pos.z - sample_view.z));
		occlusion += (sample_view.z <= sample_pos.z + Consts.Bias ? 1.0 : 0.0) * range_check;
	}

	occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
	return float4(occlusion, occlusion, occlusion, 1.0f);

}

FSOutput main(FSInput input)
{
	FSOutput output;

	float4 ao = ComputeSSAO(input.vUV);
	output.vOut = ao;
	// output.vOut = uint(ao * 255.0);

	return output;
}
