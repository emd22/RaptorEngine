#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct ParticleData
{
	float3 vPosition;
	float fSize;
	float3 vStretch;
	uint uiRotationSoftness;
	uint2 uiColor;
	uint2 uiUvRect;
};

struct VSInput
{
	uint uiVertexId : SV_VertexID;
	uint uiInstanceId : SV_InstanceID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
	float4 vColor : COLOR;
	float2 vDepthSoftness : TEXCOORD1;
};

struct PushConsts
{
	float4x4 mViewProjection;
	float4 vCameraRight;
	float4 vCameraUp;
	float3 vEyePosition;
	uint uiInstanceBase;
	float4 vDepthParams;
};

F_StructBuffer(bParticles, ParticleData, 0, 0);

[[vk::push_constant]] PushConsts Const;

static const float2 scCorners[6] = {
	float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
	float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0),
};

float2 UnpackHalf2(uint packed)
{
	return float2(f16tof32(packed), f16tof32(packed >> 16));
}

float2 UnpackUnorm2(uint packed)
{
	return float2(packed & 0xFFFF, packed >> 16) * (1.0 / 65535.0);
}

VSOutput main(VSInput input)
{
	VSOutput output;

	const ParticleData particle = bParticles[input.uiInstanceId + Const.uiInstanceBase];
	const float2 corner = scCorners[input.uiVertexId];
	const float half_size = particle.fSize * 0.5;

	const float2 rotation_softness = UnpackHalf2(particle.uiRotationSoftness);

	float sin_rotation;
	float cos_rotation;
	sincos(rotation_softness.x, sin_rotation, cos_rotation);

	const float2 rotated = float2(corner.x * cos_rotation - corner.y * sin_rotation,
								  corner.x * sin_rotation + corner.y * cos_rotation);

	const float3 billboard = particle.vPosition +
							 (Const.vCameraRight.xyz * rotated.x + Const.vCameraUp.xyz * rotated.y) * half_size;

	const float stretch_length = length(particle.vStretch);
	const float3 axis = particle.vStretch / max(stretch_length, 1e-6);
	const float3 side_unnormalized = cross(axis, Const.vEyePosition - particle.vPosition);
	const float3 side = side_unnormalized * rsqrt(max(dot(side_unnormalized, side_unnormalized), 1e-12));

	const float3 stretched = particle.vPosition - particle.vStretch * 0.5 + side * (corner.x * half_size) +
							 axis * (corner.y * (stretch_length * 0.5 + half_size));

	const bool is_stretched = stretch_length > 1e-4;
	const float3 position = select(is_stretched, stretched, billboard);

	output.vPosition = mul(float4(position, 1.0), Const.mViewProjection);

	const float3 camera_forward = cross(Const.vCameraRight.xyz, Const.vCameraUp.xyz);
	output.vDepthSoftness = float2(dot(position - Const.vEyePosition, camera_forward), rotation_softness.y);

	const float2 uv_offset = UnpackUnorm2(particle.uiUvRect.x);
	const float2 uv_scale = UnpackUnorm2(particle.uiUvRect.y);
	const float2 local_uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);

	output.vUV = uv_offset + local_uv * uv_scale;
	output.vColor = float4(UnpackHalf2(particle.uiColor.x), UnpackHalf2(particle.uiColor.y));

	return output;
}

//////////////////////////////////
// Pixel shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
	float4 vColor : COLOR;
	float2 vDepthSoftness : TEXCOORD1;
};

struct FSOutput
{
	float4 vColor : SV_TARGET0;
};

struct PushConsts
{
	float4x4 mViewProjection;
	float4 vCameraRight;
	float4 vCameraUp;
	float3 vEyePosition;
	uint uiInstanceBase;
	float4 vDepthParams;
};

[[vk::push_constant]] PushConsts Const;

F_Texture2D(tAtlas, 1, 0)
F_Texture2D(tDepth, 2, 0)

FSOutput main(FSInput input)
{
	FSOutput output;

	const float4 texel = F_Sample(tAtlas, input.vUV);

	const float scene_raw = F_SampleLoad(tDepth, int3(input.vPosition.xy, 0)).r;
	const float scene_ndc = 1.0 - scene_raw;
	const float depth_denominator = scene_ndc - Const.vDepthParams.x;
	const float scene_depth = Const.vDepthParams.y /
							  select(abs(depth_denominator) < 1e-12, -1e-12, depth_denominator);

	const float depth_gap = scene_depth - input.vDepthSoftness.x;
	const float hard_fade = saturate(depth_gap * 1000.0);
	const float soft_fade = saturate(depth_gap / max(input.vDepthSoftness.y, 1e-3));
	const float depth_fade = select(Const.vDepthParams.w > 0.5, soft_fade, hard_fade);
	const float raw_fade = select(input.vPosition.z >= scene_raw, 1.0, 0.0);
	const float fade = select(Const.vDepthParams.z > 1.5, raw_fade, select(Const.vDepthParams.z > 0.5, depth_fade, 1.0));

	output.vColor = float4(texel.rgb * texel.a, texel.a) * input.vColor * fade;

	return output;
}
