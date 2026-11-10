#ifndef PROBE_CLIPMAP_COMMON_HLSLI
#define PROBE_CLIPMAP_COMMON_HLSLI

/// Mirrors ProbeClipmap in Src/Renderer/ProbeClipmap.hpp
#define CLIPMAP_LEVELS 3
#define CLIPMAP_GRID_X 32
#define CLIPMAP_GRID_Y 16
#define CLIPMAP_GRID_Z 32
#define CLIPMAP_PROBES_PER_LEVEL (CLIPMAP_GRID_X * CLIPMAP_GRID_Y * CLIPMAP_GRID_Z)
#define CLIPMAP_RAYS 64

/// Irradiance is 8x8 per probe on every level, depth moments 16x16 on level 0 and 8x8 on the others. Each tile has a
/// one texel border holding the texels across the octahedral seams.
#define CLIPMAP_IRRADIANCE_SIZE 8
#define CLIPMAP_MOMENTS_SIZE_0 16
#define CLIPMAP_MOMENTS_SIZE 8

/// Tiles are laid out with X and Y side by side across the image and Z down it
#define CLIPMAP_TILE_COLUMNS (CLIPMAP_GRID_X * CLIPMAP_GRID_Y)

#define CLIPMAP_STATE_VALID 0x01

/// Probes are moved at most this much of their spacing off their grid point, along each axis
#define CLIPMAP_MAX_OFFSET 0.45
/// Probes this close (in spacings) to a front face are pushed away from it
#define CLIPMAP_MIN_FRONTFACE 0.2
/// Probes whose rays hit back faces more often than this are moved out of the wall they are in
#define CLIPMAP_RELOCATE_BACKFACES 0.25
/// Probes whose rays hit back faces more often than this, where they ended up, are switched off
#define CLIPMAP_INACTIVE_BACKFACES 0.3
/// Weight of a switched off probe in the gather, so a cell surrounded by them is still lit
#define CLIPMAP_INACTIVE_WEIGHT 1e-3

struct ClipmapLevel
{
	/// The window's minimum probe coordinate, in probes
	int4 vOrigin;
	/// Probe spacing, its reciprocal, ray length, cone slope
	float4 vParams;
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

// bProbeData holds a float4 per probe: its offset off its grid point in XYZ, and CLIPMAP_PROBE_* flags in the bits of W
#define CLIPMAP_PROBE_ACTIVE 0x01
/// The probe moved far enough that its tiles describe somewhere else, so its next update starts them over
#define CLIPMAP_PROBE_RESET 0x02
/// Moves smaller than this much of a spacing keep the probe's history
#define CLIPMAP_RESET_MOVE 0.1

/// Where slot `slot`'s tile of `tile_size` texels (border included) starts
uint2 ClipmapTileOrigin(uint slot, uint tile_size)
{
	const uint3 coord = ClipmapSlotCoord(slot);

	return uint2(coord.x + coord.y * CLIPMAP_GRID_X, coord.z) * tile_size;
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

#endif
