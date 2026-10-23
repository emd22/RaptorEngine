#include "Patches.hpp"

#include <Core/ArrayUtil.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

namespace fx::mapc {

namespace {

constexpr double scSmoothCellSize = 1e-3;
constexpr double scSmoothEpsilon = 2e-5;
constexpr double scSmoothingCosine = 0.766;
constexpr double scCullOffset = 1e-3;
constexpr double scCullInset = 0.1;
constexpr double scMinNormalLength = 1e-12;
constexpr uint32 scOwnGroupFlag = 0x80000000u;

struct SmoothKey
{
	uint32 Group = 0;
	int64 X = 0;
	int64 Y = 0;
	int64 Z = 0;

	bool operator==(const SmoothKey& other) const
	{
		return Group == other.Group && X == other.X && Y == other.Y && Z == other.Z;
	}
};

struct SmoothKeyHash
{
	size_t operator()(const SmoothKey& key) const
	{
		uint64 hash = static_cast<uint64>(key.Group) * 0x9E3779B97F4A7C15ull;
		hash ^= static_cast<uint64>(key.X) * 0xC2B2AE3D27D4EB4Full + (hash << 6) + (hash >> 2);
		hash ^= static_cast<uint64>(key.Y) * 0x165667B19E3779F9ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<uint64>(key.Z) * 0x27D4EB2F165667C5ull + (hash << 6) + (hash >> 2);
		return static_cast<size_t>(hash);
	}
};

SmoothKey MakeSmoothKey(uint32 group, const Vec3d& point)
{
	return SmoothKey { group, static_cast<int64>(std::floor(point.X / scSmoothCellSize)),
					   static_cast<int64>(std::floor(point.Y / scSmoothCellSize)),
					   static_cast<int64>(std::floor(point.Z / scSmoothCellSize)) };
}

struct VertexRef
{
	uint32 Surface = 0;
	uint32 Vertex = 0;
};

Vec3d Orthogonalize(const Vec3d& tangent, const Vec3d& normal)
{
	const Vec3d projected = tangent - normal * normal.Dot(tangent);
	const double length = projected.Length();

	if (length <= scMinNormalLength) {
		return tangent;
	}

	return projected * (1.0 / length);
}

struct ChartGrid
{
	uint32 Columns = 0;
	uint32 Rows = 0;

	Array<Vec2d> Points;

	Vec2d Find(const Patch& patch, const Vec2d& parameter) const
	{
		if (Points.Size == 0) {
			return Vec2d(0.0, 0.0);
		}

		const int64 column = std::clamp<int64>(std::llround(parameter.X * patch.SubdivisionsU), 0, Columns - 1);
		const int64 row = std::clamp<int64>(std::llround(parameter.Y * patch.SubdivisionsV), 0, Rows - 1);

		return Points[static_cast<size_t>(row * Columns + column)];
	}
};

void BuildChartGrid(const Patch& patch, ChartGrid& out_grid)
{
	Patch smooth = patch.Clone();
	smooth.bSmooth = true;

	PatchMesh mesh;
	smooth.Tessellate(mesh);

	const uint32 columns = mesh.Columns;
	const uint32 rows = mesh.Rows;

	out_grid.Columns = columns;
	out_grid.Rows = rows;
	out_grid.Points.Clear();

	if (columns == 0 || rows == 0 || mesh.Vertices.Size != static_cast<size_t>(columns) * rows) {
		out_grid.Columns = 0;
		out_grid.Rows = 0;
		return;
	}

	const auto at = [&](uint32 row, uint32 column) -> const Vec3d&
	{ return mesh.Vertices[static_cast<size_t>(row) * columns + column].Position; };

	Array<double> row_lengths;
	double longest_row = 0.0;

	for (uint32 row = 0; row < rows; row++) {
		double length = 0.0;

		for (uint32 column = 0; column + 1 < columns; column++) {
			length += (at(row, column + 1) - at(row, column)).Length();
		}

		row_lengths.Emplace(length);
		longest_row = std::fmax(longest_row, length);
	}

	Array<double> row_heights;
	row_heights.Emplace(0.0);

	for (uint32 row = 1; row < rows; row++) {
		double step = 0.0;

		for (uint32 column = 0; column < columns; column++) {
			step += (at(row, column) - at(row - 1, column)).Length();
		}

		row_heights.Emplace(row_heights[row - 1] + step / static_cast<double>(columns));
	}

	for (uint32 row = 0; row < rows; row++) {
		double along = (longest_row - row_lengths[row]) * 0.5;

		for (uint32 column = 0; column < columns; column++) {
			if (column > 0) {
				along += (at(row, column) - at(row, column - 1)).Length();
			}

			out_grid.Points.Emplace(Vec2d(along, row_heights[row]));
		}
	}
}

bool IsBlocked(const Bsp& bsp, const Vec3d& point, bool sealed)
{
	const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(point))];

