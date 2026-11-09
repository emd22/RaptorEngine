/*
 * File:        Bricks.hpp
 * Author:      emd22
 * Created:     08/11/2026
 * Description: Finds the bricks of every GI clip level that geometry passes through, and the triangles each one has to
 *              take its distances from. The engine computes the distances on the GPU when a brick comes into view.
 */

#pragma once

#include "MapSource.hpp"

#include <Map/CompiledMap.hpp>

namespace fx::mapc {

class SurfaceBuilder;
class ModelBuilder;

struct BrickOptions
{
	bool bEnabled = true;
	uint32 LevelCount = 5;
	/// Width of a brick in the finest level, in metres. Every level after it doubles.
	double Level0BrickSize = 0.25;
	double MarginScale = 0.5;
};

struct BrickStats
{
	uint32 TriangleCount = 0;
	uint32 LevelCount = 0;
	uint32 BrickCounts[CompiledMapBricks::scMaxLevels] = {};
	uint64 RefCount = 0;
	uint64 ByteCount = 0;
	double Seconds = 0.0;
};

/**
 * @brief Builds `out_map.Bricks` from the opaque compiled surfaces, the nonvis blockers and the baked models: the same
 * triangles the indirect bake traces against
 */
void BuildBricks(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
				 const BrickOptions& options, CompiledMap& out_map, BrickStats& out_stats);

} // namespace fx::mapc
