#ifndef PROBE_CLIPMAP_CONSTS_HLSLI
#define PROBE_CLIPMAP_CONSTS_HLSLI

#include "SDFPageCommon.hlsli"
#include "FogConsts.hlsli"
#include "ProbeClipmapCommon.hlsli"

/// Mirrors ProbeClipmapConsts in Src/Renderer/ProbeClipmap.cpp
struct ProbeClipmapConsts
{
	SDFPageScene SDFPages;
	ClipmapScene Clipmap;
	/// Rotates the fixed ray pattern, a new one every frame
	float4x4 mRayRotation;
	BakedSunShadowParams BakedSun;
	/// Light buffer slots shaded at each hit
	uint4 vLightIndices;
	/// Light count, shading flags (SHADE_FLAG_*)
	uint4 vLights;
	/// Job count, frame, flags (CLIPMAP_FLAG_*)
	uint4 vInfo;
	/// Hysteresis, hysteresis where the light changed a lot, the relative change that counts as a lot, sharpness of
	/// the depth moment lobes
	float4 vBlend;
	/// Radiance of the rays that miss
	float4 vSkyRadiance;
};

#define CLIPMAP_FLAG_BOUNCE 0x01
#define CLIPMAP_FLAG_RELOCATE 0x02

/// Jobs are dispatched as rows of this many groups
#define CLIPMAP_JOB_COLUMNS 1024

/// A job is a slot (low 24 bits), its level (bits 28-29), and whether its tile has no history yet (bit 31)
#define CLIPMAP_JOB_RESET 0x80000000u

uint ClipmapJobSlot(uint job) { return job & 0xFFFFFFu; }
uint ClipmapJobLevel(uint job) { return (job >> 28) & 0x3u; }
bool ClipmapJobIsReset(uint job) { return (job & CLIPMAP_JOB_RESET) != 0; }

/// Ray `ray` of the probe pattern: a Fibonacci sphere, turned by `rotation`
float3 ClipmapRayDirection(uint ray, float4x4 rotation)
{
	const float golden_angle = 2.39996323;
	const float z = 1.0 - (2.0 * float(ray) + 1.0) / float(CLIPMAP_RAYS);
	const float radius = sqrt(saturate(1.0 - z * z));
	const float phi = float(ray) * golden_angle;

	const float3 direction = float3(radius * cos(phi), z, radius * sin(phi));

	return normalize(mul(float4(direction, 0.0), rotation).xyz);
}

#endif
