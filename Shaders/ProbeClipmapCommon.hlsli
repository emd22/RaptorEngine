#ifndef PROBE_CLIPMAP_COMMON_HLSLI
#define PROBE_CLIPMAP_COMMON_HLSLI

/// Mirrors ProbeClipmap in Src/Renderer/ProbeClipmap.hpp
#define CLIPMAP_LEVELS 3
#define CLIPMAP_GRID_X 32
#define CLIPMAP_GRID_Y 16
#define CLIPMAP_GRID_Z 32
#define CLIPMAP_PROBES_PER_LEVEL (CLIPMAP_GRID_X * CLIPMAP_GRID_Y * CLIPMAP_GRID_Z)
#define CLIPMAP_RAYS 64

/// False for NaN and infinities. The GI passes keep them out: one kept in a probe or a history texel reaches everything
/// that reads it, for good.
bool GIIsFinite(float3 value)
{
	return and(!any(isnan(value)), !any(isinf(value)));
}

/// Irradiance is 8x8 per probe and depth moments 16x16, on every level. Each tile has a one texel border holding the
/// texels across the octahedral seams.
#define CLIPMAP_IRRADIANCE_SIZE 8
#define CLIPMAP_MOMENTS_SIZE 16
#define CLIPMAP_IRRADIANCE_TILE (CLIPMAP_IRRADIANCE_SIZE + 2)
#define CLIPMAP_MOMENTS_TILE (CLIPMAP_MOMENTS_SIZE + 2)

/// All levels share one irradiance and one moments atlas. A level's tiles are laid out with X and Y side by side across
/// the atlas and Z down it, and the levels are stacked down the atlas.
#define CLIPMAP_TILE_COLUMNS (CLIPMAP_GRID_X * CLIPMAP_GRID_Y)
#define CLIPMAP_TILE_ROWS (CLIPMAP_GRID_Z * CLIPMAP_LEVELS)

/// Probes are moved at most this much of their spacing off their grid point, along each axis
#define CLIPMAP_MAX_OFFSET 0.45
/// How far a probe found on a surface is stepped off it, and past the back face a probe in a wall is moved through,
/// in spacings but at most CLIPMAP_MIN_FRONTFACE_METRES, like DDGI's distance in world units
#define CLIPMAP_MIN_FRONTFACE 0.25
#define CLIPMAP_MIN_FRONTFACE_METRES 0.25
/// Probes whose rays hit back faces more often than this are moved out of the wall they are in
#define CLIPMAP_RELOCATE_BACKFACES 0.25
/// Probes whose rays hit back faces more often than this, on average, are switched off, and switched back on below
/// CLIPMAP_ACTIVE_BACKFACES
#define CLIPMAP_INACTIVE_BACKFACES 0.3
#define CLIPMAP_ACTIVE_BACKFACES 0.2
/// Once a probe has settled, its back face share follows each update by this much
#define CLIPMAP_BACKFACE_RATE (1.0 / 16.0)
/// Weight of a switched off probe in the gather, so a cell surrounded by them is still lit
#define CLIPMAP_INACTIVE_WEIGHT 1e-3
/// Over this many of its first updates a probe averages plainly (its back face share, its light without the change
/// detection) and refreshes its depth moments every time
#define CLIPMAP_SETTLE_UPDATES 8
/// Probes are read from their second update on. The first is traced from the grid point, which on a grid aligned map
/// is often on a wall, and is mostly back faces and no depth.
#define CLIPMAP_READY_UPDATES 2
/// A move of more than this much of a spacing while settling starts the probe's averages over
#define CLIPMAP_RESET_MOVE 0.1

struct ClipmapLevel
{
	/// The window's minimum probe coordinate, in probes
	int4 vOrigin;
	/// Probe spacing, its reciprocal, ray length, cone slope
	float4 vParams;
	/// The point the window is centred on, in probes. Unlike the origin it moves smoothly, so the band where a level
	/// hands over to the next does too.
	float4 vCenter;
};

struct ClipmapScene
{
	ClipmapLevel Levels[CLIPMAP_LEVELS];
};

uint3 ClipmapSlotCoord(uint slot)
{
	return uint3(slot % CLIPMAP_GRID_X, (slot / CLIPMAP_GRID_X) % CLIPMAP_GRID_Y, slot / (CLIPMAP_GRID_X * CLIPMAP_GRID_Y));
}

