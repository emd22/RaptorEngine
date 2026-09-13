// #define NO_PROBE_BLENDING 1

// 16 x 16 pixels
#define DEPTH_TEXTURE_SIZE 16

#define CHEBYSHEV_BIAS_BASE 0.02


struct ProbeSHData
{
	float4 SH[9];
};

struct ProbeInfo
{
	float4 vProbePosition;

	// Two moments each, 6 faces, 16x16px texture. stores moments mean and mean squared.
	float2 Moments[6 * DEPTH_TEXTURE_SIZE * DEPTH_TEXTURE_SIZE];
};

#define PROBE_DEPTH_SIZE 16
#define PROBE_DEPTH_MAX_DISTANCE 50.0

/// Spherical Harmonics L2 diffuse irradiance
float3 EvalProbeIrradiance(float3 normal, ProbeSHData probe)
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

float3 SampleProbeVolume(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeSHData> probes, StructuredBuffer<ProbeInfo> probe_infos)
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


	for (uint lin_idx = 0; lin_idx < 8; lin_idx++) {
		int3 ridx = int3(lin_idx, lin_idx >> 1, lin_idx >> 2) & int3(1, 1, 1);
		uint3 cell = base + uint3(ridx);

		uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

		float weight = select(ridx.x, f.x, (1.0 - f.x)) * select(ridx.y, f.y, (1.0 - f.y)) * select(ridx.z, f.z, (1.0 - f.z));

		for (uint k2 = 0; k2 < 9; k2++) {
			blended[k2] += probes[idx].SH[k2].rgb * weight;
		}
	}

	// Trilinearly blend the values between probes
	ProbeSHData probe;

	for (uint k3 = 0; k3 < 9; k3++) {
		probe.SH[k3] = float4(blended[k3], 0.0);
	}

	return EvalProbeIrradiance(n, probe);
}

/// Maps a direction to a depth-cubemap face + 0..1 uv.
/// Must match ProbeDepthDirectionToTexel() in Src/Renderer/LightProbe.cpp.
/// Faces: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z.
void ProbeDepthDirectionToFaceUV(float3 d, out uint face, out float2 uv01)
{
	float ax = abs(d.x);
	float ay = abs(d.y);
	float az = abs(d.z);

	float u = 0.0;
	float v = 0.0;

	if (ax >= ay && ax >= az) {
		if (d.x > 0.0) {
			face = 0;
			u = -d.z / ax;
			v = -d.y / ax;
		} else {
			face = 1;
			u = d.z / ax;
			v = -d.y / ax;
		}
	} else if (ay >= ax && ay >= az) {
		if (d.y > 0.0) {
			face = 2;
			u = d.x / ay;
			v = d.z / ay;
		} else {
			face = 3;
			u = d.x / ay;
			v = -d.z / ay;
		}
	} else {
		if (d.z > 0.0) {
			face = 4;
			u = d.x / az;
			v = -d.y / az;
		} else {
			face = 5;
			u = -d.x / az;
			v = -d.y / az;
		}
	}

	uv01 = float2(u * 0.5 + 0.5, v * 0.5 + 0.5);
}

/// Chebyshev visibility from depth moments: 1 = visible, 0 = occluded.
float ProbeDepthChebyshev(float receiver_dist, float mean, float mean_sq, float bias)
{
	float biased_dist = max(receiver_dist - bias, 0.0);

	if (biased_dist <= mean) {
		return 1.0;
	}

	float variance = max(mean_sq - mean * mean, 1e-4);
	float d = receiver_dist - mean;
	float p_max = variance / (variance + d * d);

	return p_max;
	// Reduce light bleeding on high-variance texels
	// return clamp((p - 0.05) / 0.95, 0.0, 1.0);
}