	return leaf.bSolid || (sealed && !leaf.bReached);
}

} // namespace

void PatchBuilder::Build(const MapSource& source, const Bsp& bsp, bool sealed, bool cull_hidden)
{
	Stats = PatchStats {};
	Surfaces.Clear();
	ReferencePoints.Clear();
	ReferenceFacings.Clear();

	for (uint32 patch_index = 0; patch_index < source.Patches.Size; patch_index++) {
		const SourcePatch& patch = source.Patches[patch_index];

		PatchMesh mesh;
		patch.Shape.Tessellate(mesh);

		if (mesh.Indices.IsEmpty()) {
			continue;
		}

		PatchSurface& surface = Surfaces.Emplace();
		surface.SourceIndex = patch_index;
		surface.MaterialIndex = patch.MaterialIndex;
		surface.bCollides = patch.bCollides;
		surface.bCastsShadow = patch.bCastsShadow;

		ChartGrid chart;
		BuildChartGrid(patch.Shape, chart);

		for (const PatchVertex& vertex : mesh.Vertices) {
			surface.Vertices.Emplace(PatchSurfaceVertex {
				.Position = vertex.Position,
				.Normal = vertex.Normal,
				.Tangent = vertex.Tangent,
				.Handedness = vertex.Handedness,
				.Texcoord = vertex.Texcoord,
				.Chart = chart.Find(patch.Shape, vertex.Parameter),
				.Color = vertex.Color,
			});
		}

		for (const uint32 index : mesh.Indices) {
			surface.Indices.Emplace(static_cast<int32>(index));
			ReferencePoints.Emplace(mesh.Vertices[index].Position);
		}

		for (size_t index = 0; index + 2 < mesh.Indices.Size; index += 3) {
			ReferenceFacings.Emplace(mesh.Vertices[mesh.Indices[index]].Normal +
									 mesh.Vertices[mesh.Indices[index + 1]].Normal +
									 mesh.Vertices[mesh.Indices[index + 2]].Normal);
		}

		Stats.PatchCount++;
		Stats.SourceTriangles += static_cast<uint32>(mesh.Indices.Size / 3);
	}

	SmoothNormals(source);

	if (cull_hidden && bsp.Nodes.Size > 0) {
		Cull(bsp, sealed);
	}
}

