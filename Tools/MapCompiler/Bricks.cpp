#include "Bricks.hpp"

#include "Models.hpp"
#include "Surfaces.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace fx::mapc {

namespace {

struct Point
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;

	double operator[](uint32 axis) const
	{
		if (axis == 0) {
			return X;
		}

		if (axis == 1) {
			return Y;
		}

		return Z;
	}
};

Point Sub(const Point& a, const Point& b) { return Point { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }

Point Cross(const Point& a, const Point& b)
{
	return Point { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
}

double Dot(const Point& a, const Point& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

Point ToPoint(const Vec3d& v) { return Point { v.X, v.Y, v.Z }; }

Point ToPoint(const Vec3f& v) { return Point { v.X, v.Y, v.Z }; }

struct Triangle
{
	Point Corners[3];
};

/// A triangle referenced by a brick, keyed by the brick so sorting groups each brick's triangles together
struct BrickRef
{
	uint64 Key = 0;
	uint32 Triangle = 0;

	bool operator<(const BrickRef& other) const
	{
		if (Key != other.Key) {
			return Key < other.Key;
		}

		return Triangle < other.Triangle;
	}
};

constexpr int64 scKeyBias = 1LL << 20;
constexpr uint64 scKeyMask = (1ULL << 21) - 1;

uint64 MakeKey(int64 x, int64 y, int64 z)
{
	return (static_cast<uint64>(x + scKeyBias) << 42) | (static_cast<uint64>(y + scKeyBias) << 21) |
		   static_cast<uint64>(z + scKeyBias);
}

int32 KeyCoord(uint64 key, uint32 shift) { return static_cast<int32>(static_cast<int64>((key >> shift) & scKeyMask) - scKeyBias); }

/// Separating axis test of a triangle against an axis aligned box (Akenine-Möller)
bool TriangleOverlapsBox(const Triangle& triangle, const Point& center, double half_size)
{
	const Point v0 = Sub(triangle.Corners[0], center);
	const Point v1 = Sub(triangle.Corners[1], center);
	const Point v2 = Sub(triangle.Corners[2], center);

	const Point edges[3] = { Sub(v1, v0), Sub(v2, v1), Sub(v0, v2) };
	const Point axes[3] = { Point { 1.0, 0.0, 0.0 }, Point { 0.0, 1.0, 0.0 }, Point { 0.0, 0.0, 1.0 } };

	for (const Point& edge : edges) {
		for (const Point& box_axis : axes) {
			const Point axis = Cross(box_axis, edge);

			const double p0 = Dot(v0, axis);
			const double p1 = Dot(v1, axis);
			const double p2 = Dot(v2, axis);

			const double radius = half_size * (std::abs(axis.X) + std::abs(axis.Y) + std::abs(axis.Z));

			if (std::min({ p0, p1, p2 }) > radius || std::max({ p0, p1, p2 }) < -radius) {
				return false;
			}
		}
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		const double low = std::min({ v0[axis], v1[axis], v2[axis] });
		const double high = std::max({ v0[axis], v1[axis], v2[axis] });

		if (low > half_size || high < -half_size) {
			return false;
		}
	}

	const Point normal = Cross(edges[0], edges[1]);
	const double distance = Dot(normal, v0);
	const double radius = half_size * (std::abs(normal.X) + std::abs(normal.Y) + std::abs(normal.Z));

	return std::abs(distance) <= radius;
}

void AddTriangle(Array<Triangle>& triangles, const Point& a, const Point& b, const Point& c)
{
	const Point normal = Cross(Sub(b, a), Sub(c, a));

	constexpr double scMinDoubleArea = 1e-12;

	if (Dot(normal, normal) <= scMinDoubleArea * scMinDoubleArea) {
		return;
	}

	triangles.Insert(Triangle { { a, b, c } });
}

void GatherTriangles(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
					 const CompiledMap& map, Array<Triangle>& out_triangles)
{
	for (const CompiledMapSurface& surface : map.Surfaces) {
		if (source.OpaqueMaterials[surface.MaterialIndex] == 0) {
			continue;
		}

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			AddTriangle(out_triangles, ToPoint(surface.Vertices[surface.Indices[index]].Position),
						ToPoint(surface.Vertices[surface.Indices[index + 1]].Position),
						ToPoint(surface.Vertices[surface.Indices[index + 2]].Position));
		}
	}

	if (models != nullptr) {
		for (const ModelSurface& surface : models->Surfaces) {
			for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
				AddTriangle(out_triangles,
							ToPoint(surface.Vertices[static_cast<size_t>(surface.Indices[index])].Position),
							ToPoint(surface.Vertices[static_cast<size_t>(surface.Indices[index + 1])].Position),
							ToPoint(surface.Vertices[static_cast<size_t>(surface.Indices[index + 2])].Position));
			}
		}
	}

	for (size_t index = 0; index + 2 < surfaces.BlockerPoints.Size; index += 3) {
		AddTriangle(out_triangles, ToPoint(surfaces.BlockerPoints[index]), ToPoint(surfaces.BlockerPoints[index + 1]),
					ToPoint(surfaces.BlockerPoints[index + 2]));
	}
}

void BuildLevel(const Array<Triangle>& triangles, double brick_size, double margin, Array<BrickRef>& out_refs)
{
	const double half_size = brick_size * 0.5 + margin;

	for (uint32 triangle_index = 0; triangle_index < triangles.Size; triangle_index++) {
		const Triangle& triangle = triangles[triangle_index];

		int64 low[3];
		int64 high[3];

		for (uint32 axis = 0; axis < 3; axis++) {
			const double min_value = std::min({ triangle.Corners[0][axis], triangle.Corners[1][axis],
												triangle.Corners[2][axis] });
			const double max_value = std::max({ triangle.Corners[0][axis], triangle.Corners[1][axis],
												triangle.Corners[2][axis] });

			low[axis] = static_cast<int64>(std::floor((min_value - margin) / brick_size));
			high[axis] = static_cast<int64>(std::floor((max_value + margin) / brick_size));
		}

		for (int64 x = low[0]; x <= high[0]; x++) {
			for (int64 y = low[1]; y <= high[1]; y++) {
				for (int64 z = low[2]; z <= high[2]; z++) {
					const Point center { (static_cast<double>(x) + 0.5) * brick_size,
										 (static_cast<double>(y) + 0.5) * brick_size,
										 (static_cast<double>(z) + 0.5) * brick_size };

					if (TriangleOverlapsBox(triangle, center, half_size)) {
						out_refs.Insert(BrickRef { MakeKey(x, y, z), triangle_index });
					}
				}
			}
		}
	}

	std::sort(out_refs.begin(), out_refs.end());
}

} // namespace

