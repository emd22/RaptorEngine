#ifndef PROBE_CLIPMAP_CONSTS_HLSLI
#define PROBE_CLIPMAP_CONSTS_HLSLI

#include "SDFPageCommon.hlsli"
#include "FogConsts.hlsli"
#include "ProbeClipmapCommon.hlsli"

/// Mirrors ProbeClipmap::scMaxProxies
#define CLIPMAP_MAX_PROXIES 32

/// A box of an object tagged DynamicGI. Mirrors ProbeClipmapProxyGpu in Src/Renderer/ProbeClipmap.hpp
struct ClipmapProxy
{
	/// Unit axes, each with W = -dot(axis, centre)
	float4 vAxes[3];
	/// Half extents, then the albedo packed as RGB8
	float4 vExtents;
};

/// Mirrors ProbeClipmapConsts in Src/Renderer/ProbeClipmap.cpp
struct ProbeClipmapConsts
{
	SDFPageScene SDFPages;
	ClipmapScene Clipmap;
	BakedSunShadowParams BakedSun;
	/// Light buffer slots shaded at each hit
	uint4 vLightIndices[SHADE_LIGHT_VECTORS];
	/// Light count, shading flags (SHADE_FLAG_*)
	uint4 vLights;
	/// Job count, frame (seeds the ray rotations), flags (CLIPMAP_FLAG_*), proxy count
	uint4 vInfo;
	/// Sharpness of the depth moment lobes times log2(e), the reciprocal of a lobe's expected weight over the rays, how
	/// far a probe's short term light is from its stored light (relative to the larger) where the faster blend starts
	float4 vBlend;
	/// Per level, the hysteresis, raised to how many times less often its probes are refreshed than it is meant for.
	/// W is the hysteresis of a fully dirty probe.
	float4 vSlowHysteresis;
	/// Per level, the hysteresis where the light changed a lot, raised the same way
	float4 vFastHysteresis;
	/// Radiance of the rays that miss
	float4 vSkyRadiance;
	/// The moving objects' boxes, which the SDF pages don't hold
	ClipmapProxy Proxies[CLIPMAP_MAX_PROXIES];
};

struct ClipmapProxyHit
{
	float fDistance;
	/// Outward normal of the face the ray reached. A ray that started inside the box reaches the face from behind.
	float3 vNormal;
	float3 vAlbedo;
	bool bHit;
};

/// Intersects the ray with one box, and keeps the hit if it is closer than the one `hit` has
void ClipmapTraceProxy(ClipmapProxy proxy, float3 origin, float3 direction, inout ClipmapProxyHit hit)
{
	const float3 axis_x = proxy.vAxes[0].xyz;
	const float3 axis_y = proxy.vAxes[1].xyz;
	const float3 axis_z = proxy.vAxes[2].xyz;

	const float3 local_origin = float3(dot(axis_x, origin), dot(axis_y, origin), dot(axis_z, origin)) +
								float3(proxy.vAxes[0].w, proxy.vAxes[1].w, proxy.vAxes[2].w);
	const float3 local_direction = float3(dot(axis_x, direction), dot(axis_y, direction), dot(axis_z, direction));
	const float3 safe_direction = select(abs(local_direction) < 1e-6, select(local_direction < 0.0, -1e-6, 1e-6),
										 local_direction);
	const float3 inv_direction = 1.0 / safe_direction;

	const float3 extents = proxy.vExtents.xyz;
	const float3 t0 = (-extents - local_origin) * inv_direction;
	const float3 t1 = (extents - local_origin) * inv_direction;
	const float3 t_min = min(t0, t1);
	const float3 t_max = max(t0, t1);
	const float t_near = max(max(t_min.x, t_min.y), t_min.z);
	const float t_far = min(min(t_max.x, t_max.y), t_max.z);

	const bool is_inside = t_near < 0.0;
	const float t = select(is_inside, t_far, t_near);
	const bool is_hit = and(t_near <= t_far, and(t_far > 0.0, t < hit.fDistance));

	// The face is on the axis whose slab the ray entered last, or leaves first from inside
	const float3 face_t = select(is_inside, t_max, t_min);
	const bool is_face_x = face_t.x == t;
	const bool is_face_y = and(!is_face_x, face_t.y == t);
	const bool is_face_z = and(!is_face_x, !is_face_y);
	const float3 face_sign = select(is_inside, sign(safe_direction), -sign(safe_direction));
	const float3 local_normal = float3(is_face_x, is_face_y, is_face_z) * face_sign;
	const float3 normal = local_normal.x * axis_x + local_normal.y * axis_y + local_normal.z * axis_z;

	const uint packed = asuint(proxy.vExtents.w);
	const float3 albedo = float3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu) * (1.0 / 255.0);

	hit.fDistance = select(is_hit, t, hit.fDistance);
	hit.vNormal = select(is_hit, normal, hit.vNormal);
	hit.vAlbedo = select(is_hit, albedo, hit.vAlbedo);
	hit.bHit = or(hit.bHit, is_hit);
}

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

#endif
