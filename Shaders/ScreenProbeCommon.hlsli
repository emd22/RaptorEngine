#ifndef SCREEN_PROBE_COMMON_HLSLI
#define SCREEN_PROBE_COMMON_HLSLI

// Screen probes, see ScreenProbeTrace.hlsl. Mirrors ProbeClipmap's screen probe constants.

#define SCREEN_PROBE_RAYS 64
/// float4s per probe: position (W 1 where placed), normal (W distance from the eye), then L1 SH per channel
#define SCREEN_PROBE_STRIDE 5
/// How far off its surface a probe's rays start: at least this, and at least this many texels of the SDF page level
/// holding the probe
#define SCREEN_PROBE_SURFACE_BIAS 0.06
#define SCREEN_PROBE_BIAS_TEXELS 2.0
/// A ray end whose clipmap probes weigh less than this before normalizing is taken partly as having left the map, see
/// ScreenProbeTrace
#define SCREEN_PROBE_CACHE_WEIGHT 0.05

#define SCREEN_PROBE_SH_Y0 0.282095
#define SCREEN_PROBE_SH_Y1 0.488603

/// Irradiance / pi along `normal` from one channel's L1 coefficients (c0, c1 x, y, z), cosine convolved
float ScreenProbeIrradiance(float4 sh, float3 normal)
{
	const float a0 = 3.14159265 * SCREEN_PROBE_SH_Y0;
	const float a1 = (2.0 * 3.14159265 / 3.0) * SCREEN_PROBE_SH_Y1;

	return max(a0 * sh.x + a1 * dot(sh.yzw, normal), 0.0) / 3.14159265;
}

#endif
