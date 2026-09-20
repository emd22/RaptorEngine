///
/// Light-probe sampling shared by the forward passes.
///

// These mirror Src/Renderer/Limits.hpp. There is no compile-time link, so they have to be changed in both places.

#define PROBE_SH_COEFF_COUNT 9

/// Probe volumes that can be placed at once. Mirrors Limits::MaxProbeVolumes.
#define PROBE_MAX_VOLUMES 8

/// Each probe has a depth moments cubemap: per texel, the mean distance and mean squared distance to what it sees
#define PROBE_DEPTH_SIZE 16
#define PROBE_DEPTH_FACES 6
#define PROBE_DEPTH_TEXELS_PER_FACE (PROBE_DEPTH_SIZE * PROBE_DEPTH_SIZE)

/// Distance stored by the bake where nothing was hit
#define PROBE_DEPTH_MAX_DISTANCE 50.0

// Tuning

/// Bias (metres) subtracted from the receiver distance before the Chebyshev test
#define PROBE_CHEBYSHEV_BIAS 0.02
/// Lower bound (metres) on the spread of distances in a depth texel. Softens the edges of what a probe can see.
#define PROBE_MIN_DEPTH_STDDEV 0.5

/// Surfaces are sampled this fraction of the smallest probe spacing off the surface, clamped to MIN..MAX metres
#define PROBE_NORMAL_BIAS_SCALE 0.2
#define PROBE_NORMAL_BIAS_MIN 0.02
#define PROBE_NORMAL_BIAS_MAX 0.5

/// Lowest weights a probe can be given for being occluded or behind the surface, so that no surface is left unlit
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
	/// xyz = world space min corner of the grid. w = how many volumes the buffer holds, repeated in every
	/// entry so that a single descriptor is enough to walk the whole list.
	float4 vMinAndCount;

	float4 vInvCellSize;

	/// Grid dimensions in XYZ, and in W the index this volume's first probe has in the probe buffers
	uint4 vDimsAndFirst;
};

uint GetProbeVolumeCount(StructuredBuffer<ProbeVolume> volumes) { return uint(volumes[0].vMinAndCount.w); }

/// Spacing between this volume's probes along each axis
float3 ProbeVolumeCellSize(ProbeVolume volume) { return 1.0 / max(volume.vInvCellSize.xyz, 1e-6); }

/// World space corner opposite vMin. Probes sit on the boundary, so the grid spans (dims - 1) cells.
float3 ProbeVolumeMax(ProbeVolume volume)
{
	return volume.vMinAndCount.xyz + ProbeVolumeCellSize(volume) * float3(volume.vDimsAndFirst.xyz - 1);
}

/// Squared distance from `pos_ws` to `volume`, 0 when it is inside.
float ProbeVolumeDistanceSq(float3 pos_ws, ProbeVolume volume)
{
	const float3 outside = max(max(volume.vMinAndCount.xyz - pos_ws, pos_ws - ProbeVolumeMax(volume)), 0.0);
	return dot(outside, outside);
}

/// Picks the volume that lights `pos_ws`: the one it is inside, and of those the one with the tightest probe
/// spacing, so a small dense volume wins over the level sized one it sits in. A position outside every volume
/// falls back to the nearest one, whose lookup clamps it to the grid's edge.
///
/// Volumes are not blended into each other, so lighting changes across a volume's boundary. Overlapping a
/// volume well past the geometry it was placed for keeps that step away from anything the player looks at.
uint SelectProbeVolume(float3 pos_ws, StructuredBuffer<ProbeVolume> volumes, uint volume_count)
{
	uint best = 0;
	float best_distance_sq = 1e30;
	float best_cell_volume = 1e30;

	for (uint i = 0; i < volume_count; i++) {
		const float distance_sq = ProbeVolumeDistanceSq(pos_ws, volumes[i]);

		const float3 cell = ProbeVolumeCellSize(volumes[i]);
		const float cell_volume = cell.x * cell.y * cell.z;

		// Closer wins outright; between volumes that both contain the position (both 0) the denser one does
		const bool closer = distance_sq < best_distance_sq - 1e-4;
		const bool denser = distance_sq <= best_distance_sq + 1e-4 && cell_volume < best_cell_volume;

		if (closer || denser) {
			best = i;
			best_distance_sq = distance_sq;
			best_cell_volume = cell_volume;
		}
	}

	return best;
}

