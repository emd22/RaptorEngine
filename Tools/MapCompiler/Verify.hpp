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

VerifyResult VerifyTriangles(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
							 const Array<Vec3d>& triangle_points, uint32 ray_count);

} // namespace fx::mapc