/// Bilinear-filtered moments fetch from a single probe face.
/// Moments live in a StructuredBuffer (no HW filtering), so filter manually.
/// Out-of-range taps clamp to the face edge.
float2 SampleProbeMomentsBilinear(ProbeInfo probe_info, uint face, float2 uv01)
{
	uint face_base = face * (PROBE_DEPTH_SIZE * PROBE_DEPTH_SIZE);

	float2 st = uv01 * float(PROBE_DEPTH_SIZE) - 0.5;
	float2 base_f = floor(st);
	float2 f = clamp(st - base_f, float2(0.0, 0.0), float2(1.0, 1.0));

	int2 i00 = int2(base_f);
	int2 i10 = i00 + int2(1, 0);
	int2 i01 = i00 + int2(0, 1);
	int2 i11 = i00 + int2(1, 1);

	i00 = clamp(i00, int2(0, 0), int2(PROBE_DEPTH_SIZE - 1, PROBE_DEPTH_SIZE - 1));
	i10 = clamp(i10, int2(0, 0), int2(PROBE_DEPTH_SIZE - 1, PROBE_DEPTH_SIZE - 1));
	i01 = clamp(i01, int2(0, 0), int2(PROBE_DEPTH_SIZE - 1, PROBE_DEPTH_SIZE - 1));
	i11 = clamp(i11, int2(0, 0), int2(PROBE_DEPTH_SIZE - 1, PROBE_DEPTH_SIZE - 1));

	float2 m00 = probe_info.Moments[face_base + uint(i00.y * PROBE_DEPTH_SIZE + i00.x)];
	float2 m10 = probe_info.Moments[face_base + uint(i10.y * PROBE_DEPTH_SIZE + i10.x)];
	float2 m01 = probe_info.Moments[face_base + uint(i01.y * PROBE_DEPTH_SIZE + i01.x)];
	float2 m11 = probe_info.Moments[face_base + uint(i11.y * PROBE_DEPTH_SIZE + i11.x)];

	float2 m0 = lerp(m00, m10, f.x);
	float2 m1 = lerp(m01, m11, f.x);
	return lerp(m0, m1, f.y);
}

/// Visibility of `pos_ws` as seen from probe `probe_idx` using its depth cubemap.
float SampleProbeVisibility(float3 pos_ws, StructuredBuffer<ProbeInfo> probe_infos, uint info_index)
{
	ProbeInfo probe_info = probe_infos[info_index];

	float3 to_receiver = pos_ws - probe_info.vProbePosition.xyz;
	float dist = length(to_receiver);

	if (dist < 1e-4) {
		return 1.0;
	}

	float3 dir = to_receiver / dist;
	// dist = min(dist, PROBE_DEPTH_MAX_DISTANCE);

	uint face;
	float2 uv01;
	ProbeDepthDirectionToFaceUV(dir, face, uv01);

	float2 moments = SampleProbeMomentsBilinear(probe_info, face, uv01);

	float mean = moments.x;
	float mean_sq = moments.y;

	if (mean >= PROBE_DEPTH_MAX_DISTANCE) {
		return 1.0;
	}


	return ProbeDepthChebyshev(dist, mean, mean_sq, CHEBYSHEV_BIAS_BASE);
}

/// Trilinearly blended visibility from the 8 surrounding probes.
float SampleProbeVolumeVisibility(float3 pos_ws, StructuredBuffer<ProbeInfo> depths,
								  float3 probe_min, float3 inv_cell, uint3 dims)
{
	float3 local = clamp((pos_ws - probe_min) * inv_cell, float3(0.0, 0.0, 0.0), float3(dims - uint3(1, 1, 1)));
	uint3 base = min(uint3(floor(local)), dims - uint3(2, 2, 2));
	float3 f = clamp(local - float3(base), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));

	float visibility = 0.0;

	for (uint lin_idx = 0; lin_idx < 8; lin_idx++) {
		int3 ridx = int3(lin_idx, lin_idx >> 1, lin_idx >> 2) & int3(1, 1, 1);
		uint3 cell = base + uint3(ridx);
		uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

		float w = select(ridx.x, f.x, (1.0 - f.x)) * select(ridx.y, f.y, (1.0 - f.y)) * select(ridx.z, f.z, (1.0 - f.z));

		visibility += SampleProbeVisibility(pos_ws, depths, idx) * w;
	}

	return visibility;
}
