// Light probe helpers (precomputed GI, MVP).
//
// Mirrors `ProbeData` in Src/Renderer/LightProbe.hpp. SH coefficients are in
// D3D order: Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22.
// Keep the basis in sync with MakeUniformAmbientProbe()/MakeSkyGradientProbe().

struct ProbeData
{
	float4 SH[9];
};

/// Evaluates SH L2 diffuse irradiance for normal `n` (must be normalized).
float3 EvalProbeIrradiance(float3 n, ProbeData probe)
{
	float3 irradiance = probe.SH[0].rgb * 0.282095;
	irradiance += probe.SH[1].rgb * 0.488603 * n.y;
	irradiance += probe.SH[2].rgb * 0.488603 * n.z;
	irradiance += probe.SH[3].rgb * 0.488603 * n.x;
	irradiance += probe.SH[4].rgb * 1.092548 * n.x * n.y;
	irradiance += probe.SH[5].rgb * 1.092548 * n.y * n.z;
	irradiance += probe.SH[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0);
	irradiance += probe.SH[7].rgb * 1.092548 * n.x * n.z;
	irradiance += probe.SH[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y);

	return max(irradiance, float3(0.0, 0.0, 0.0));
}

// Mirrors ProbeVolumeData in Src/Renderer/LightProbe.hpp. Grid index order is
// x-fastest: idx = x + dx * (y + dy * z), matching ComputeGridPlacement().
struct ProbeVolume
{
	float4 vMin;
	float4 vInvCellSize;
	uint4 vDimsAndCount;
};

/// Trilinearly blends the 8 surrounding grid probes (coeffs first, one eval).
/// Requires every grid axis to have at least 2 probes. Outside the volume the
/// edge cell is clamped, degrading gracefully to the nearest probe.
float3 SampleProbeVolume(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeData> probes)
{
	uint3 dims = volume.vDimsAndCount.xyz;

	float3 local =
		clamp((pos_ws - volume.vMin.xyz) * volume.vInvCellSize.xyz, float3(0.0, 0.0, 0.0), float3(dims - uint3(1, 1, 1)));
	uint3 base = min(uint3(floor(local)), dims - uint3(2, 2, 2));
	float3 f = clamp(local - float3(base), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));

	float3 blended[9];
	for (uint k = 0; k < 9; k++) {
		blended[k] = float3(0.0, 0.0, 0.0);
	}

	for (uint cz = 0; cz < 2; cz++) {
		for (uint cy = 0; cy < 2; cy++) {
			for (uint cx = 0; cx < 2; cx++) {
				uint3 cell = base + uint3(cx, cy, cz);
				uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

				float w = (cx ? f.x : (1.0 - f.x)) * (cy ? f.y : (1.0 - f.y)) * (cz ? f.z : (1.0 - f.z));

				for (uint k2 = 0; k2 < 9; k2++) {
					blended[k2] += probes[idx].SH[k2].rgb * w;
				}
			}
		}
	}

	ProbeData probe;
	for (uint k3 = 0; k3 < 9; k3++) {
		probe.SH[k3] = float4(blended[k3], 0.0);
	}

	return EvalProbeIrradiance(n, probe);
}
