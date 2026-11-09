#pragma once

#include <Core/Types.hpp>

namespace fx::Limits {
static constexpr uint32 MaxActiveLights = 64;
/// Number of bone matrices `GraphicsBackend::BoneBuffer` can hold per frame, shared between every skinned object that
/// updates its pose that frame.
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
static constexpr uint32 MaxSkinnedDecals = 64;
static constexpr uint32 MaxSkinnedDecalsPerSkeleton = 16;

static constexpr uint32 DecalMaskWords = MaxVisibleDecals / 32;

static_assert((MaxVisibleDecals % 32) == 0);

///////////////////////////////////////
// Light Probes
//////////////////////////////////////

/// Number of SH coefficients for an L2 irradiance probe.
/// Mirrored by PROBE_SH_COEFF_COUNT in Shaders/ProbeCommon.hlsli.
static constexpr uint32 ProbeSHCoeffCount = 9;

static constexpr uint32 MaxIrradianceProbes = 4096;
static constexpr uint32 MaxProbeVolumes = 8;

/// Mirrored by PROBE_GRID_EMPTY in Shaders/ProbeCommon.hlsli.
static constexpr uint16 ProbeGridEmpty = 0xFFFF;

/// Mirrored by PROBE_GRID_SOLID in Shaders/ProbeCommon.hlsli.
static constexpr uint16 ProbeGridSolid = 0xFFFE;

static constexpr uint32 MaxProbeGridPoints = 1u << 20;

static_assert(MaxIrradianceProbes < ProbeGridSolid);
static_assert((MaxProbeGridPoints % 2) == 0);

/// Default probe grid dimensions (X x Y x Z) of a volume fitted to the level.
static constexpr uint32 ProbeGridDims[3] = { 16, 8, 16 };

static constexpr uint32 MinProbeGridDim = 2;

static_assert((ProbeGridDims[0] * ProbeGridDims[1] * ProbeGridDims[2]) <= MaxIrradianceProbes);

/////////////////////////////////////
// Probe depth moments (visibility)
/////////////////////////////////////

/// Resolution of the per-probe octahedral depth map (16x16 texels covering the whole sphere).
/// Mirrored by PROBE_DEPTH_SIZE.
static constexpr uint32 ProbeDepthSize = 16;
static constexpr uint32 ProbeDepthTexelCount = ProbeDepthSize * ProbeDepthSize;


static constexpr uint32 ProbeDepthMomentsPerTexel = 2;
static constexpr uint32 ProbeDepthFloatCount = ProbeDepthTexelCount * ProbeDepthMomentsPerTexel;

static constexpr float ProbeDepthMaxDistance = 50.0f;

static constexpr uint32 ProbeAtlasTileSize = ProbeDepthSize + 2;
static constexpr uint32 ProbeAtlasColumns = 64;
static constexpr uint32 ProbeAtlasRows = (MaxIrradianceProbes + ProbeAtlasColumns - 1) / ProbeAtlasColumns;
static constexpr uint32 ProbeAtlasWidth = ProbeAtlasColumns * ProbeAtlasTileSize;
static constexpr uint32 ProbeAtlasHeight = ProbeAtlasRows * ProbeAtlasTileSize;

// Vulkan only guarantees maxImageDimension2D of 4096
static_assert(ProbeAtlasWidth <= 4096 && ProbeAtlasHeight <= 4096, "The probe moment atlas must fit a 4096 texture");
static_assert(ProbeAtlasColumns * ProbeAtlasRows >= MaxIrradianceProbes, "The atlas must hold every probe");

static constexpr uint32 MaxReflectionProbes = 16;

static constexpr uint32 ReflectionProbeSize = 128;

static constexpr uint32 ReflectionProbeMips = 6;

static constexpr uint32 ReflectionProbeFaces = 6;

static_assert((ReflectionProbeSize >> (ReflectionProbeMips - 1)) >= 1);

static constexpr uint32 DfgLutSize = 32;

static constexpr uint32 LightmapSize = 4096;


} // namespace fx::Limits
