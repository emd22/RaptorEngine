/*
 * File:        FogShadow.hpp
 * Author:      emd22
 * Created:     25/10/2026
 * Description: Bakes sun shadow pages for the fog, depth maps seen along the baked sun direction
 */

#pragma once

#include <Map/CompiledMap.hpp>
#include <Math/Vec3d.hpp>

namespace fx::mapc {

class OccluderSet;

struct FogShadowOptions
{
	bool bEnabled = true;

	/// Size of a page along X and Z in metres. 100 metre world tiles hold 4x4 pages.
	double PageSize = 25.0;
	/// Size of a texel in the most detailed level, in metres
	double TexelSize = 0.1;
	/// Rays per texel along each axis, the nearest hit is kept so thin occluders are not lost
	uint32 Samples = 4;
	uint32 MaxLevelSize = 1024;
	/// Furthest a shadow is followed past the edge of the level, in metres
	double MaxReach = 50.0;
};

struct FogShadowStats
{
	uint32 CellCount = 0;
	uint32 PageCount = 0;
	uint64 TexelCount = 0;
	uint64 RayCount = 0;
	uint64 ByteCount = 0;
};

bool BakeFogShadow(const OccluderSet& occluders, const Vec3d& sun_direction, const FogShadowOptions& options,
				   CompiledMapFogShadow& out_shadow, FogShadowStats& out_stats);

} // namespace fx::mapc
