#ifndef PROBE_CLIPMAP_FORWARD_HLSLI
#define PROBE_CLIPMAP_FORWARD_HLSLI

#include "FogConsts.hlsli"
#include "ProbeClipmapCommon.hlsli"

/// What a pass that reads the probe clipmap needs. Mirrors ProbeClipmapForwardData in Src/Renderer/ProbeClipmap.cpp
struct ClipmapForwardData
{
	ClipmapScene Clipmap;
	BakedSunShadowParams BakedSun;
	/// Flags (CLIPMAP_FORWARD_FLAG_*)
	uint4 vInfo;
	/// Irradiance over pi where no probe holds a surface. Black unless the map has no SDF pages to trace.
	float4 vFallback;
};

#define CLIPMAP_FORWARD_FLAG_BAKED_SUN 0x01

#endif
