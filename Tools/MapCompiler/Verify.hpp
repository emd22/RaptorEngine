/*
 * File:        Verify.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Checks compiled triangles against the source brushes by tracing random rays through both
 */

#pragma once

#include "Bsp.hpp"
#include "MapSource.hpp"

namespace fx::mapc {

struct VerifyResult
{
	uint32 RayCount = 0;
	uint32 HitCount = 0;
	uint32 MissingCount = 0;
	uint32 ExtraCount = 0;
	double LargestError = 0.0;
};

struct LightmapVerifyResult
{
	uint32 PointCount = 0;
	uint32 EdgeCount = 0;
	uint32 HiddenCount = 0;
	uint32 LitCount = 0;
	uint32 WrongCount = 0;
};

VerifyResult VerifyTriangles(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
							 const Array<Vec3d>& triangle_points, const Array<Vec3d>& patch_points, uint32 ray_count);

LightmapVerifyResult VerifyLightmap(const PlaneSet& planes, const MapSource& source, const CompiledMap& map,
									const Array<Vec3d>& patch_points, const Array<Vec3d>& patch_facings, double density,
									uint32 point_count);

} // namespace fx::mapc