void PatchBuilder::SmoothNormals(const MapSource& source)
{
	std::unordered_map<SmoothKey, Array<VertexRef>, SmoothKeyHash> cells;
	Array<uint32> groups;

	for (uint32 surface_index = 0; surface_index < Surfaces.Size; surface_index++) {
		const PatchSurface& surface = Surfaces[surface_index];
		const Patch& shape = source.Patches[surface.SourceIndex].Shape;

		uint32 group = 0;

		if (shape.bSmooth) {
			group = scOwnGroupFlag | surface_index;

			if (shape.SmoothingGroup != 0) {
				group = shape.SmoothingGroup & ~scOwnGroupFlag;
			}
		}

		groups.Emplace(group);

		if (group == 0) {
			continue;
		}

		for (uint32 vertex_index = 0; vertex_index < surface.Vertices.Size; vertex_index++) {
			cells[MakeSmoothKey(group, surface.Vertices[vertex_index].Position)].Emplace(
				VertexRef { surface_index, vertex_index });
		}
	}

	Array<Array<Vec3d>> smoothed;

	for (uint32 surface_index = 0; surface_index < Surfaces.Size; surface_index++) {
		const PatchSurface& surface = Surfaces[surface_index];
		Array<Vec3d>& normals = smoothed.Emplace();

		for (const PatchSurfaceVertex& vertex : surface.Vertices) {
			Vec3d normal = vertex.Normal;
			const uint32 group = groups[surface_index];

			if (group != 0) {
				const SmoothKey center = MakeSmoothKey(group, vertex.Position);

				Vec3d sum = Vec3d(0.0);
				uint32 count = 0;

				for (int64 x = -1; x <= 1; x++) {
					for (int64 y = -1; y <= 1; y++) {
						for (int64 z = -1; z <= 1; z++) {
							const auto found = cells.find(
								SmoothKey { group, center.X + x, center.Y + y, center.Z + z });

							if (found == cells.end()) {
								continue;
							}

							for (const VertexRef& other_ref : found->second) {
								const PatchSurfaceVertex& other = Surfaces[other_ref.Surface]
																	  .Vertices[other_ref.Vertex];

								if ((other.Position - vertex.Position).Length() < scSmoothEpsilon &&
									other.Normal.Dot(vertex.Normal) > scSmoothingCosine) {
									sum = sum + other.Normal;
									count++;
								}
							}
						}
					}
				}

				const double length = sum.Length();

				if (count > 1 && length > scMinNormalLength) {
					normal = sum * (1.0 / length);
					Stats.SmoothedVertices++;
				}
			}

			normals.Emplace(normal);
		}
	}

	for (uint32 surface_index = 0; surface_index < Surfaces.Size; surface_index++) {
		PatchSurface& surface = Surfaces[surface_index];

		for (uint32 vertex_index = 0; vertex_index < surface.Vertices.Size; vertex_index++) {
			PatchSurfaceVertex& vertex = surface.Vertices[vertex_index];

			vertex.Normal = smoothed[surface_index][vertex_index];
			vertex.Tangent = Orthogonalize(vertex.Tangent, vertex.Normal);
		}
	}
}

void PatchBuilder::Cull(const Bsp& bsp, bool sealed)
{
	for (PatchSurface& surface : Surfaces) {
		Array<int32> kept;

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			const Vec3d& a = surface.Vertices[static_cast<size_t>(surface.Indices[index])].Position;
			const Vec3d& b = surface.Vertices[static_cast<size_t>(surface.Indices[index + 1])].Position;
			const Vec3d& c = surface.Vertices[static_cast<size_t>(surface.Indices[index + 2])].Position;

			const Vec3d cross = (b - a).Cross(c - a);
			const double length = cross.Length();

			bool is_hidden = length > scMinNormalLength;

			if (is_hidden) {
				const Vec3d normal = cross * (scCullOffset / length);
				const Vec3d centroid = (a + b + c) * (1.0 / 3.0);

				const Vec3d samples[4] = {
					centroid,
					a + (centroid - a) * scCullInset,
					b + (centroid - b) * scCullInset,
					c + (centroid - c) * scCullInset,
				};

				for (const Vec3d& sample : samples) {
					is_hidden = is_hidden && IsBlocked(bsp, sample + normal, sealed) &&
								IsBlocked(bsp, sample - normal, sealed);
				}
			}

			if (is_hidden) {
				Stats.CulledTriangles++;
				continue;
			}

			kept.Emplace(surface.Indices[index]);
			kept.Emplace(surface.Indices[index + 1]);
			kept.Emplace(surface.Indices[index + 2]);
		}

		surface.Indices = std::move(kept);
	}
}

