#include "SDFPages.hpp"

#include "IndirectLight.hpp"
#include "Models.hpp"
#include "Surfaces.hpp"

#include <Math/DVec3.hpp>
#include <Math/Triangle.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace fx::mapc {

namespace {

// struct Triangle
// {
// 	DVec3 Corners[3];
// };

/// A triangle referenced by a page, keyed by the page so sorting groups each page's triangles together
struct SDFPageRef
{
	uint64 Key = 0;
	uint32 Triangle = 0;

	bool operator<(const SDFPageRef& other) const
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

int32 KeyCoord(uint64 key, uint32 shift)
{
	return static_cast<int32>(static_cast<int64>((key >> shift) & scKeyMask) - scKeyBias);
}

/// Albedo of the triangles that no material is known for, the same default the bake uses
constexpr float32 scDefaultAlbedo = 0.5f;

uint32 PackAlbedo(const FVec3& albedo)
{
	const auto channel = [](float32 value)
	{ return static_cast<uint32>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };

	return channel(albedo.X) | (channel(albedo.Y) << 8) | (channel(albedo.Z) << 16);
}

/**
 * @brief Adds triangle abc wound so that its face normal, (b - a) x (c - a), points the way `outward` does. The GI
 * tells probes inside walls apart by the back faces their rays hit, so the winding has to agree with the surface.
 * A zero `outward` keeps the winding as it is.
 */
void AddTriangle(Array<DTriangle>& triangles, Array<uint32>& albedos, uint32 albedo, const DVec3 a, const DVec3 b,
				 const DVec3 c, const DVec3 outward = DVec3(0.0))
{
	const DVec3 normal = (b - a).Cross(c - a);

	constexpr double scMinDoubleArea = 1e-12;

	if (normal.Dot(normal) <= scMinDoubleArea * scMinDoubleArea) {
		return;
	}

	if (normal.Dot(outward) < 0.0) {
		triangles.Insert(DTriangle { a, c, b });
	}
	else {
		triangles.Insert(DTriangle { a, b, c });
	}

	albedos.Insert(albedo);
}

void GatherTriangles(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
					 const CompiledMap& map, Array<DTriangle>& out_triangles, Array<uint32>& out_albedos)
{
	SizedArray<FVec3> map_albedos;
	SizedArray<FVec3> model_albedos;
	uint32 missing = 0;

	LoadMeanAlbedos(map, models, map_albedos, model_albedos, missing);

	const auto get_albedo = [](const SizedArray<FVec3>& albedos, uint32 index)
	{
		if (index < albedos.Size) {
			return PackAlbedo(albedos[index]);
		}

		return PackAlbedo(FVec3(scDefaultAlbedo));
	};

	for (const CompiledMapSurface& surface : map.Surfaces) {
		if (source.OpaqueMaterials[surface.MaterialIndex] == 0) {
			continue;
		}

		const uint32 albedo = get_albedo(map_albedos, static_cast<uint32>(surface.MaterialIndex));

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			const CompiledMapVertex& a = surface.Vertices[surface.Indices[index]];
			const CompiledMapVertex& b = surface.Vertices[surface.Indices[index + 1]];
			const CompiledMapVertex& c = surface.Vertices[surface.Indices[index + 2]];

			AddTriangle(out_triangles, out_albedos, albedo, DVec3(a.Position), DVec3(b.Position), DVec3(c.Position),
						DVec3(a.Normal) + DVec3(b.Normal) + DVec3(c.Normal));
		}
	}

	if (models != nullptr) {
		for (const ModelSurface& surface : models->Surfaces) {
			const uint32 albedo = get_albedo(model_albedos, surface.MaterialIndex);

			for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
				const ModelVertex& a = surface.Vertices[static_cast<size_t>(surface.Indices[index])];
				const ModelVertex& b = surface.Vertices[static_cast<size_t>(surface.Indices[index + 1])];
				const ModelVertex& c = surface.Vertices[static_cast<size_t>(surface.Indices[index + 2])];

				AddTriangle(out_triangles, out_albedos, albedo, a.Position, b.Position, c.Position,
							a.Normal + b.Normal + c.Normal);
			}
		}
	}

	// Blockers are faces nobody is meant to see, kept to stop light. The bake gives rays that reach one nothing, and so
	// do the probes: black, so neither the sun on them nor what bounces off them comes back.
	const uint32 blocker_albedo = PackAlbedo(FVec3(0.0f));

	for (size_t index = 0; index + 2 < surfaces.BlockerPoints.Size; index += 3) {
		AddTriangle(out_triangles, out_albedos, blocker_albedo, (surfaces.BlockerPoints[index]),
					(surfaces.BlockerPoints[index + 1]), (surfaces.BlockerPoints[index + 2]));
	}
}

