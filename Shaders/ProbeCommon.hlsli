/// Depth-cubemap face resolution. Must match Limits::ProbeDepthSize on the CPU.
#define PROBE_DEPTH_SIZE 16

/// Distance at which a probe texel is treated as "no occluder" (sky / miss).
#define PROBE_DEPTH_MAX_DISTANCE 50.0

#define CHEBYSHEV_BIAS_BASE 0.03
#define CHEBYSHEV_BIAS_SLOPE 0.01

/// Receiver offset along the surface normal, to keep the query point off the
/// surface so coarse depth-cube texels don't self-occlude into edge artifacts.
#define PROBE_VISIBILITY_NORMAL_OFFSET 0.05

/// Penumbra width of the visibility test, as a fraction of the occluder
/// distance (one 16x16 texel spans ~0.1 rad). Flooring the Chebyshev variance
/// at (dist * this) softens the min-depth step enough to hide texel stair-steps.
#define PROBE_VISIBILITY_SOFTNESS 0.1

struct ProbeSHData
{
	float4 SH[9];
};

struct ProbeInfo
{
	float4 vProbePosition;

	// Two moments per texel (nearest-occluder distance and its square), 6 faces.
	float2 Moments[6 * PROBE_DEPTH_SIZE * PROBE_DEPTH_SIZE];
};

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

/// Trilinearly blended L2 irradiance from the 8 surrounding probes.
float3 SampleProbeVolume(float3 pos_ws, float3 n, ProbeVolume volume, StructuredBuffer<ProbeSHData> probes)
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

	for (uint lin_idx = 0; lin_idx < 8; lin_idx++) {
		int3 ridx = int3(lin_idx, lin_idx >> 1, lin_idx >> 2) & int3(1, 1, 1);
		uint3 cell = base + uint3(ridx);

		uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

		float weight = (ridx.x != 0 ? f.x : (1.0 - f.x)) * (ridx.y != 0 ? f.y : (1.0 - f.y)) *
			(ridx.z != 0 ? f.z : (1.0 - f.z));

		for (uint k2 = 0; k2 < 9; k2++) {
			blended[k2] += probes[idx].SH[k2].rgb * weight;
		}
	}

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

	// Soften the min-depth step so coarse texel boundaries don't read as teeth.
	const float soft = mean * PROBE_VISIBILITY_SOFTNESS;
	variance = max(variance, soft * soft);

	float d = receiver_dist - mean;
	float p_max = variance / (variance + d * d);

	// Trim the tail to reduce light bleeding at depth discontinuities.
	return clamp((p_max - 0.05) / 0.95, 0.0, 1.0);
}

/// Bilinear-filtered moments fetch from a single probe face.
/// Moments live in a StructuredBuffer (no HW filtering), so filter manually.
float2 SampleProbeMomentsBilinear(StructuredBuffer<ProbeInfo> probe_infos, uint info_index, uint face, float2 uv01)
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

	float2 m00 = probe_infos[info_index].Moments[face_base + uint(i00.y * PROBE_DEPTH_SIZE + i00.x)];
	float2 m10 = probe_infos[info_index].Moments[face_base + uint(i10.y * PROBE_DEPTH_SIZE + i10.x)];
	float2 m01 = probe_infos[info_index].Moments[face_base + uint(i01.y * PROBE_DEPTH_SIZE + i01.x)];
	float2 m11 = probe_infos[info_index].Moments[face_base + uint(i11.y * PROBE_DEPTH_SIZE + i11.x)];

	float2 m0 = lerp(m00, m10, f.x);
	float2 m1 = lerp(m01, m11, f.x);
	return lerp(m0, m1, f.y);
}

/// Runs the visibility test at a single face UV (one bilinear tap).
float ProbeDepthVisibilityAtUV(StructuredBuffer<ProbeInfo> probe_infos, uint info_index, uint face, float2 uv01,
							   float receiver_dist, float bias)
{
	float2 moments = SampleProbeMomentsBilinear(probe_infos, info_index, face, uv01);

	const float mean = moments.x;
	if (mean >= PROBE_DEPTH_MAX_DISTANCE) {
		return 1.0;
	}

	return ProbeDepthChebyshev(receiver_dist, mean, moments.y, bias);
}

/// Visibility of `pos_ws` as seen from probe `probe_idx` using its depth cubemap.
float SampleProbeVisibility(float3 pos_ws, StructuredBuffer<ProbeInfo> probe_infos, uint info_index)
{
	float3 to_receiver = pos_ws - probe_infos[info_index].vProbePosition.xyz;
	float dist = length(to_receiver);

	if (dist < 1e-4) {
		return 1.0;
	}

	float3 dir = to_receiver / dist;

	uint face;
	float2 uv01;
	ProbeDepthDirectionToFaceUV(dir, face, uv01);

	// Bias grows with distance: a few cm is enough up close but is lost in
	// coarse texels meters away.
	float bias = CHEBYSHEV_BIAS_BASE + dist * CHEBYSHEV_BIAS_SLOPE;

	// PCF over the cube texel grid. The occluder edge in the min-depth cube is
	// quantized to texels, so a single bilinear tap still stair-steps along
	// silhouettes. Averaging the test (not the moments) filters that out while
	// keeping the mean at the nearest occluder, so there's no light leak.
	const float texel = 1.0 / float(PROBE_DEPTH_SIZE);
	const float2 taps[5] = {
		float2(0.0, 0.0), float2(texel, 0.0), float2(-texel, 0.0), float2(0.0, texel), float2(0.0, -texel)
	};

	float visibility = 0.0;
	[unroll]
	for (uint tap = 0; tap < 5; tap++) {
		visibility += ProbeDepthVisibilityAtUV(probe_infos, info_index, face, uv01 + taps[tap], dist, bias);
	}

	return visibility * 0.2;
}

/// Trilinearly blended visibility from the 8 surrounding probes.
/// `n` is the surface normal, used to push the query point off the surface.
float SampleProbeVolumeVisibility(float3 pos_ws, float3 n, StructuredBuffer<ProbeInfo> depths,
								  float3 probe_min, float3 inv_cell, uint3 dims)
{
	float3 vis_pos = pos_ws + n * PROBE_VISIBILITY_NORMAL_OFFSET;

	float3 local = clamp((vis_pos - probe_min) * inv_cell, float3(0.0, 0.0, 0.0), float3(dims - uint3(1, 1, 1)));
	uint3 base = min(uint3(floor(local)), dims - uint3(2, 2, 2));
	float3 f = clamp(local - float3(base), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));

	float visibility = 0.0;

	for (uint lin_idx = 0; lin_idx < 8; lin_idx++) {
		int3 ridx = int3(lin_idx, lin_idx >> 1, lin_idx >> 2) & int3(1, 1, 1);
		uint3 cell = base + uint3(ridx);
		uint idx = cell.x + dims.x * (cell.y + dims.y * cell.z);

		float w = (ridx.x != 0 ? f.x : (1.0 - f.x)) * (ridx.y != 0 ? f.y : (1.0 - f.y)) *
			(ridx.z != 0 ? f.z : (1.0 - f.z));

		visibility += SampleProbeVisibility(vis_pos, depths, idx) * w;
	}

	return visibility;
}