void PatchBuilder::FindBoundaryEdges(const PatchSurface& surface, Array<std::pair<int32, int32>>& out_edges)
{
	out_edges.Clear();

	std::map<std::tuple<double, double, double>, int32> position_ids;
	Array<int32> ids;

	for (const PatchSurfaceVertex& vertex : surface.Vertices) {
		const auto key = std::make_tuple(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
		const auto found = position_ids.emplace(key, static_cast<int32>(position_ids.size())).first;

		ids.Emplace(found->second);
	}

	std::map<std::pair<int32, int32>, int32> edge_counts;

	const auto edge_key = [&ids](int32 a, int32 b)
	{
		const int32 id_a = ids[static_cast<size_t>(a)];
		const int32 id_b = ids[static_cast<size_t>(b)];

		return std::make_pair(std::min(id_a, id_b), std::max(id_a, id_b));
	};

	for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
		for (uint32 corner = 0; corner < 3; corner++) {
			edge_counts[edge_key(surface.Indices[index + corner], surface.Indices[index + (corner + 1) % 3])]++;
		}
	}

	for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
		for (uint32 corner = 0; corner < 3; corner++) {
			const int32 from = surface.Indices[index + corner];
			const int32 to = surface.Indices[index + (corner + 1) % 3];

			if (edge_counts[edge_key(from, to)] == 1) {
				out_edges.Emplace(from, to);
			}
		}
	}
}

void PatchBuilder::WriteCollision(double chunk_size, CompiledMap& out_map)
{
	struct MeshBuilder
	{
		Array<float32> Points;
		Array<uint32> Indices;
		std::map<std::pair<uint32, int32>, uint32> Lookup;
	};

	std::map<std::tuple<int64, int64, int64>, MeshBuilder> meshes;

	for (uint32 surface_index = 0; surface_index < Surfaces.Size; surface_index++) {
		const PatchSurface& surface = Surfaces[surface_index];

		if (!surface.bCollides) {
			continue;
		}

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			Vec3d centroid = Vec3d(0.0);

			for (uint32 corner = 0; corner < 3; corner++) {
				centroid = centroid + surface.Vertices[static_cast<size_t>(surface.Indices[index + corner])].Position;
			}

			centroid = centroid * (1.0 / 3.0);

			std::tuple<int64, int64, int64> cell { 0, 0, 0 };

			if (chunk_size > 0.0) {
				cell = std::make_tuple(static_cast<int64>(std::floor(centroid.X / chunk_size)),
									   static_cast<int64>(std::floor(centroid.Y / chunk_size)),
									   static_cast<int64>(std::floor(centroid.Z / chunk_size)));
			}

			MeshBuilder& mesh = meshes[cell];

			for (uint32 corner = 0; corner < 3; corner++) {
				const int32 vertex_index = surface.Indices[index + corner];
				auto found = mesh.Lookup.find({ surface_index, vertex_index });

				if (found == mesh.Lookup.end()) {
					const Vec3d& position = surface.Vertices[static_cast<size_t>(vertex_index)].Position;

					found = mesh.Lookup.emplace(std::make_pair(surface_index, vertex_index), mesh.Points.Size / 3)
								.first;

					mesh.Points.Emplace(static_cast<float32>(position.X));
					mesh.Points.Emplace(static_cast<float32>(position.Y));
					mesh.Points.Emplace(static_cast<float32>(position.Z));
				}

				mesh.Indices.Emplace(found->second);
			}

			Stats.CollisionTriangles++;
		}
	}

	out_map.CollisionMeshes.InitCapacity(meshes.size());

	for (auto& [cell, mesh] : meshes) {
		CompiledMapCollisionMesh& out_mesh = out_map.CollisionMeshes.Emplace();
		out_mesh.Points = ArrayUtil::DynToSizedArray(mesh.Points);
		out_mesh.Indices = ArrayUtil::DynToSizedArray(mesh.Indices);
	}

	Stats.CollisionMeshes = static_cast<uint32>(out_map.CollisionMeshes.Size);
}

} // namespace fx::mapc
