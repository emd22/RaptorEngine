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
	/// Hysteresis, hysteresis where the light changed a lot, how far a probe's short term light is from its stored
	/// light (relative to the larger) where the faster blend starts, sharpness of the depth moment lobes
	float4 vBlend;
	/// Per level, the power the hystereses are raised to: how many times less often its probes are refreshed than the
	/// hysteresis is meant for. W is the hysteresis of a fully dirty probe.
	float4 vLevelRate;
	/// Radiance of the rays that miss
	float4 vSkyRadiance;
};

#define CLIPMAP_FLAG_BOUNCE 0x01
#define CLIPMAP_FLAG_RELOCATE 0x02

/// Jobs are dispatched as rows of this many groups
#define CLIPMAP_JOB_COLUMNS 1024

/// A job is a slot (low 24 bits), how many dirty updates the probe has left (bits 24-27), its level (bits 28-29),
/// and whether its tile has no history yet (bit 31)
#define CLIPMAP_JOB_RESET 0x80000000u

/// Mirrors ProbeClipmap::scDirtyUpdates
#define CLIPMAP_DIRTY_UPDATES 8

uint ClipmapJobDirty(uint job) { return (job >> 24) & 0xFu; }

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