/// Evaluates a probe's SH in direction `normal`. The bake stores coefficients convolved with the cosine lobe, so this is
/// the irradiance divided by pi.
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

/// Cubemap face and UV that direction `d` falls in. Must match DirectionToMomentTexel() in Src/Renderer/LightProbe.cpp.
void ProbeDepthDirectionToFaceUV(float3 d, out uint face, out float2 uv01)
{
	const float3 a = abs(d);
	float2 uv;

	if (a.x >= a.y && a.x >= a.z) {
		face = (d.x > 0.0) ? 0 : 1;
		uv = float2((d.x > 0.0) ? -d.z : d.z, -d.y) / a.x;
	}
	else if (a.y >= a.z) {
		face = (d.y > 0.0) ? 2 : 3;
		uv = float2(d.x, (d.y > 0.0) ? d.z : -d.z) / a.y;
	}
	else {
		face = (d.z > 0.0) ? 4 : 5;
		uv = float2((d.z > 0.0) ? d.x : -d.x, -d.y) / a.z;
	}

	uv01 = uv * 0.5 + 0.5;
}

/// Half the angle covered by a depth texel, as a slope
static const float PROBE_DEPTH_TEXEL_SLOPE = tan(radians(45.0 / PROBE_DEPTH_SIZE));

float ProbeMinVariance(float mean)
{
	// A texel covers more area the further away it is
	const float stddev = max(mean * PROBE_DEPTH_TEXEL_SLOPE, PROBE_MIN_DEPTH_STDDEV);
	return stddev * stddev;
}

/// Chebyshev visibility from depth moments: 1 = visible, 0 = occluded.
float ProbeDepthChebyshev(float receiver_dist, float mean, float mean_sq)
{
	const float biased_dist = max(receiver_dist - PROBE_CHEBYSHEV_BIAS, 0.0);

	// In front of the mean occluder distance: fully visible.
	if (biased_dist <= mean) {
		return 1.0;
	}

	const float variance = max(mean_sq - mean * mean, ProbeMinVariance(mean));
	const float d = biased_dist - mean;

	return variance / (variance + d * d);
}

/// Bilinear-filtered moments fetch from a single probe face.
float2 SampleProbeMomentsBilinear(StructuredBuffer<ProbeInfo> probe_infos, uint info_index, uint face, float2 uv01)
{
	const float2 st = uv01 * float(PROBE_DEPTH_SIZE) - 0.5;
	const float2 st_floor = floor(st);
	const float2 f = st - st_floor;

	// Clamp at the face's edges rather than filtering across into the neighbouring face
	const uint2 i0 = uint2(clamp(int2(st_floor), 0, PROBE_DEPTH_SIZE - 1));
	const uint2 i1 = uint2(clamp(int2(st_floor) + 1, 0, PROBE_DEPTH_SIZE - 1));

	const uint face_base = face * PROBE_DEPTH_TEXELS_PER_FACE;

	const float2 m00 = probe_infos[info_index].Moments[face_base + i0.y * PROBE_DEPTH_SIZE + i0.x];
	const float2 m10 = probe_infos[info_index].Moments[face_base + i0.y * PROBE_DEPTH_SIZE + i1.x];
	const float2 m01 = probe_infos[info_index].Moments[face_base + i1.y * PROBE_DEPTH_SIZE + i0.x];
	const float2 m11 = probe_infos[info_index].Moments[face_base + i1.y * PROBE_DEPTH_SIZE + i1.x];

	return lerp(lerp(m00, m10, f.x), lerp(m01, m11, f.x), f.y);
}

/// Visibility of `pos_ws` as seen from probe `info_index` using its depth cubemap.
float SampleProbeVisibility(float3 pos_ws, StructuredBuffer<ProbeInfo> probe_infos, uint info_index)
{
	const float3 to_receiver = pos_ws - probe_infos[info_index].vProbePosition.xyz;
	const float raw_dist = length(to_receiver);

	if (raw_dist < 1e-4) {
		return 1.0;
	}

	uint face;
	float2 uv01;
	ProbeDepthDirectionToFaceUV(to_receiver / raw_dist, face, uv01);

	const float2 moments = SampleProbeMomentsBilinear(probe_infos, info_index, face, uv01);

	// Nothing was hit in this direction.
	if (moments.x >= PROBE_DEPTH_MAX_DISTANCE) {
		return 1.0;
	}

	return ProbeDepthChebyshev(min(raw_dist, PROBE_DEPTH_MAX_DISTANCE), moments.x, moments.y);
}

