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
	// float4 vOut : SV_TARGET0;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_DataTexture2D(tNoise, uint, 2, 0);

struct SSAOPushConsts
{
	float4x4 InvProjection;
	float4x4 Projection;
	float4x4 View;
	float2 ScreenSize;
	float Radius;
	float Bias;
};

[[vk::push_constant]] SSAOPushConsts Consts;

#define SSAO_KERNEL_SIZE 40
#define SSAO_POWER 2.0
#define SSAO_NOISE_IMAGE_SIZE 64
#define SSAO_DISTANCE_CUTOFF 50.0
#define SSAO_CUTOFF_FADE 10.0
#define SSAO_STRENGTH 2.5
#define PI 3.14159265

#define GOLDEN_ANGLE 2.39996323

float3 ReconstructViewPos(float2 uv, float depth)
{
	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);

	float4 view_space = mul(Consts.InvProjection, ndc);

	return view_space.xyz / view_space.w;
}

float2 GetNoiseVector(float2 uv)
{
	int2 noise_coord = int2(uv * Consts.ScreenSize) % SSAO_NOISE_IMAGE_SIZE;
	uint noise = F_SampleLoad(tNoise, int3(noise_coord, 0));

	float angle = (noise / 4294967295.0) * 2.0 * PI;

	return float2(cos(angle), sin(angle));
}

float3 GetSampleKernel(uint index)
{
	float findex = (index + 0.5) / SSAO_KERNEL_SIZE;

	float z = findex;
	float r = sqrt(max(1.0 - z * z, 0.0));

	float phi = index * GOLDEN_ANGLE;

	return float3(r * cos(phi), r * sin(phi), z);
}

float4 ComputeSSAO(float2 uv)
{
	float raw_depth = F_Sample(tDepth, uv).r;

	// Skip the skybox
	if (raw_depth >= 1.0)
	{
		return float4(1.0, 1.0, 1.0, 1.0);
	}

	float depth = 1.0 - raw_depth;
	float3 fragment_position = ReconstructViewPos(uv, depth);

	float3 world_normal = F_Sample(tNormal, uv).xyz;
	float3 normal = normalize(mul((float3x3)Consts.View, world_normal));

	float2 noise_vector = GetNoiseVector(uv);

	float3 tangent = normalize(cross(normal, float3(noise_vector, 0.0)));
	float3 bitangent = cross(normal, tangent);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	float occlusion = 0.0;

	for (uint i = 0; i < SSAO_KERNEL_SIZE; ++i)
	{
		float3 sample_direction = mul(GetSampleKernel(i), TBN);
		float3 sample_position = fragment_position + sample_direction * Consts.Radius;

		float4 sample_clip = mul(Consts.Projection, float4(sample_position, 1.0));
		float3 sample_ndc = sample_clip.xyz / sample_clip.w;
		float2 sample_uv = sample_ndc.xy * 0.5 + 0.5;

		float sample_raw_depth = F_Sample(tDepth, sample_uv).r;

		// Skip offscreen samples
		if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
		{
			continue;
		}

		float sample_scene_z = ReconstructViewPos(sample_uv, 1.0 - sample_raw_depth).z;

		float delta = sample_position.z - sample_scene_z;

		// Skip samples not occluded or occluded by unrelated foreground geometry (haloing)
		if (delta < Consts.Bias || delta > Consts.Radius)
		{
			continue;
		}

		float attenuation = 1.0 - delta / Consts.Radius;
		occlusion += attenuation;
	}

	float ao = saturate(1.0 - SSAO_STRENGTH * (occlusion / SSAO_KERNEL_SIZE));

	// Fade AO out to 1.0 with distance from the camera
	float cutoff = saturate((SSAO_DISTANCE_CUTOFF - length(fragment_position.xyz)) / SSAO_CUTOFF_FADE);
	ao = pow(lerp(1.0, ao, cutoff), SSAO_POWER);

	return float4(ao, ao, ao, 1.0);
}

FSOutput main(FSInput input)
{
	FSOutput output;

	float4 ao = ComputeSSAO(input.vUV);
	// output.vOut = ao;
	output.vOut = uint(ao.r * 255.0);

	return output;
}
