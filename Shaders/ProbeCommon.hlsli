///
/// Light-probe sampling shared by the forward passes.
///

/// SH coefficient count for an L2 irradiance probe (Limits::ProbeSHCoeffCount).
#define PROBE_SH_COEFF_COUNT 9

/// Depth-moments cubemap: PROBE_DEPTH_FACES faces of PROBE_DEPTH_SIZE^2 texels,
/// each texel holding (mean distance, mean distance squared).
#define PROBE_DEPTH_SIZE 16
#define PROBE_DEPTH_FACES 6
#define PROBE_DEPTH_TEXELS_PER_FACE (PROBE_DEPTH_SIZE * PROBE_DEPTH_SIZE)

/// Distance stored by the bake when a ray hit nothing (Limits::ProbeDepthMaxDistance).
#define PROBE_DEPTH_MAX_DISTANCE 50.0

/// Bias (metres) subtracted from the receiver distance before the Chebyshev test.
#define PROBE_CHEBYSHEV_BIAS 0.02


#define PROBE_DEPTH_TEXEL_SLOPE tan(radians(45.0 / PROBE_DEPTH_SIZE))

#define PROBE_NORMAL_BIAS_SCALE 0.2
#define PROBE_NORMAL_BIAS_MIN 0.02
#define PROBE_NORMAL_BIAS_MAX 0.5

#define PROBE_VISIBILITY_FLOOR 0.05
#define PROBE_BACKFACE_FLOOR 0.2

struct ProbeSHData
{
	float4 SH[PROBE_SH_COEFF_COUNT];
};

struct ProbeInfo
{
	/// Baked probe position. Probes get pushed out of geometry during placement,
	/// so this is not necessarily the grid position the trilinear weights assume.
	float4 vProbePosition;

	float2 Moments[PROBE_DEPTH_FACES * PROBE_DEPTH_TEXELS_PER_FACE];
};

struct ProbeVolume
{
	float4 vMin;
	float4 vInvCellSize;
	uint4 vDimsAndCount;
};

/// Spherical Harmonics L2 diffuse irradiance.
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


float ProbeMinVariance(float mean)
{
	float footprint = mean * PROBE_DEPTH_TEXEL_SLOPE;
	float bias_floor = PROBE_NORMAL_BIAS_MAX * 1.0;

	return max(max(footprint * footprint, bias_floor * bias_floor), 1e-4);
}

/// Chebyshev visibility from depth moments: 1 = visible, 0 = occluded.
float ProbeDepthChebyshev(float receiver_dist, float mean, float mean_sq)
{
	float biased_dist = max(receiver_dist - PROBE_CHEBYSHEV_BIAS, 0.0);

	// In front of the mean occluder distance: fully visible.
	if (biased_dist <= mean) {
		return 1.0;
	}

	float variance = max(mean_sq - mean * mean, ProbeMinVariance(mean));
	float d = biased_dist - mean;

	return variance / (variance + d * d);
}

/// Bilinear-filtered moments fetch from a single probe face.
float2 SampleProbeMomentsBilinear(StructuredBuffer<ProbeInfo> probe_infos, uint info_index, uint face, float2 uv01)
{
	uint face_base = face * PROBE_DEPTH_TEXELS_PER_FACE;

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

	float2 m00 = probe_infos[info_index].Moments[face_base + uint(i00.y * PROBE_DEPTH_SIZE + i00.x)];
	float2 m10 = probe_infos[info_index].Moments[face_base + uint(i10.y * PROBE_DEPTH_SIZE + i10.x)];
	float2 m01 = probe_infos[info_index].Moments[face_base + uint(i01.y * PROBE_DEPTH_SIZE + i01.x)];
	float2 m11 = probe_infos[info_index].Moments[face_base + uint(i11.y * PROBE_DEPTH_SIZE + i11.x)];

	float2 m0 = lerp(m00, m10, f.x);
	float2 m1 = lerp(m01, m11, f.x);
	return lerp(m0, m1, f.y);
}

/// Visibility of `pos_ws` as seen from probe `info_index` using its depth cubemap.
float SampleProbeVisibility(float3 pos_ws, StructuredBuffer<ProbeInfo> probe_infos, uint info_index)
{
	float3 to_receiver = pos_ws - probe_infos[info_index].vProbePosition.xyz;
	float raw_dist = length(to_receiver);

	if (raw_dist < 1e-4) {
		return 1.0;
	}

	float3 dir = to_receiver / raw_dist;

	float dist = min(raw_dist, PROBE_DEPTH_MAX_DISTANCE);

	uint face;
	float2 uv01;
	ProbeDepthDirectionToFaceUV(dir, face, uv01);

	float2 moments = SampleProbeMomentsBilinear(probe_infos, info_index, face, uv01);

	float mean = moments.x;
	float mean_sq = moments.y;

	// Nothing was hit in this direction.
	if (mean >= PROBE_DEPTH_MAX_DISTANCE) {
		return 1.0;
	}

	return ProbeDepthChebyshev(dist, mean, mean_sq);
}

