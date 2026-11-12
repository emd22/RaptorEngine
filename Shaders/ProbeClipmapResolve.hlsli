#ifndef PROBE_CLIPMAP_RESOLVE_HLSLI
#define PROBE_CLIPMAP_RESOLVE_HLSLI

// The half resolution GI resolve and how the forward pass reads it back. Each half resolution texel holds the gather
// of the full resolution pixel at twice its coordinate, so even pixels read their own value and odd ones lie halfway
// between two texels.

/// Distance from the eye where the resolve has nothing (sky, the viewmodel, a pixel it can't place)
#define GI_RESOLVE_NO_SURFACE -1.0

/// Specular as R11G11B10-style floats in the low 31 bits (blue keeps 9), whether the probes light the surface in the
/// top bit
#define GI_RESOLVE_HAS_PROBES 0x80000000u

uint GIResolvePackSpecular(float3 colour, bool has_probes)
{
	const uint3 half_bits = f32tof16(max(colour, 0.0));
	const uint packed = ((half_bits.r >> 4) & 0x7FFu) | (((half_bits.g >> 4) & 0x7FFu) << 11) |
						(((half_bits.b >> 6) & 0x1FFu) << 22);

	return packed | select(has_probes, GI_RESOLVE_HAS_PROBES, 0u);
}

float3 GIResolveUnpackSpecular(uint packed)
{
	return float3(f16tof32((packed & 0x7FFu) << 4), f16tof32(((packed >> 11) & 0x7FFu) << 4),
				  f16tof32(((packed >> 22) & 0x1FFu) << 6));
}

float2 GIResolveEncodeNormal(float3 n)
{
	const float3 d = n / dot(abs(n), 1.0);
	const float2 signs = select(d.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));

	return select(d.z >= 0.0, d.xy, (1.0 - abs(d.yx)) * signs);
}

float3 GIResolveDecodeNormal(float2 e)
{
	const float z = 1.0 - abs(e.x) - abs(e.y);
	const float2 signs = select(e >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
	const float2 xy = select(z >= 0.0, e, (1.0 - abs(e.yx)) * signs);

	return normalize(float3(xy, z));
}

/**
 * The surface's own normal from the positions of the pixel and its neighbours, which the prepass normal (normal mapped)
 * is not. Along each axis it takes the side whose two neighbours line up with the pixel (the smaller error predicting
 * it from them), so at a crease or a silhouette the step is taken along the pixel's own face; the nearer neighbour
 * alone picks the other face about half the time at a crease and gives a normal halfway between the two. Faces
 * `view_direction`. `fallback` where the chosen steps are longer than `max_step` (a pixel on its own).
 */
float3 GIResolveGeometricNormal(float3 centre, float3 left, float3 left2, float3 right, float3 right2, float3 up,
								float3 up2, float3 down, float3 down2, float3 view_direction, float max_step,
								float3 fallback)
{
	const float3 left_error = 2.0 * left - left2 - centre;
	const float3 right_error = 2.0 * right - right2 - centre;
	const float3 up_error = 2.0 * up - up2 - centre;
	const float3 down_error = 2.0 * down - down2 - centre;

	const float3 along_x = select(dot(left_error, left_error) < dot(right_error, right_error), centre - left,
								  right - centre);
	const float3 along_y = select(dot(up_error, up_error) < dot(down_error, down_error), centre - up, down - centre);

	const float3 normal = cross(along_y, along_x);
	const float len = length(normal);
	const float3 unit = normal / max(len, 1e-12);
	const float3 facing = select(dot(unit, view_direction) < 0.0, -unit, unit);

	const float max_sq = max_step * max_step;
	const bool is_usable = and(len > 1e-12, and(dot(along_x, along_x) < max_sq, dot(along_y, along_y) < max_sq));

	return select(is_usable, facing, fallback);
}

/// GIResolveGeometricNormal around `at`, with `fetch(int2 pixel)` giving the world position of a pixel
#define GI_RESOLVE_GEOMETRIC_NORMAL(fetch, centre, at, view_direction, max_step, fallback)                         \
	GIResolveGeometricNormal(centre, fetch(at - int2(1, 0)), fetch(at - int2(2, 0)), fetch(at + int2(1, 0)),        \
							 fetch(at + int2(2, 0)), fetch(at - int2(0, 1)), fetch(at - int2(0, 2)),               \
							 fetch(at + int2(0, 1)), fetch(at + int2(0, 2)), view_direction, max_step, fallback)

#endif
