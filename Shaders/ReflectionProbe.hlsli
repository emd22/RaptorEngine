#ifndef REFLECTION_PROBE_HLSLI
#define REFLECTION_PROBE_HLSLI

#define REFLECTION_PROBE_MAX_LOD 5.0

/// Most a reflection probe is brightened to match the light where it is used. Dimming is not limited.
#define REFLECTION_NORMALIZE_MAX 1.5

struct ReflectionProbe
{
	float4x4 mWorldToBox;
	float4 vPositionAndCount;
	float4 vFade;
	/// Luminance of the irradiance at the capture point along +X, +Y, +Z, w = 1 when it is known.
	float4 vCaptureIrradiancePositive;
	/// The same along -X, -Y, -Z
	float4 vCaptureIrradianceNegative;
};

/// A cubemap holds the light around its capture point. Somewhere that gets less light than that point (a nook, under a
/// ledge, the dark side of a doorway the box spans) would reflect it too brightly, so the cubemap is scaled by the
/// irradiance at the pixel over the irradiance at the capture point, along the same normal.
float ReflectionProbeNormalization(ReflectionProbe probe, float3 n, float local_luminance)
{
	const float3 side = select(n >= 0.0, probe.vCaptureIrradiancePositive.xyz, probe.vCaptureIrradianceNegative.xyz);
	const float capture = dot(n * n, side);
	const float ratio = min(local_luminance / max(capture, 1e-6), REFLECTION_NORMALIZE_MAX);

	return select(probe.vCaptureIrradiancePositive.w > 0.5, ratio, 1.0);
}

uint GetReflectionProbeCount(StructuredBuffer<ReflectionProbe> probes)
{
	return (uint)probes[0].vPositionAndCount.w;
}

float3 ReflectionBoxProject(float3 pos_ws, float3 dir_ws, float3 pos_bs, float3 dir_bs, float3 probe_pos)
{
	const float3 safe_dir = dir_bs + (step(abs(dir_bs), float3(1e-6, 1e-6, 1e-6)) * 2e-6);
	const float3 inv_dir = 1.0 / safe_dir;

	const float3 t_far = max((1.0 - pos_bs) * inv_dir, (-1.0 - pos_bs) * inv_dir);
	const float t = min(t_far.x, min(t_far.y, t_far.z));

	return (pos_ws + (dir_ws * max(t, 0.0))) - probe_pos;
}

float ReflectionProbeWeight(float3 pos_bs, ReflectionProbe probe)
{
	const float3 edge = ((1.0 - abs(pos_bs)) * probe.vFade.xyz) + 1.0;

	return saturate(min(edge.x, min(edge.y, edge.z)));
}

/// ReflectionProbeNormalization() blended over the probes the way SampleReflectionProbes() blends their cubemaps, 1 where
/// no probe covers the position. Debug view only.
float ReflectionProbeNormalizationBlend(float3 pos_ws, float3 n, float local_luminance,
									   StructuredBuffer<ReflectionProbe> probes)
{
	const uint probe_count = (uint)probes[0].vPositionAndCount.w;

	float normalization = 0.0;
	float remaining = 1.0;

	for (uint i = 0; i < probe_count; i++) {
		const ReflectionProbe probe = probes[i];

		const float3 pos_bs = mul(float4(pos_ws, 1.0), probe.mWorldToBox).xyz;
		const float weight = ReflectionProbeWeight(pos_bs, probe);

		normalization += ReflectionProbeNormalization(probe, n, local_luminance) * (weight * remaining);
		remaining *= 1.0 - weight;
	}

	return normalization + remaining;
}

float3 ReflectionProbeCoverage(float3 pos_ws, StructuredBuffer<ReflectionProbe> probes)
{
	const uint probe_count = (uint)probes[0].vFade.w;

	float3 color = float3(0.0, 0.0, 0.0);
	float remaining = 1.0;

	for (uint i = 0; i < probe_count; i++) {
		const ReflectionProbe probe = probes[i];

		const float3 pos_bs = mul(float4(pos_ws, 1.0), probe.mWorldToBox).xyz;
		const float weight = ReflectionProbeWeight(pos_bs, probe);

		const float3 probe_color = 0.5 + (0.5 * cos(6.2831853 * ((float(i) * 0.618034) + float3(0.0, 0.33, 0.67))));

		color += probe_color * (weight * remaining);
		remaining *= 1.0 - weight;
	}

	return color + (float3(0.03, 0.03, 0.03) * remaining);
}

/// `n` and `local_luminance` are the normal and irradiance luminance the probes are normalized to. With
/// `apply_normalization` off the cubemaps are used as captured.
float3 SampleReflectionProbes(float3 pos_ws, float3 r, float roughness, float3 fallback, uint probe_count,
							  StructuredBuffer<ReflectionProbe> probes, TextureCubeArray cubemaps,
							  SamplerState cubemap_sampler, float3 n, float local_luminance, bool apply_normalization)
{
	const float lod = roughness * REFLECTION_PROBE_MAX_LOD;

	float3 radiance = float3(0.0, 0.0, 0.0);
	float remaining = 1.0;

	for (uint i = 0; i < probe_count; i++) {
		const ReflectionProbe probe = probes[i];

		const float3 pos_bs = mul(float4(pos_ws, 1.0), probe.mWorldToBox).xyz;
		const float weight = ReflectionProbeWeight(pos_bs, probe);

		if (weight <= 0.0) {
			continue;
		}

		const float3 dir_bs = mul(float4(r, 0.0), probe.mWorldToBox).xyz;
		const float3 dir = ReflectionBoxProject(pos_ws, r, pos_bs, dir_bs, probe.vPositionAndCount.xyz);

		const float normalization =
			select(apply_normalization, ReflectionProbeNormalization(probe, n, local_luminance), 1.0);

		radiance += cubemaps.SampleLevel(cubemap_sampler, float4(dir, float(i)), lod).rgb *
					(weight * remaining * normalization);
		remaining *= 1.0 - weight;

		if (remaining < 0.01) {
			break;
		}
	}

	return radiance + (fallback * remaining);
}

#endif
