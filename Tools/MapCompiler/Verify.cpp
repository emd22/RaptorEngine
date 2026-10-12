#include "Verify.hpp"

#include <algorithm>
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
constexpr double scLightmapBias = 1e-3;
constexpr double scLightmapEdgeTexels = 2.0;
constexpr double scLightmapLitThreshold = 0.5;

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

bool SeesSun(const PlaneSet& planes, const MapSource& source, const Vec3d& origin)
{
	for (const SourceBrush& brush : source.Brushes) {
		if (brush.bVisible && TraceBrush(planes, brush, origin, source.SunDirection) != scNoHit) {
			return false;
		}
	}

	return true;
}

bool IsInsideVisibleBrush(const PlaneSet& planes, const MapSource& source, const Vec3d& point)
{
	for (const SourceBrush& brush : source.Brushes) {
		if (!brush.bVisible) {
			continue;
		}

		bool inside = true;

		for (const int32 plane_index : brush.PlaneIndices) {
			inside = inside && (planes[plane_index].DistanceTo(point) <= 0.0);
		}

		if (inside) {
			return true;
		}
	}

	return false;
}

double SampleLightmap(const CompiledMapLightmap& lightmap, double u, double v)
{
	const double x = std::fmin(std::fmax(u * lightmap.Width - 0.5, 0.0), lightmap.Width - 1.0);
	const double y = std::fmin(std::fmax(v * lightmap.Height - 0.5, 0.0), lightmap.Height - 1.0);

	const uint32 x0 = static_cast<uint32>(x);
	const uint32 y0 = static_cast<uint32>(y);
	const uint32 x1 = std::min(x0 + 1, lightmap.Width - 1);
	const uint32 y1 = std::min(y0 + 1, lightmap.Height - 1);

	const double blend_x = x - x0;
	const double blend_y = y - y0;

	const auto texel = [&](uint32 texel_x, uint32 texel_y) {
		return lightmap.SunVisibility[static_cast<size_t>(texel_y) * lightmap.Width + texel_x] / 255.0;
	};

	const double top = texel(x0, y0) * (1.0 - blend_x) + texel(x1, y0) * blend_x;
	const double bottom = texel(x0, y1) * (1.0 - blend_x) + texel(x1, y1) * blend_x;

	return top * (1.0 - blend_y) + bottom * blend_y;
}

} // namespace

LightmapVerifyResult VerifyLightmap(const PlaneSet& planes, const MapSource& source, const CompiledMap& map,
									double density, uint32 point_count)
{
	LightmapVerifyResult result;

	std::mt19937_64 random(scSeed);
	std::uniform_real_distribution<double> unit(0.0, 1.0);

	if (map.Lightmap.IsEmpty() || map.Surfaces.Size == 0 || density <= 0.0) {
		return result;
	}

	const double edge_distance = scLightmapEdgeTexels / density;

	for (uint32 point_index = 0; point_index < point_count; point_index++) {
		const CompiledMapSurface& surface =
			map.Surfaces[std::min<size_t>(static_cast<size_t>(unit(random) * map.Surfaces.Size), map.Surfaces.Size - 1)];

		if (!surface.HasLightmapTexcoords() || surface.Indices.Size < 3) {
			continue;
		}

		const size_t triangle_count = surface.Indices.Size / 3;
		const size_t triangle = std::min<size_t>(static_cast<size_t>(unit(random) * triangle_count), triangle_count - 1);

		double weights[3] = { unit(random), unit(random), 0.0 };

		if (weights[0] + weights[1] > 1.0) {
			weights[0] = 1.0 - weights[0];
			weights[1] = 1.0 - weights[1];
		}

		weights[2] = 1.0 - weights[0] - weights[1];

		Vec3d position = Vec3d(0.0);
		double u = 0.0;
		double v = 0.0;

		for (uint32 corner = 0; corner < 3; corner++) {
			const uint32 index = surface.Indices[triangle * 3 + corner];
			const CompiledMapVertex& vertex = surface.Vertices[index];

			position = position +
					   Vec3d(vertex.Position[0], vertex.Position[1], vertex.Position[2]) * weights[corner];
			u += surface.LightmapTexcoords[index * 2] * weights[corner];
			v += surface.LightmapTexcoords[index * 2 + 1] * weights[corner];
		}

		const CompiledMapVertex& first = surface.Vertices[surface.Indices[triangle * 3]];
		const Vec3d normal(first.Normal[0], first.Normal[1], first.Normal[2]);

		const Vec3d reference = (std::abs(normal.Y) < 0.9) ? Vec3d(0.0, 1.0, 0.0) : Vec3d(1.0, 0.0, 0.0);
		const Vec3d right = reference.Cross(normal).Normalized();
		const Vec3d up = normal.Cross(right);

		const Vec3d origin = position + normal * scLightmapBias;
		const bool faces_sun = normal.Dot(source.SunDirection) > 0.0;

		const Vec3d offsets[5] = { Vec3d(0.0), right * edge_distance, right * -edge_distance, up * edge_distance,
								   up * -edge_distance };

		uint32 lit_count = 0;

		for (const Vec3d& offset : offsets) {
			lit_count += (faces_sun && SeesSun(planes, source, origin + offset)) ? 1 : 0;
		}

		result.PointCount++;

		if ((lit_count != 0 && lit_count != 5) || IsInsideVisibleBrush(planes, source, origin)) {
			result.EdgeCount++;
			continue;
		}

		const bool expected_lit = (lit_count == 5);
		const bool actual_lit = SampleLightmap(map.Lightmap, u, v) > scLightmapLitThreshold;

		result.LitCount += expected_lit ? 1 : 0;
		result.WrongCount += (expected_lit != actual_lit) ? 1 : 0;
	}

	return result;
}

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