uint ClipmapSlot(int3 probe)
{
	const uint3 wrapped = uint3(probe & int3(CLIPMAP_GRID_X - 1, CLIPMAP_GRID_Y - 1, CLIPMAP_GRID_Z - 1));

	return wrapped.x + wrapped.y * CLIPMAP_GRID_X + wrapped.z * CLIPMAP_GRID_X * CLIPMAP_GRID_Y;
}

/// The probe coordinate that slot `slot` holds while the level's window starts at `origin`
int3 ClipmapSlotProbe(ClipmapLevel level, uint slot)
{
	const int3 grid = int3(CLIPMAP_GRID_X, CLIPMAP_GRID_Y, CLIPMAP_GRID_Z);
	const int3 wrapped = int3(ClipmapSlotCoord(slot));

	return level.vOrigin.xyz + ((wrapped - level.vOrigin.xyz) % grid + grid) % grid;
}

float3 ClipmapProbePosition(ClipmapLevel level, int3 probe) { return float3(probe) * level.vParams.x; }

/// Index of a level's slot in the per probe buffers
uint ClipmapProbeIndex(uint level, uint slot) { return level * CLIPMAP_PROBES_PER_LEVEL + slot; }

// bProbes holds a uint4 per slot, written by the blend each time the slot's probe is updated:
//   x: the CLIPMAP_PROBE_* flags in the low bits and the probe's grid point key above them
//   y: the probe's offset off its grid point, X and Y as halves
//   z: the offset's Z as a half, how many updates its light has been averaged over (saturating) and its mean back face
//      share
//   w: how many updates the probe has had (saturating), which its settling goes by
// A slot whose key isn't the probe the window now puts there hasn't been traced for it yet, so the record is the
// probe's validity as well. A cleared record is not valid.
#define CLIPMAP_PROBE_VALID 0x01
#define CLIPMAP_PROBE_ACTIVE 0x02
#define CLIPMAP_PROBE_FLAG_MASK 0x03u
#define CLIPMAP_PROBE_MAX_AGE 255

/// 10 bits of each coordinate, above the flags
uint ClipmapProbeKey(int3 probe)
{
	const uint3 bits = uint3(probe) & 0x3FFu;

	return (bits.x << 2) | (bits.y << 12) | (bits.z << 22);
}

struct ClipmapProbe
{
	bool bValid;
	bool bActive;
	float3 vOffset;
	uint uiAge;
	/// Share of the probe's rays that hit back faces, over its settling updates
	float fBackfaces;
	uint uiSettle;
	/// Filled, and past its first update, see CLIPMAP_READY_UPDATES
	bool bReady;
};

/// The record of `probe` from its slot's entry, as an unfilled probe if the entry was written for another
ClipmapProbe ClipmapDecodeProbe(uint4 record, int3 probe)
{
	const bool matches = (record.x & ~CLIPMAP_PROBE_FLAG_MASK) == ClipmapProbeKey(probe);

	ClipmapProbe result;
	result.bValid = and(matches, (record.x & CLIPMAP_PROBE_VALID) != 0);
	result.bActive = (record.x & CLIPMAP_PROBE_ACTIVE) != 0;
	result.vOffset = select(result.bValid,
							float3(f16tof32(record.y & 0xFFFFu), f16tof32(record.y >> 16), f16tof32(record.z & 0xFFFFu)),
							float3(0.0, 0.0, 0.0));
	result.uiAge = select(result.bValid, (record.z >> 16) & 0xFFu, 0u);
	result.fBackfaces = select(result.bValid, float(record.z >> 24) * (1.0 / 255.0), 0.0);
	result.uiSettle = select(result.bValid, record.w & 0xFFu, 0u);
	result.bReady = and(result.bValid, result.uiSettle >= CLIPMAP_READY_UPDATES);

	return result;
}

uint4 ClipmapEncodeProbe(int3 probe, ClipmapProbe data)
{
	const uint3 offset = f32tof16(data.vOffset);
	const uint backfaces = uint(saturate(data.fBackfaces) * 255.0 + 0.5);

	uint flags = ClipmapProbeKey(probe) | CLIPMAP_PROBE_VALID;
	flags |= select(data.bActive, CLIPMAP_PROBE_ACTIVE, 0u);

	return uint4(flags, offset.x | (offset.y << 16), offset.z | (min(data.uiAge, CLIPMAP_PROBE_MAX_AGE) << 16) | (backfaces << 24),
				 min(data.uiSettle, CLIPMAP_PROBE_MAX_AGE));
}

