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

#endif