/// Lower weight for probes behind the surface.
float ProbeBackfaceWeight(float3 pos_ws, float3 n, float3 probe_pos)
{
	const float3 to_probe = probe_pos - pos_ws;
	const float len = length(to_probe);

	if (len < 1e-4) {
		return 1.0 + PROBE_BACKFACE_FLOOR;
	}

	const float wrap = (dot(to_probe / len, n) + 1.0) * 0.5;

	return wrap * wrap + PROBE_BACKFACE_FLOOR;
}

/// `pos_ws` moved off the surface along its normal, so that the surface doesn't occlude itself in the visibility test.
float3 ProbeBiasedPosition(float3 pos_ws, float3 n, ProbeVolume volume)
{
	const float3 spacing = 1.0 / max(volume.vInvCellSize.xyz, 1e-6);
	const float min_spacing = min(spacing.x, min(spacing.y, spacing.z));

	return pos_ws + n * clamp(PROBE_NORMAL_BIAS_SCALE * min_spacing, PROBE_NORMAL_BIAS_MIN, PROBE_NORMAL_BIAS_MAX);
}

/// Grid cell + trilinear fractions for a world position.
/// `base` is the low corner of the cell; the 8 taps are `base + (bit0, bit1, bit2)`.
void ProbeVolumeCell(float3 pos_ws, ProbeVolume volume, out uint3 base, out float3 cell_frac)
{
	const uint3 dims = volume.vDimsAndFirst.xyz;

	const float3 local = clamp((pos_ws - volume.vMinAndCount.xyz) * volume.vInvCellSize.xyz, 0.0, float3(dims - 1));

	base = min(uint3(floor(local)), dims - 2);
	cell_frac = saturate(local - float3(base));
}

/// Blends the SH of the 8 probes around `pos_ws` with normal `n`. Each probe is weighted by distance (trilinear), by
/// whether it is in front of the surface, and by whether it can see the surface. `out_visibility` is the trilinear
/// blend of how well the probes can see the surface, for the debug view.
ProbeSHData SampleProbeVolumeSH(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeSHData> probes,
								StructuredBuffer<ProbeInfo> probe_infos, out float out_visibility)
{
	const uint3 dims = volume.vDimsAndFirst.xyz;
	const uint first_probe = volume.vDimsAndFirst.w;

	const float3 biased_pos = ProbeBiasedPosition(pos_ws, n, volume);

	uint3 base;
	float3 cell_frac;
	ProbeVolumeCell(biased_pos, volume, base, cell_frac);

	float3 blended[PROBE_SH_COEFF_COUNT];
	for (uint k = 0; k < PROBE_SH_COEFF_COUNT; k++) {
		blended[k] = float3(0.0, 0.0, 0.0);
	}

	float total_weight = 0.0;
	out_visibility = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		const uint3 offset = uint3(corner, corner >> 1, corner >> 2) & uint3(1, 1, 1);
		const uint3 cell = base + offset;
		const uint index = first_probe + cell.x + dims.x * (cell.y + dims.y * cell.z);

		const float3 axis_weights = lerp(1.0 - cell_frac, cell_frac, float3(offset));
		const float trilinear = axis_weights.x * axis_weights.y * axis_weights.z;

		const float visibility = SampleProbeVisibility(biased_pos, probe_infos, index);
		out_visibility += visibility * trilinear;

		const float weight = trilinear * ProbeBackfaceWeight(pos_ws, n, probe_infos[index].vProbePosition.xyz) *
							 max(visibility, PROBE_VISIBILITY_FLOOR);

		for (uint k2 = 0; k2 < PROBE_SH_COEFF_COUNT; k2++) {
			blended[k2] += probes[index].SH[k2].rgb * weight;
		}

		total_weight += weight;
	}

	const float inv_weight = 1.0 / max(total_weight, 1e-6);

	ProbeSHData probe;
	for (uint k3 = 0; k3 < PROBE_SH_COEFF_COUNT; k3++) {
		probe.SH[k3] = float4(blended[k3] * inv_weight, 0.0);
	}

	return probe;
}
