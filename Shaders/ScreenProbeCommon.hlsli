#ifndef SCREEN_PROBE_COMMON_HLSLI
#define SCREEN_PROBE_COMMON_HLSLI

// Screen probes, see ScreenProbeTrace.hlsl. Mirrors ProbeClipmap's screen probe constants.

#define SCREEN_PROBE_RAYS 64
/// float4s per traced probe: position (W 1 where placed), normal, then L1 SH per channel, already cosine convolved and
/// over pi (see ScreenProbeIrradiance)
#define SCREEN_PROBE_STRIDE 5
/// float4s per filtered probe, as the resolve reads them: the probe's plane (unit normal, then dot(normal, position);
/// all zero where it isn't placed), then the SH as halves (see ScreenProbePackSH)
#define SCREEN_PROBE_FILTERED_STRIDE 3
/// How far off its surface a probe's rays start: at least this, and at least this many texels of the SDF page level
/// holding the probe
#define SCREEN_PROBE_SURFACE_BIAS 0.06
#define SCREEN_PROBE_BIAS_TEXELS 2.0
/// A ray end whose clipmap probes weigh less than this before normalizing is taken partly as having left the map, see
/// ScreenProbeTrace
#define SCREEN_PROBE_CACHE_WEIGHT 0.05
/// A screen probe stands for a surface within this share of the surface's distance from the eye of its plane
#define SCREEN_PROBE_PLANE_SHARE 0.02

#define SCREEN_PROBE_SH_Y0 0.282095
#define SCREEN_PROBE_SH_Y1 0.488603

/// What each radiance sample is weighed by into the stored coefficients: the SH basis, times the cosine lobe's band
/// factor (pi for band 0, 2 pi / 3 for band 1), times the basis again for the evaluation, over pi. The read is then
/// one dot product.
#define SCREEN_PROBE_SH_STORE_0 (SCREEN_PROBE_SH_Y0 * SCREEN_PROBE_SH_Y0)
#define SCREEN_PROBE_SH_STORE_1 ((2.0 / 3.0) * SCREEN_PROBE_SH_Y1 * SCREEN_PROBE_SH_Y1)

/// Irradiance / pi along `normal` from one channel's stored coefficients (c0, c1 x, y, z)
float ScreenProbeIrradiance(float4 sh, float3 normal) { return max(sh.x + dot(sh.yzw, normal), 0.0); }

uint2 ScreenProbePackHalf4(float4 value)
{
	const uint4 bits = f32tof16(value);
	return uint2(bits.x | (bits.y << 16), bits.z | (bits.w << 16));
}

float4 ScreenProbeUnpackHalf4(uint2 bits)
{
	return float4(f16tof32(bits.x & 0xFFFFu), f16tof32(bits.x >> 16), f16tof32(bits.y & 0xFFFFu), f16tof32(bits.y >> 16));
}

#endif
