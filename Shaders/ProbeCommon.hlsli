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