/// Where slot `slot` of level `level`'s tile of `tile_size` texels (border included) starts in its atlas
uint2 ClipmapTileOrigin(uint level, uint slot, uint tile_size)
{
	const uint3 coord = ClipmapSlotCoord(slot);

	return uint2(coord.x + coord.y * CLIPMAP_GRID_X, coord.z + level * CLIPMAP_GRID_Z) * tile_size;
}

/// Octahedral map coordinates in [-1, 1] for unit direction `d`, +Y at the centre. Matches ProbeDepthDirectionToOctUV.
float2 ClipmapDirectionToOct(float3 d)
{
	const float3 n = d / dot(abs(d), 1.0);

	const float2 signs = select(n.xz >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
	const float2 folded = (1.0 - abs(n.zx)) * signs;

	return select(n.y >= 0.0, n.xz, folded);
}

float3 ClipmapOctToDirection(float2 oct)
{
	const float y = 1.0 - abs(oct.x) - abs(oct.y);
	const float2 signs = select(oct >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
	const float2 xz = select(y >= 0.0, oct, (1.0 - abs(oct.yx)) * signs);

	return normalize(float3(xz.x, y, xz.y));
}

/// Atlas UV of `direction` in the tile of a level's slot, `size` interior texels a side
float2 ClipmapAtlasUV(uint level, uint slot, uint size, float3 direction)
{
	const float2 atlas_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_TILE_ROWS) * float(size + 2);
	const float2 origin = float2(ClipmapTileOrigin(level, slot, size + 2)) + 1.0;
	const float2 texel = origin + (ClipmapDirectionToOct(direction) * 0.5 + 0.5) * float(size);

	return texel / atlas_size;
}

/// The interior texel that border texel (x, y) of a tile with `size` interior texels a side copies, see
/// ProbeEncoding::TileTexelToMomentTexel
uint2 ClipmapBorderSource(int2 texel, int size)
{
	int u = texel.x - 1;
	int v = texel.y - 1;

	if (u < 0 || u >= size) {
		u = clamp(u, 0, size - 1);
		v = size - 1 - v;
	}

	if (v < 0 || v >= size) {
		v = clamp(v, 0, size - 1);
		u = size - 1 - u;
	}

	return uint2(u, v);
}

uint ClipmapHash(uint x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

/**
 * A uniformly random rotation (Shoemake) for one probe's update. Every probe gets its own, so the error of one
 * update's rays differs from probe to probe and averages out across the eight a surface reads, rather than every probe
 * traced in a frame being off the same way.
 */
float3x3 ClipmapRayRotation(uint level, uint slot, uint frame)
{
	const uint seed = ClipmapHash(ClipmapProbeIndex(level, slot) * 0x9E3779B9u + frame * 0x85EBCA6Bu);
	const uint h1 = ClipmapHash(seed);
	const uint h2 = ClipmapHash(seed ^ 0x68E31DA4u);
	const uint h3 = ClipmapHash(seed ^ 0xB5297A4Du);

	const float u1 = float(h1 & 0xFFFFFFu) * (1.0 / 16777216.0);
	const float u2 = float(h2 & 0xFFFFFFu) * (6.28318530718 / 16777216.0);
	const float u3 = float(h3 & 0xFFFFFFu) * (6.28318530718 / 16777216.0);

	const float a = sqrt(1.0 - u1);
	const float b = sqrt(u1);
	const float x = a * sin(u2);
	const float y = a * cos(u2);
	const float z = b * sin(u3);
	const float w = b * cos(u3);

	return float3x3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y),
					2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x),
					2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y));
}

/// Ray `ray` of the probe pattern: a Fibonacci sphere, turned by `rotation`
float3 ClipmapRayDirection(uint ray, float3x3 rotation)
{
	const float golden_angle = 2.39996323;
	const float z = 1.0 - (2.0 * float(ray) + 1.0) / float(CLIPMAP_RAYS);
	const float radius = sqrt(saturate(1.0 - z * z));
	const float phi = float(ray) * golden_angle;

	const float3 direction = float3(radius * cos(phi), z, radius * sin(phi));

	return normalize(mul(direction, rotation));
}

#endif
