#pragma once

#include <Core/Types.hpp>

namespace fx::Limits {
static constexpr uint32 MaxActiveLights = 64;
static constexpr uint32 MaxBones = 100;
static constexpr uint32 MaxDeletionQueueItems = 128;
static constexpr uint32 MaxConcurrentThreads = 10;

///////////////////////////////////
// Forward+ Tiled Lighting
///////////////////////////////////

/// Screen space tile dimensions in pixels
static constexpr uint32 LightTileSize = 16;

/// Maximum amount of lights that can be stored in a single tile
static constexpr uint32 MaxLightsPerTile = 64;

/// Maximum amount of tiles along each screen axis. Supports resolutions up to 3840x2160.
static constexpr uint32 MaxScreenTilesX = 240;
static constexpr uint32 MaxScreenTilesY = 135;
static constexpr uint32 MaxScreenTiles = MaxScreenTilesX * MaxScreenTilesY;


} // namespace fx::Limits