void BuildBricks(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
				 const BrickOptions& options, CompiledMap& out_map, BrickStats& out_stats)
{
	out_map.Bricks = CompiledMapBricks {};
	out_stats = BrickStats {};

	if (!options.bEnabled || options.LevelCount == 0) {
		return;
	}

	const auto start_time = std::chrono::steady_clock::now();

	Array<Triangle> triangles;
	GatherTriangles(source, surfaces, models, out_map, triangles);

	if (triangles.Size == 0) {
		return;
	}

	const uint32 level_count = std::min(options.LevelCount, CompiledMapBricks::scMaxLevels);

	Array<BrickRef> level_refs[CompiledMapBricks::scMaxLevels];
	uint64 brick_count = 0;
	uint64 ref_count = 0;

	for (uint32 level = 0; level < level_count; level++) {
		const double brick_size = options.Level0BrickSize * static_cast<double>(1U << level);

		BuildLevel(triangles, brick_size, brick_size * options.MarginScale, level_refs[level]);

		const Array<BrickRef>& refs = level_refs[level];

		for (size_t index = 0; index < refs.Size; index++) {
			if (index == 0 || refs[index].Key != refs[index - 1].Key) {
				brick_count++;
			}
		}

		ref_count += refs.Size;
	}

	CompiledMapBricks& bricks = out_map.Bricks;
	bricks.MarginScale = static_cast<float32>(options.MarginScale);

	bricks.Levels.InitCapacity(level_count);
	bricks.Bricks.InitCapacity(brick_count);
	bricks.Refs.InitCapacity(ref_count);
	bricks.Triangles.InitCapacity(triangles.Size);

	for (const Triangle& triangle : triangles) {
		CompiledMapBrickTriangle& packed = bricks.Triangles.Emplace();

		for (uint32 corner = 0; corner < 3; corner++) {
			packed.Corners[corner][0] = static_cast<float32>(triangle.Corners[corner].X);
			packed.Corners[corner][1] = static_cast<float32>(triangle.Corners[corner].Y);
			packed.Corners[corner][2] = static_cast<float32>(triangle.Corners[corner].Z);
			packed.Corners[corner][3] = 0.0f;
		}
	}

	for (uint32 level = 0; level < level_count; level++) {
		const Array<BrickRef>& refs = level_refs[level];

		CompiledMapBrickLevel& packed_level = bricks.Levels.Emplace();
		packed_level.BrickSize = static_cast<float32>(options.Level0BrickSize * static_cast<double>(1U << level));
		packed_level.FirstBrick = static_cast<uint32>(bricks.Bricks.Size);

		for (size_t index = 0; index < refs.Size; index++) {
			if (index == 0 || refs[index].Key != refs[index - 1].Key) {
				CompiledMapBrick& brick = bricks.Bricks.Emplace();
				brick.Coord[0] = KeyCoord(refs[index].Key, 42);
				brick.Coord[1] = KeyCoord(refs[index].Key, 21);
				brick.Coord[2] = KeyCoord(refs[index].Key, 0);
				brick.FirstRef = static_cast<uint32>(bricks.Refs.Size);
			}

			bricks.Refs.Insert(refs[index].Triangle);
			bricks.Bricks[bricks.Bricks.Size - 1].RefCount++;
		}

		packed_level.BrickCount = static_cast<uint32>(bricks.Bricks.Size) - packed_level.FirstBrick;
		out_stats.BrickCounts[level] = packed_level.BrickCount;
	}

	out_stats.TriangleCount = static_cast<uint32>(triangles.Size);
	out_stats.LevelCount = level_count;
	out_stats.RefCount = ref_count;
	out_stats.ByteCount = bricks.Levels.GetSizeInBytes() + bricks.Bricks.GetSizeInBytes() +
						  bricks.Refs.GetSizeInBytes() + bricks.Triangles.GetSizeInBytes();
	out_stats.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

} // namespace fx::mapc
