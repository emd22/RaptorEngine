#pragma once

#include <Core/Types.hpp>

namespace fx::Limits {
static constexpr uint32 MaxActiveLights = 64;
/// Number of bone matrices `GraphicsBackend::BoneBuffer` can hold per frame, shared between every skinned object that
/// updates its pose that frame. Each skeleton claims a run the length of its joint count, so a few very large
/// skeletons and many small ones fit in the same budget. Skeletons that do not fit are skipped for that frame.
static constexpr uint32 MaxBoneMatrices = 16384;
static constexpr uint32 MaxDeletionQueueItems = 128;
static constexpr uint32 MaxConcurrentThreads = 10;

///////////////////////////////////
// Forward+ Tiled Lighting
///////////////////////////////////

/// Screen space tile dimensions in pixels
static constexpr uint32 LightTileSize = 16;

/// Maximum amount of lights that can be stored in a single tile
static constexpr uint32 MaxLightsPerTile = 6;

/// Maximum amount of tiles along each screen axis. Supports resolutions up to 3840x2160.
static constexpr uint32 MaxScreenTilesX = 240;
static constexpr uint32 MaxScreenTilesY = 135;
static constexpr uint32 MaxScreenTiles = MaxScreenTilesX * MaxScreenTilesY;

///////////////////////////////////
// Clustered Decals
///////////////////////////////////

/// Decals kept in the world at once. New decals replace the oldest ones past this.
static constexpr uint32 MaxDecals = 512;

/// Decals uploaded for a single frame, after frustum culling. Mirrored by MAX_VISIBLE_DECALS in
/// Shaders/DecalCommon.hlsli.
static constexpr uint32 MaxVisibleDecals = 512;

/// Each screen tile has a bit per visible decal, packed into this many words. Mirrored by DECAL_MASK_WORDS.
static constexpr uint32 DecalMaskWords = MaxVisibleDecals / 32;

static_assert((MaxVisibleDecals % 32) == 0);

///////////////////////////////////////
// Light Probes
//////////////////////////////////////

/// Number of SH coefficients for an L2 irradiance probe.
/// Mirrored by PROBE_SH_COEFF_COUNT in Shaders/ProbeCommon.hlsli.
static constexpr uint32 ProbeSHCoeffCount = 9;

/// Probe budget shared by every volume. Each volume takes a contiguous range out of it, so this has to leave
/// room for more than the one grid fitted to the level (ProbeGridDims), which takes half of it on its own.
/// Mirrored by PROBE_MAX_PROBES in Shaders/ProbeCommon.hlsli, which derives the moments atlas height from it.
static constexpr uint32 MaxIrradianceProbes = 4096;


static constexpr uint32 MaxProbeVolumes = 8;

/// Default probe grid dimensions (X x Y x Z) of a volume fitted to the level.
static constexpr uint32 ProbeGridDims[3] = { 16, 8, 16 };

static constexpr uint32 MinProbeGridDim = 2;

static_assert((ProbeGridDims[0] * ProbeGridDims[1] * ProbeGridDims[2]) <= MaxIrradianceProbes);

/////////////////////////////////////
// Probe depth moments (visibility)
/////////////////////////////////////
//
// These have no compile-time link to the shader, so each one is mirrored by a
// PROBE_* define in Shaders/ProbeCommon.hlsli and must be changed in both places.
// A mismatch is silent: the shader would index the moments buffer with a stride
// the bake never used.

/// Resolution of the per-probe depth cubemap face (16x16 texels per face).
/// Mirrored by PROBE_DEPTH_SIZE.
static constexpr uint32 ProbeDepthSize = 16;
/// Mirrored by PROBE_DEPTH_FACES.
static constexpr uint32 ProbeDepthFaces = 6;
static constexpr uint32 ProbeDepthTexelsPerFace = ProbeDepthSize * ProbeDepthSize;
/// Two moments per texel: mean distance and mean squared distance.
/// The shader pairs these as a single float2, so this is fixed at 2.
static constexpr uint32 ProbeDepthMomentsPerTexel = 2;
static constexpr uint32 ProbeDepthFloatCount = ProbeDepthFaces * ProbeDepthTexelsPerFace * ProbeDepthMomentsPerTexel;

/// Distances beyond this are clamped (misses / sky count as far).
/// Mirrored by PROBE_DEPTH_MAX_DISTANCE. The shader clamps the receiver distance
/// to the same value, so a receiver further out than this cannot be occluded.
static constexpr float ProbeDepthMaxDistance = 50.0f;

static constexpr uint32 ProbeAtlasColumns = 32;
static constexpr uint32 ProbeAtlasProbeWidth = ProbeDepthFaces * ProbeDepthSize;
static constexpr uint32 ProbeAtlasRows = (MaxIrradianceProbes + ProbeAtlasColumns - 1) / ProbeAtlasColumns;
static constexpr uint32 ProbeAtlasWidth = ProbeAtlasColumns * ProbeAtlasProbeWidth;
static constexpr uint32 ProbeAtlasHeight = ProbeAtlasRows * ProbeDepthSize;

// Vulkan only guarantees maxImageDimension2D of 4096
static_assert(ProbeAtlasWidth <= 4096 && ProbeAtlasHeight <= 4096, "The probe moment atlas must fit a 4096 texture");
static_assert(ProbeAtlasColumns * ProbeAtlasRows >= MaxIrradianceProbes, "The atlas must hold every probe");


} // namespace fx::Limits