void BuildLevel(const Array<DTriangle>& triangles, double sdf_page_size, double margin, Array<SDFPageRef>& out_refs)
{
	const double half_size = sdf_page_size * 0.5 + margin;

	for (uint32 triangle_index = 0; triangle_index < triangles.Size; triangle_index++) {
		const DTriangle& triangle = triangles[triangle_index];

		int64 low[3];
		int64 high[3];

		{
			const double v0 = triangle.Points[0].X;
			const double v1 = triangle.Points[1].X;
			const double v2 = triangle.Points[2].X;

			low[0] = static_cast<int64>(std::floor((std::min({ v0, v1, v2 }) - margin) / sdf_page_size));
			high[0] = static_cast<int64>(std::floor((std::max({ v0, v1, v2 }) + margin) / sdf_page_size));
		}
		{
			const double v0 = triangle.Points[0].Y;
			const double v1 = triangle.Points[1].Y;
			const double v2 = triangle.Points[2].Y;

			low[1] = static_cast<int64>(std::floor((std::min({ v0, v1, v2 }) - margin) / sdf_page_size));
			high[1] = static_cast<int64>(std::floor((std::max({ v0, v1, v2 }) + margin) / sdf_page_size));
		}
		{
			const double v0 = triangle.Points[0].Z;
			const double v1 = triangle.Points[1].Z;
			const double v2 = triangle.Points[2].Z;

			low[2] = static_cast<int64>(std::floor((std::min({ v0, v1, v2 }) - margin) / sdf_page_size));
			high[2] = static_cast<int64>(std::floor((std::max({ v0, v1, v2 }) + margin) / sdf_page_size));
		}

		for (int64 x = low[0]; x <= high[0]; x++) {
			for (int64 y = low[1]; y <= high[1]; y++) {
				for (int64 z = low[2]; z <= high[2]; z++) {
					const DVec3 xyz = DVec3(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
					const DVec3 center = (xyz + DVec3(0.5)) * DVec3(sdf_page_size);

					if (triangle.OverlapsBox(center, half_size)) {
						out_refs.Insert(SDFPageRef { MakeKey(x, y, z), triangle_index });
					}
				}
			}
		}
	}

	std::sort(out_refs.begin(), out_refs.end());
}

} // namespace

void BuildSDFPages(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
				   const SDFPageOptions& options, CompiledMap& out_map, SDFPageStats& out_stats)
{
	out_map.SDFPages = CompiledMapSDFPages {};
	out_stats = SDFPageStats {};

	if (!options.bEnabled || options.LevelCount == 0) {
		return;
	}

	const auto start_time = std::chrono::steady_clock::now();

	Array<DTriangle> triangles;
	Array<uint32> albedos;
	GatherTriangles(source, surfaces, models, out_map, triangles, albedos);

	if (triangles.Size == 0) {
		return;
	}

	const uint32 level_count = std::min(options.LevelCount, CompiledMapSDFPages::scMaxLevels);

	Array<SDFPageRef> level_refs[CompiledMapSDFPages::scMaxLevels];
	uint64 sdf_page_count = 0;
	uint64 ref_count = 0;

	for (uint32 level = 0; level < level_count; level++) {
		const double sdf_page_size = options.Level0SDFPageSize * static_cast<double>(1U << level);

		BuildLevel(triangles, sdf_page_size, sdf_page_size * options.MarginScale, level_refs[level]);

		const Array<SDFPageRef>& refs = level_refs[level];

		for (size_t index = 0; index < refs.Size; index++) {
			if (index == 0 || refs[index].Key != refs[index - 1].Key) {
				sdf_page_count++;
			}
		}

		ref_count += refs.Size;
	}

	CompiledMapSDFPages& pages = out_map.SDFPages;
	pages.MarginScale = static_cast<float32>(options.MarginScale);

	pages.Levels.InitCapacity(level_count);
	pages.SDFPages.InitCapacity(sdf_page_count);
	pages.Refs.InitCapacity(ref_count);
	pages.Triangles.InitCapacity(triangles.Size);

	for (size_t triangle_index = 0; triangle_index < triangles.Size; triangle_index++) {
		const DTriangle& triangle = triangles[triangle_index];
		CompiledMapSDFPageTriangle& packed = pages.Triangles.Emplace();

		for (uint32 corner = 0; corner < 3; corner++) {
			packed.Corners[corner][0] = static_cast<float32>(triangle.Points[corner].X);
			packed.Corners[corner][1] = static_cast<float32>(triangle.Points[corner].Y);
			packed.Corners[corner][2] = static_cast<float32>(triangle.Points[corner].Z);
			packed.Corners[corner][3] = 0.0f;
		}

		memcpy(&packed.Corners[0][3], &albedos[triangle_index], sizeof(uint32));
	}

	for (uint32 level = 0; level < level_count; level++) {
		const Array<SDFPageRef>& refs = level_refs[level];

		CompiledMapSDFPageLevel& packed_level = pages.Levels.Emplace();
		packed_level.SDFPageSize = static_cast<float32>(options.Level0SDFPageSize * static_cast<double>(1U << level));
		packed_level.FirstSDFPage = static_cast<uint32>(pages.SDFPages.Size);

		for (size_t index = 0; index < refs.Size; index++) {
			if (index == 0 || refs[index].Key != refs[index - 1].Key) {
				CompiledMapSDFPage& page = pages.SDFPages.Emplace();

				page.Coord[0] = KeyCoord(refs[index].Key, 42);
				page.Coord[1] = KeyCoord(refs[index].Key, 21);
				page.Coord[2] = KeyCoord(refs[index].Key, 0);

				page.FirstRef = static_cast<uint32>(pages.Refs.Size);
			}

			pages.Refs.Insert(refs[index].Triangle);
			pages.SDFPages[pages.SDFPages.Size - 1].RefCount++;
		}

		packed_level.SDFPageCount = static_cast<uint32>(pages.SDFPages.Size) - packed_level.FirstSDFPage;

		for (uint32 page = packed_level.FirstSDFPage; page < pages.SDFPages.Size; page++) {
			out_stats.MaxRefsPerPage = std::max(out_stats.MaxRefsPerPage, pages.SDFPages[page].RefCount);
		}

		out_stats.SDFPageCounts[level] = packed_level.SDFPageCount;
	}

	out_stats.TriangleCount = static_cast<uint32>(triangles.Size);
	out_stats.LevelCount = level_count;
	out_stats.RefCount = ref_count;
	out_stats.ByteCount = pages.Levels.GetSizeInBytes() + pages.SDFPages.GetSizeInBytes() +
						  pages.Refs.GetSizeInBytes() + pages.Triangles.GetSizeInBytes();

	out_stats.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

} // namespace fx::mapc