float ProbeBackfaceWeight(float3 pos_ws, float3 n, float3 probe_pos)
{
	float3 to_probe = probe_pos - pos_ws;
	float len = length(to_probe);

	if (len < 1e-4) {
		return 1.0 + PROBE_BACKFACE_FLOOR;
	}

	float wrap = (dot(to_probe / len, n) + 1.0) * 0.5;

	return wrap * wrap + PROBE_BACKFACE_FLOOR;
}

float3 ProbeBiasedPosition(float3 pos_ws, float3 n, ProbeVolume volume)
{
	float3 spacing = float3(1.0, 1.0, 1.0) / max(volume.vInvCellSize.xyz, float3(1e-6, 1e-6, 1e-6));
	float min_spacing = min(spacing.x, min(spacing.y, spacing.z));

	float bias = clamp(PROBE_NORMAL_BIAS_SCALE * min_spacing, PROBE_NORMAL_BIAS_MIN, PROBE_NORMAL_BIAS_MAX);

	return pos_ws + n * bias;
}

/// Grid cell + trilinear fractions for a world position.
/// `base` is the low corner of the cell; the 8 taps are `base + (bit0, bit1, bit2)`.
void ProbeVolumeCell(float3 pos_ws, ProbeVolume volume, out uint3 base, out float3 cell_frac)
{
	uint3 dims = volume.vDimsAndCount.xyz;

	float3 local = clamp((pos_ws - volume.vMin.xyz) * volume.vInvCellSize.xyz, float3(0.0, 0.0, 0.0),
						 float3(dims - uint3(1, 1, 1)));

	base = min(uint3(floor(local)), dims - uint3(2, 2, 2));
	cell_frac = clamp(local - float3(base), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

/// Linear probe index of tap `corner` (0..7) around `base`.
uint ProbeVolumeTapIndex(uint3 base, uint corner, uint3 dims, out float3 corner_mask)
{
	uint3 ridx = uint3(corner, corner >> 1, corner >> 2) & uint3(1, 1, 1);
	uint3 cell = base + ridx;

	corner_mask = float3(ridx);

	return cell.x + dims.x * (cell.y + dims.y * cell.z);
}

/// Trilinear weight of a tap, given its corner mask and the cell fractions.
float ProbeVolumeTapWeight(float3 corner_mask, float3 cell_frac)
{
	float3 w = lerp(float3(1.0, 1.0, 1.0) - cell_frac, cell_frac, corner_mask);

	return w.x * w.y * w.z;
}


ProbeSHData SampleProbeVolumeSH(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeSHData> probes,
								StructuredBuffer<ProbeInfo> probe_infos)
{
	uint3 dims = volume.vDimsAndCount.xyz;

	float3 biased_pos = ProbeBiasedPosition(pos_ws, n, volume);

	uint3 base;
	float3 cell_frac;
	ProbeVolumeCell(biased_pos, volume, base, cell_frac);

	float3 blended[PROBE_SH_COEFF_COUNT];
	for (uint k = 0; k < PROBE_SH_COEFF_COUNT; k++) {
		blended[k] = float3(0.0, 0.0, 0.0);
	}

	float total_weight = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		float3 corner_mask;
		uint idx = ProbeVolumeTapIndex(base, corner, dims, corner_mask);

		float weight = ProbeVolumeTapWeight(corner_mask, cell_frac);

		weight *= ProbeBackfaceWeight(pos_ws, n, probe_infos[idx].vProbePosition.xyz);
		weight *= max(SampleProbeVisibility(biased_pos, probe_infos, idx), PROBE_VISIBILITY_FLOOR);

		for (uint k2 = 0; k2 < PROBE_SH_COEFF_COUNT; k2++) {
			blended[k2] += probes[idx].SH[k2].rgb * weight;
		}

		total_weight += weight;
	}

	float inv_weight = 1.0 / max(total_weight, 1e-6);

	ProbeSHData probe;
	for (uint k3 = 0; k3 < PROBE_SH_COEFF_COUNT; k3++) {
		probe.SH[k3] = float4(blended[k3] * inv_weight, 0.0);
	}

	return probe;
}

float3 SampleProbeVolume(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeSHData> probes,
						 StructuredBuffer<ProbeInfo> probe_infos)
{
	return EvalProbeIrradiance(n, SampleProbeVolumeSH(pos_ws, n, volume, probes, probe_infos));
}

float SampleProbeVolumeVisibility(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeInfo> probe_infos)
{
	uint3 dims = volume.vDimsAndCount.xyz;

	float3 biased_pos = ProbeBiasedPosition(pos_ws, n, volume);

	uint3 base;
	float3 cell_frac;
	ProbeVolumeCell(biased_pos, volume, base, cell_frac);

	float visibility = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		float3 corner_mask;
		uint idx = ProbeVolumeTapIndex(base, corner, dims, corner_mask);

		visibility += SampleProbeVisibility(biased_pos, probe_infos, idx) * ProbeVolumeTapWeight(corner_mask, cell_frac);
	}

	return visibility;
}
