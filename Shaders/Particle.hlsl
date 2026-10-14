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
	float fRotation;
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
};

struct VSPushConsts
{
	float4x4 mViewProjection;
	float4 vCameraRight;
	float4 vCameraUp;
	float4 vEyePosition;
};

F_StructBuffer(bParticles, ParticleData, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

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

	const ParticleData particle = bParticles[input.uiInstanceId];
	const float2 corner = scCorners[input.uiVertexId];
	const float half_size = particle.fSize * 0.5;

	float sin_rotation;
	float cos_rotation;
	sincos(particle.fRotation, sin_rotation, cos_rotation);

	const float2 rotated = float2(corner.x * cos_rotation - corner.y * sin_rotation,
								  corner.x * sin_rotation + corner.y * cos_rotation);

	const float3 billboard = particle.vPosition +
							 (VSConst.vCameraRight.xyz * rotated.x + VSConst.vCameraUp.xyz * rotated.y) * half_size;

	const float stretch_length = length(particle.vStretch);
	const float3 axis = particle.vStretch / max(stretch_length, 1e-6);
	const float3 side_unnormalized = cross(axis, VSConst.vEyePosition.xyz - particle.vPosition);
	const float3 side = side_unnormalized * rsqrt(max(dot(side_unnormalized, side_unnormalized), 1e-12));

	const float3 stretched = particle.vPosition - particle.vStretch * 0.5 + side * (corner.x * half_size) +
							 axis * (corner.y * (stretch_length * 0.5 + half_size));

	const bool is_stretched = stretch_length > 1e-4;
	const float3 position = select(is_stretched, stretched, billboard);

	output.vPosition = mul(float4(position, 1.0), VSConst.mViewProjection);

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
};

struct FSOutput
{
	float4 vColor : SV_TARGET0;
};

F_Texture2D(tAtlas, 1, 0)

FSOutput main(FSInput input)
{
	FSOutput output;

	const float4 texel = F_Sample(tAtlas, input.vUV);

	output.vColor = float4(texel.rgb * texel.a, texel.a) * input.vColor;

	return output;
}
