// #define NO_PROBE_BLENDING 1

// Offset along the surface normal when sampling the probe volume. Prevents
// thin geometry from bleeding probes from the opposite side.
#define PROBE_NORMAL_BIAS 0.3
// Runtime scale for probe irradiance. SH L2 without cosine convolution
// reconstructs directional radiance (~2x true diffuse irradiance for
// half-covered cases), so 0.5 keeps energy in line with direct lighting.
#define PROBE_GI_SCALE 0.5

struct ProbeData
{
	float4 SH[9];
};

/// Spherical Harmonics L2 diffuse irradiance
float3 EvalProbeIrradiance(float3 normal, ProbeData probe)
{
	float3 irradiance = probe.SH[0].rgb * 0.282095;

	irradiance += probe.SH[1].rgb * 0.488603 * normal.y;
	irradiance += probe.SH[2].rgb * 0.488603 * normal.z;
	irradiance += probe.SH[3].rgb * 0.488603 * normal.x;
	irradiance += probe.SH[4].rgb * 1.092548 * normal.x * normal.y;
	irradiance += probe.SH[5].rgb * 1.092548 * normal.y * normal.z;
	irradiance += probe.SH[6].rgb * 0.315392 * (3.0 * normal.z * normal.z - 1.0);
	irradiance += probe.SH[7].rgb * 1.092548 * normal.x * normal.z;
	irradiance += probe.SH[8].rgb * 0.546274 * (normal.x * normal.x - normal.y * normal.y);

	return max(irradiance, float3(0.0, 0.0, 0.0));
}

struct ProbeVolume
{
	float4 vMin;
	float4 vInvCellSize;
	uint4 vDimsAndCount;
};

float3 SampleProbeVolume(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeData> probes)
{
	uint3 dims = volume.vDimsAndCount.xyz;

	float3 local =
		clamp((pos_ws - volume.vMin.xyz) * volume.vInvCellSize.xyz, float3(0.0, 0.0, 0.0), float3(dims - uint3(1, 1, 1)));

	uint3 base = min(uint3(floor(local)), dims - uint3(2, 2, 2));
	float3 f = clamp(local - float3(base), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));

	// Zero out the blend values
	float3 blended[9];
	for (uint k = 0; k < 9; k++) {
		blended[k] = float3(0.0, 0.0, 0.0);
	}

#ifdef NO_PROBE_BLENDING
	float best_weight = 0.0f;
#endif

	for (uint lin_idx = 0; lin_idx < 8; lin_idx++) {
		int3 ridx = int3(lin_idx, lin_idx >> 1, lin_idx >> 2) & int3(1, 1, 1);
		uint3 cell = base + uint3(ridx);

		uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

		float w = select(ridx.x, f.x, (1.0 - f.x)) * select(ridx.y, f.y, (1.0 - f.y)) * select(ridx.z, f.z, (1.0 - f.z));

	#ifdef NO_PROBE_BLENDING
		if (w > best_weight) {
			best_weight = w;

			for (uint k2 = 0; k2 < 9; k2++) {
				blended[k2] = probes[idx].SH[k2].rgb * w;
			}
		}
	#else
		for (uint k2 = 0; k2 < 9; k2++) {
			blended[k2] += probes[idx].SH[k2].rgb * w;
		}
	#endif
	}

	// Trilinearly blend the values between probes
	ProbeData probe;

	for (uint k3 = 0; k3 < 9; k3++) {
		probe.SH[k3] = float4(blended[k3], 0.0);
	}

	return EvalProbeIrradiance(n, probe);
}
