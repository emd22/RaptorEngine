#include "Verify.hpp"

#include <limits>
#include <random>

namespace fx::mapc {

namespace {

constexpr double scNoHit = std::numeric_limits<double>::infinity();
constexpr double scOriginClearance = 1e-2;
constexpr double scHitTolerance = 1e-4;
constexpr double scBarycentricTolerance = 1e-9;
constexpr double scParallelEpsilon = 1e-14;
constexpr uint32 scMaxOriginAttempts = 64;
constexpr uint32 scSeed = 0x52415054;

bool IsNearBrush(const PlaneSet& planes, const SourceBrush& brush, const Vec3d& point)
{
	for (const int32 plane_index : brush.PlaneIndices) {
		if (planes[plane_index].DistanceTo(point) > scOriginClearance) {
			return false;
		}
	}

	return true;
}

double TraceBrush(const PlaneSet& planes, const SourceBrush& brush, const Vec3d& origin, const Vec3d& direction)
{
	double enter = -scNoHit;
	double exit = scNoHit;

	for (const int32 plane_index : brush.PlaneIndices) {
		const Plane& plane = planes[plane_index];

		const double distance = plane.DistanceTo(origin);
		const double speed = plane.Normal.Dot(direction);

		if (std::abs(speed) < scParallelEpsilon) {
			if (distance > 0.0) {
				return scNoHit;
			}

			continue;
		}

		const double time = -distance / speed;

		if (speed < 0.0) {
			enter = std::fmax(enter, time);
		}
		else {
			exit = std::fmin(exit, time);
		}
	}

	return (enter > 0.0 && enter < exit) ? enter : scNoHit;
}

double TraceTriangle(const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& origin, const Vec3d& direction)
{
	const Vec3d edge_ab = b - a;
	const Vec3d edge_ac = c - a;

	const Vec3d p = direction.Cross(edge_ac);
	const double determinant = edge_ab.Dot(p);

	if (std::abs(determinant) < scParallelEpsilon) {
		return scNoHit;
	}

	const double inverse = 1.0 / determinant;
	const Vec3d offset = origin - a;

	const double u = offset.Dot(p) * inverse;

	if (u < -scBarycentricTolerance || u > 1.0 + scBarycentricTolerance) {
		return scNoHit;
	}

	const Vec3d q = offset.Cross(edge_ab);
	const double v = direction.Dot(q) * inverse;

	if (v < -scBarycentricTolerance || u + v > 1.0 + scBarycentricTolerance) {
		return scNoHit;
	}

	const double time = edge_ac.Dot(q) * inverse;

	return (time > 0.0) ? time : scNoHit;
}

} // namespace

VerifyResult VerifyTriangles(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
							 const Array<Vec3d>& triangle_points, uint32 ray_count)
{
	VerifyResult result;

	std::mt19937_64 random(scSeed);
	std::uniform_real_distribution<double> unit(0.0, 1.0);
	std::normal_distribution<double> normal(0.0, 1.0);

	const Vec3d size = source.Max - source.Min;

	for (uint32 ray = 0; ray < ray_count; ray++) {
		Vec3d origin = Vec3d(0.0);
		bool found_origin = false;

		for (uint32 attempt = 0; attempt < scMaxOriginAttempts && !found_origin; attempt++) {
			origin = source.Min + Vec3d(size.X * unit(random), size.Y * unit(random), size.Z * unit(random));

			const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(origin))];

			found_origin = !leaf.bSolid && (!sealed || leaf.bReached);

			for (size_t i = 0; i < source.Brushes.Size && found_origin; i++) {
				const SourceBrush& brush = source.Brushes[i];

				if (brush.bVisible || brush.bSolid) {
					found_origin = !IsNearBrush(planes, brush, origin);
				}
			}
		}

		if (!found_origin) {
			continue;
		}

		Vec3d direction(normal(random), normal(random), normal(random));
		const double length = direction.Length();

		if (length < 1e-6) {
			continue;
		}

		direction = direction * (1.0 / length);

		double expected = scNoHit;
		double bound = scNoHit;

		for (const SourceBrush& brush : source.Brushes) {
			if (brush.bVisible) {
				expected = std::fmin(expected, TraceBrush(planes, brush, origin, direction));
			}
			else if (brush.bSolid) {
				bound = std::fmin(bound, TraceBrush(planes, brush, origin, direction));
			}
		}

		double actual = scNoHit;

		for (size_t i = 0; i + 2 < triangle_points.Size; i += 3) {
			actual = std::fmin(actual, TraceTriangle(triangle_points[i], triangle_points[i + 1],
													 triangle_points[i + 2], origin, direction));
		}

		if (expected > bound) {
			expected = scNoHit;
		}

		if (actual > bound) {
			actual = scNoHit;
		}

		result.RayCount++;

		if (expected == scNoHit && actual == scNoHit) {
			continue;
		}

		result.HitCount++;

		if (actual == scNoHit || actual > expected + scHitTolerance * (1.0 + expected)) {
			result.MissingCount++;
		}
		else if (expected == scNoHit || actual < expected - scHitTolerance * (1.0 + expected)) {
			result.ExtraCount++;
		}
		else {
			result.LargestError = std::fmax(result.LargestError, std::abs(actual - expected));
		}
	}

	return result;
}

} // namespace fx::mapc
