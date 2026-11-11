#include "Compiler.hpp"

#include "Areas.hpp"
#include "Bsp.hpp"
#include "MapSource.hpp"
#include "Models.hpp"
#include "Verify.hpp"

#include <Asset/ConfigFile.hpp>
#include <BrushEntry.hpp>
#include <Core/ArrayUtil.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Map/CompiledMap.hpp>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>

namespace fx {

void Terminate() { std::terminate(); }

} // namespace fx

namespace fx::mapc {

namespace {

bool HasFloorBelow(const PlaneSet& planes, const MapSource& source, const DVec3& point)
{
	for (const SourceBrush& brush : source.Brushes) {
		if (!brush.bCollides) {
			continue;
		}

		double enter = -1e30;
		double exit = 1e30;
		bool misses = false;

		for (const int32 plane_index : brush.PlaneIndices) {
			const Plane& plane = planes[plane_index];

			const double distance = plane.DistanceTo(point);
			const double speed = -plane.Normal.Y;

			if (std::abs(speed) < 1e-12) {
				misses = misses || (distance > 0.0);
				continue;
			}

			if (speed < 0.0) {
				enter = std::fmax(enter, -distance / speed);
			}
			else {
				exit = std::fmin(exit, -distance / speed);
			}
		}

		if (!misses && enter < exit && exit > 0.0) {
			return true;
		}
	}

	return false;
}

void AddHulls(const PlaneSet& planes, const MapSource& source, CompiledMap& map)
{
	constexpr float32 scHullPointEpsilon = 1e-5f;

	size_t hull_count = 0;

	for (const SourceBrush& brush : source.Brushes) {
		if (brush.bCollides) {
			hull_count++;
		}
	}

	map.BrushNames.InitCapacity(source.Brushes.Size + source.Patches.Size);

	for (uint32 index = 0; index < source.Patches.Size; index++) {
		const SourcePatch& patch = source.Patches[index];

		if (index > 0 && source.Patches[index - 1].Name == patch.Name) {
			continue;
		}

		map.BrushNames.Emplace(patch.Name);
	}

	map.Hulls.InitCapacity(hull_count);

	Array<float32> points;

	for (const SourceBrush& brush : source.Brushes) {
		map.BrushNames.Emplace(brush.Name);

		if (!brush.bCollides) {
			continue;
		}

		points.Clear();

		for (const PolyhedronSide& side : brush.Shape.Sides) {
			for (const DVec3& point : side.Points) {
				const float32 values[3] = { static_cast<float32>(point.X), static_cast<float32>(point.Y),
											static_cast<float32>(point.Z) };

				bool duplicate = false;

				for (uint32 i = 0; i + 2 < points.Size && !duplicate; i += 3) {
					duplicate = std::abs(points[i] - values[0]) < scHullPointEpsilon &&
								std::abs(points[i + 1] - values[1]) < scHullPointEpsilon &&
								std::abs(points[i + 2] - values[2]) < scHullPointEpsilon;
				}

				if (!duplicate) {
					points.Emplace(values[0]);
					points.Emplace(values[1]);
					points.Emplace(values[2]);
				}
			}
		}

		CompiledMapHull hull;
		hull.Points = ArrayUtil::DynToSizedArray(points);

		hull.SidePlanes.InitSize(brush.Shape.Sides.Size * 4);
		hull.SideMaterials.InitSize(brush.Shape.Sides.Size);

		for (size_t side_index = 0; side_index < brush.Shape.Sides.Size; side_index++) {
			const PolyhedronSide& side = brush.Shape.Sides[side_index];

			int32 material = brush.MaterialIndex;
			DVec3 normal = DVec3(0.0);
			double distance = 0.0;

			if (side.PlaneIndex != PlaneSet::scNone) {
				const Plane& plane = planes[side.PlaneIndex];
				normal = plane.Normal;
				distance = plane.Distance;

				int32 face_material = SourceBrush::scDroppedFace;

				if (brush.FindMap(side.PlaneIndex, face_material) != nullptr &&
					face_material != SourceBrush::scDroppedFace) {
					material = face_material;
				}
			}

			hull.SidePlanes[side_index * 4] = static_cast<float32>(normal.X);
			hull.SidePlanes[side_index * 4 + 1] = static_cast<float32>(normal.Y);
			hull.SidePlanes[side_index * 4 + 2] = static_cast<float32>(normal.Z);
			hull.SidePlanes[side_index * 4 + 3] = static_cast<float32>(distance);
			hull.SideMaterials[side_index] = material;
		}

		map.Hulls.Emplace(std::move(hull));
	}
}

/// Mirror ProbeClipmap::scLevelCount and scLevel0Spacing, and CLIPMAP_MAX_OFFSET in Shaders/ProbeClipmapCommon.hlsli
constexpr uint32 scProbeMaskLevels = 3;
constexpr double scProbeMaskSpacing = 1.0;
constexpr double scProbeMaskOffsets[2] = { 0.2, 0.4 };

/**
 * @brief Marks the points of the runtime GI probe grids that can hold a probe. A point inside a brush, or in space the
 * flood fill did not reach, still counts where a probe there could be moved into reached space, since the runtime
 * moves probes out of walls by up to that much.
 */
void BuildProbeMask(const Bsp& bsp, const MapSource& source, bool sealed, CompiledMap& map, const LogFunction& log)
{
	enum class ePoint
	{
		Open,
		Solid,
		Void,
	};

	const auto classify = [&](const DVec3& point)
	{
		const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(point))];

		if (leaf.bSolid) {
			return ePoint::Solid;
		}

		if (sealed && !leaf.bReached) {
			return ePoint::Void;
		}

		return ePoint::Open;
	};

	CompiledMapProbeMask& mask = map.ProbeMask;
	mask.bOutsideDead = sealed ? 1U : 0U;
	mask.Levels.InitSize(scProbeMaskLevels);

	const double min[3] = { source.Min.X, source.Min.Y, source.Min.Z };
	const double max[3] = { source.Max.X, source.Max.Y, source.Max.Z };

	uint64 word_count = 0;

	for (uint32 level_index = 0; level_index < scProbeMaskLevels; level_index++) {
		CompiledMapProbeMaskLevel& level = mask.Levels[level_index];
		const double spacing = scProbeMaskSpacing * static_cast<double>(1U << level_index);

		level.Spacing = static_cast<float32>(spacing);
		level.FirstWord = static_cast<uint32>(word_count);

		for (uint32 axis = 0; axis < 3; axis++) {
			const int32 first = static_cast<int32>(std::floor(min[axis] / spacing)) - 1;
			const int32 last = static_cast<int32>(std::ceil(max[axis] / spacing)) + 1;

			level.Origin[axis] = first;
			level.Size[axis] = static_cast<uint32>(last - first + 1);
		}

		word_count += (static_cast<uint64>(level.Size[0]) * level.Size[1] * level.Size[2] + 31) / 32;
	}

	mask.Words.InitSize(word_count, 0U);

	for (uint32 level_index = 0; level_index < scProbeMaskLevels; level_index++) {
		const CompiledMapProbeMaskLevel& level = mask.Levels[level_index];
		const double spacing = level.Spacing;

		uint64 open = 0;
		uint64 moved = 0;
		uint64 solid = 0;
		uint64 outside = 0;
		uint64 bit = 0;

		for (uint32 z = 0; z < level.Size[2]; z++) {
			for (uint32 y = 0; y < level.Size[1]; y++) {
				for (uint32 x = 0; x < level.Size[0]; x++, bit++) {
					const DVec3 point((static_cast<double>(level.Origin[0]) + x) * spacing,
									  (static_cast<double>(level.Origin[1]) + y) * spacing,
									  (static_cast<double>(level.Origin[2]) + z) * spacing);

					const ePoint at_point = classify(point);
					bool is_alive = (at_point == ePoint::Open);

					for (uint32 step = 0; step < 2 && !is_alive; step++) {
						const double reach = scProbeMaskOffsets[step] * spacing;

						for (int32 direction = 0; direction < 27 && !is_alive; direction++) {
							const DVec3 offset((direction % 3 - 1) * reach, ((direction / 3) % 3 - 1) * reach,
											   (direction / 9 - 1) * reach);

							is_alive = (direction != 13) && (classify(point + offset) == ePoint::Open);
						}
					}

					if (is_alive) {
						mask.Words[level.FirstWord + bit / 32] |= (1U << (bit % 32));

						if (at_point == ePoint::Open) {
							open++;
						}
						else {
							moved++;
						}
					}
					else if (at_point == ePoint::Solid) {
						solid++;
					}
					else {
						outside++;
					}
				}
			}
		}

		log(String::Fmt("  probe mask   {} m: {} x {} x {} points, {} open, {} kept for a move, {} in brushes, {} outside",
						spacing, level.Size[0], level.Size[1], level.Size[2], open, moved, solid, outside));
	}
}

bool WriteObj(const String& path, const CompiledMap& map)
{
	// std::FILE* file = std::fopen(path.CStr(), "w");

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		return false;
	}

	uint32 base = 1;

	for (size_t i = 0; i < map.Surfaces.Size; i++) {
		const CompiledMapSurface& surface = map.Surfaces[i];
		const CompiledMapMaterial& material = map.Materials[surface.MaterialIndex];

		String material_name = material.Name;

		if (material.Name.IsEmpty()) {
			material_name = String::Fmt("library_{}", material.LibraryIndex);
		}

		// std::fprintf(file, "o surface_%zu\nusemtl %s\n", i, material_name.CStr());
		file.Write(String::Fmt("o surface_{}\nusemtl {}\n", i, material_name));

		for (const CompiledMapVertex& vertex : surface.Vertices) {
			// std::fprintf(file, "v %.6f %.6f %.6f\n", vertex.Position[0], vertex.Position[1], vertex.Position[2]);
			file.Write(String::Fmt("v {} {} {}\n", vertex.Position.X, vertex.Position.Y, vertex.Position.Z));
		}

		for (const CompiledMapVertex& vertex : surface.Vertices) {
			// std::fprintf(file, "vt %.6f %.6f\n", vertex.Texcoord[0], 1.0f - vertex.Texcoord[1]);
			file.Write(String::Fmt("vt {} {}\n", vertex.Texcoord.X, 1.0f - vertex.Texcoord.Y));
		}

		for (const CompiledMapVertex& vertex : surface.Vertices) {
			// std::fprintf(file, "vn %.6f %.6f %.6f\n", vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]);
			file.Write(String::Fmt("vn {} {} {}\n", vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z));
		}

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			const uint32 a = base + surface.Indices[index];
			const uint32 b = base + surface.Indices[index + 1];
			const uint32 c = base + surface.Indices[index + 2];

			file.Write(String::Fmt("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, a, a, b, b, b, c, c, c));
		}

		base += static_cast<uint32>(surface.Vertices.Size);
	}

	for (size_t i = 0; i < map.ShadowSurfaces.Size; i++) {
		const CompiledMapShadowSurface& surface = map.ShadowSurfaces[i];

		file.Write(String::Fmt("o shadow_{}\nusemtl shadow\n", i));

		for (uint32 point = 0; point < surface.GetPointCount(); point++) {
			file.Write(String::Fmt("v {} {} {}\n", surface.Points[point * 3], surface.Points[point * 3 + 1],
								   surface.Points[point * 3 + 2]));
		}

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			file.Write(String::Fmt("f {} {} {}\n", base + surface.Indices[index], base + surface.Indices[index + 1],
								   base + surface.Indices[index + 2]));
		}

		base += surface.GetPointCount();
	}

	file.Close();

	return true;
}

String ReplaceExtension(const String& path, const char* extension)
{
	return String(std::filesystem::path(path.CStr()).replace_extension(extension).string());
}

double GetSeconds(std::chrono::steady_clock::time_point since)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

double GetPercentRemoved(double before, double after)
{
	if (before > 0.0) {
		return (1.0 - after / before) * 100.0;
	}

	return 0.0;
}

} // namespace

uint64 HashPrototypeFile(const String& prototype_path)
{
	std::error_code error;

	const std::filesystem::path input_path = std::filesystem::absolute(prototype_path.CStr(), error);
	const std::filesystem::path base_path = std::filesystem::absolute(FilesystemIO::GetBasePath(), error);
	const String engine_path = std::filesystem::relative(input_path, base_path, error).generic_string();

	ConfigFile info {};
	info.Load(engine_path.CStr());

	return HashPrototype(String(input_path.string()), info);
}

CompileResult CompileMap(const CompileOptions& options, const LogFunction& log)
{
	CompileResult result;

	std::error_code error;

	const std::filesystem::path input_path = std::filesystem::absolute(options.InputPath.CStr(), error);

	if (error || !std::filesystem::exists(input_path)) {
		log(String::Fmt("Cannot find '{}'", options.InputPath));
		return result;
	}

	if (options.OutputPath.IsEmpty()) {
		result.OutputPath = CompiledMap::GetPathForPrototype(String(input_path.string()));
	}
	else {
		result.OutputPath = options.OutputPath;
	}

	const std::filesystem::path base_path = std::filesystem::absolute(FilesystemIO::GetBasePath(), error);
	const String engine_path = std::filesystem::relative(input_path, base_path, error).generic_string();

	const auto start_time = std::chrono::steady_clock::now();

	PlaneSet planes;
	MapSource source;

	if (!source.Load(engine_path, planes)) {
		log(String::Fmt("Failed to load '{}'", options.InputPath));
		return result;
	}

	log(options.InputPath);

	for (const String& warning : source.Warnings) {
		log(String::Fmt("  warning      {}", warning));
	}

	log(String::Fmt(
		"  brushes      {} world ({} nvbrush, {} dbound, {} portal, {} transparent), {} left to the engine, {} invalid",
		source.Brushes.Size, source.ClipBrushCount, source.BoundBrushCount, source.PortalBrushCount,
		source.TransparentBrushCount, source.EntityBrushCount, source.InvalidBrushCount));

	if (source.NonVisFaceCount > 0) {
		log(String::Fmt("  nonvis       {} faces dropped", source.NonVisFaceCount));
	}

	ModelBuilder models;

	if (options.bModels) {
		models.Build(source);
	}

	if (source.Models.Size > 0) {
		const ModelStats& model_stats = models.Stats;

		if (!options.bModels) {
			log(String::Fmt("  models       {} left out of the bake (--no-models)", source.Models.Size));
		}
		else {
			log(String::Fmt("  models       {} of {} baked ({} triangles), {} probe boxes, {} left to the engine "
							"(unlit, no shadows, scripted or moving)",
							model_stats.BakedCount, source.Models.Size, model_stats.Triangles, model_stats.ProbeBoxes,
							source.Models.Size - model_stats.BakedCount));
		}

		for (const String& message : models.Errors) {
			log(String::Fmt("  warning      {}", message));
		}

		if (model_stats.SkippedMeshCount > 0) {
			log(String::Fmt("  warning      {} skinned or joint-attached model meshes are not baked, they only have a "
							"pose once animated",
							model_stats.SkippedMeshCount));
		}

		if (model_stats.BlendMeshCount > 0) {
			log(String::Fmt("  models       {} blended (see-through) model meshes neither cast baked shadows nor "
							"bounce light",
							model_stats.BlendMeshCount));
		}
	}


	CompiledMap map;
	map.SourceHash = HashPrototypeFile(String(input_path.string()));

	if (source.Brushes.Size == 0) {
		log("  nothing to compile");
	}

	Bsp bsp;
	FloodResult flood;
	AreaBuilder areas;
	SurfaceBuilder surfaces;
	PatchBuilder patches;

	bool sealed = false;

	if (source.Brushes.Size != 0) {
		Array<Polyhedron> shapes;
		Array<Polyhedron> portal_shapes;

		for (const SourceBrush& brush : source.Brushes) {
			if (brush.bSolid) {
				shapes.Emplace(brush.Shape.Clone());
			}
			else if (brush.bPortal) {
				portal_shapes.Emplace(brush.Shape.Clone());
			}
		}

		bsp.Build(planes, std::move(shapes), std::move(portal_shapes), source.Min, source.Max);
		bsp.BuildPortals();

		flood = bsp.FloodFill(source.Entities);
		sealed = flood.bSealed && !options.bKeepOutside;

		log(String::Fmt("  bsp          {} nodes, {} empty and {} solid leaves, {} portals, depth {}", bsp.Nodes.Size,
						bsp.EmptyLeafCount, bsp.SolidLeafCount, bsp.PortalCount / 2, bsp.MaxDepth));

		if (flood.EntitiesInSolid > 0) {
			log(String::Fmt("  warning      {} entities are inside solid brushes", flood.EntitiesInSolid));
		}

		if (source.bHasPlayerSpawn) {
			const DVec3& spawn = source.PlayerSpawn;

			if (bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(spawn))].bSolid) {
				log(String::Fmt("  warning      the player spawn at ({:.2f}, {:.2f}, {:.2f}) is inside a solid brush",
								spawn.X, spawn.Y, spawn.Z));
			}
			else if (!HasFloorBelow(planes, source, spawn)) {
				log(String::Fmt("  warning      nothing is under the player spawn at ({:.2f}, {:.2f}, {:.2f}), the "
								"player will fall out of the world",
								spawn.X, spawn.Y, spawn.Z));
			}
		}

		areas.Build(planes, bsp, source, sealed);

		for (const String& warning : areas.Warnings) {
			log(String::Fmt("  warning      {}", warning));
		}

		if (areas.Stats.AreaCount > 0) {
			log(String::Fmt("  areas        {} areas, {} portals ({} portal brushes ignored)", areas.Stats.AreaCount,
							areas.Stats.PortalCount, areas.Stats.IgnoredPortalCount));
		}

		SurfaceOptions surface_options = options.Surface;
		surface_options.bMakeLightmapCharts = options.Lightmap.bEnabled &&
											  (source.bHasSun || options.Lightmap.bIndirect);

		patches.Build(source, bsp, sealed, surface_options.bCullHidden);

		surfaces.Build(planes, bsp, source, sealed, surface_options, &patches, map);
		map.bSealed = sealed;

		areas.Write(planes, bsp, source, map);

		AddHulls(planes, source, map);
		patches.WriteCollision(surface_options.ChunkSize, map);
	}

	if (options.bPlaceProbes && !source.ProbeBoxes.IsEmpty()) {
		ProbePlacementBoxes& boxes = source.ProbeBoxes;

		if (sealed) {
			boxes.IsOpen = [&bsp](const FVec3 point)
			{
				const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(DVec3(point.X, point.Y, point.Z)))];
				return leaf.bSolid || leaf.bReached;
			};
		}

		ProbePlacement::BuildLevelLayout(
			map.Probes, boxes, Slice<BBox>(source.ProbeRegions.pData, source.ProbeRegions.Size), options.ProbeSettings);
		map.ProbeSettings = options.ProbeSettings;

		result.ProbeCount = map.Probes.GetProbeCount();
		result.ProbeVolumeCount = map.Probes.VolumeCount;
	}

	LightmapBaker lightmap;

	result.bHasLightmap = lightmap.Bake(planes, source, surfaces, &patches, &models, options.Lightmap, map);
	result.Lightmap = lightmap.Stats;

	if (source.Brushes.Size != 0) {
		BuildSDFPages(source, surfaces, &models, options.SDFPages, map, result.SDFPages);
	}

	if (source.Brushes.Size != 0 && options.SDFPages.bEnabled) {
		BuildProbeMask(bsp, source, sealed, map, log);
	}

	const String leak_path = ReplaceExtension(result.OutputPath, ".leak");

	std::filesystem::remove(leak_path.CStr(), error);

	if (flood.bSealed) {
		result.Seal = eSealState::Sealed;
		const char* kept_note = "";
		if (options.bKeepOutside) {
			kept_note = ", faces towards the void kept";
		}

		log(String::Fmt("  seal         sealed{}", kept_note));
	}
	else if (flood.EntitiesUsed == 0) {
		log("  seal         unknown, there are no entities (spawn, lights, models) in open space to fill from. Faces "
			"towards the void are kept");
	}
	else if (source.Brushes.Size != 0) {
		result.Seal = eSealState::Leaked;
		result.LeakPath = leak_path;

		std::FILE* leak_file = std::fopen(leak_path.CStr(), "w");

		if (leak_file != nullptr) {
			for (const DVec3& point : flood.LeakPath) {
				std::fprintf(leak_file, "%.6f %.6f %.6f\n", point.X, point.Y, point.Z);
			}

			std::fclose(leak_file);
		}

		const DVec3& from = flood.LeakPath[0];
		const DVec3& to = GetLast(flood.LeakPath);

		log(String::Fmt("  seal         LEAKED, faces towards the void are kept. The path from the entity at ({:.2f}, "
						"{:.2f}, {:.2f}) to the void at ({:.2f}, {:.2f}, {:.2f}) is in {}",
						from.X, from.Y, from.Z, to.X, to.Y, to.Z, leak_path));
	}

	const SurfaceStats& stats = surfaces.Stats;
	result.Stats = stats;

	log(String::Fmt("  faces        {}, {} removed, {} clipped, {} overlaps cut", stats.NumSrcFaces, stats.FacesRemoved,
					stats.FacesClipped, stats.OverlapsRemoved));

	for (const String& warning : surfaces.Warnings) {
		log(String::Fmt("  warning      {}", warning));
	}

	log(String::Fmt("  polygons     {} pieces merged into {}, {} T-junctions fixed", stats.NumPolygonsBeforeMerge,
					stats.NumPolygons, stats.TJunctionsFixed));

	if (stats.MergeFallbacks > 0) {
		log(String::Fmt("  warning      {} groups of faces could not be merged and were left as separate pieces",
						stats.MergeFallbacks));
	}

	log(String::Fmt("  triangles    {} -> {} ({:+.1f}%), {} vertices in {} surfaces", stats.NumSrcTriangles,
					stats.NumTriangles, -GetPercentRemoved(stats.NumSrcTriangles, stats.NumTriangles),
					stats.NumVertices, map.Surfaces.Size));
	log(String::Fmt("  shadows      {} triangles in {} surfaces, {} of them nonvis", stats.NumShadowTriangles,
					map.ShadowSurfaces.Size, stats.NumNonVisTriangles));
	log(String::Fmt("  area         {:.1f} -> {:.1f} m2 ({:+.1f}%)", stats.SourceArea, stats.Area,
					-GetPercentRemoved(stats.SourceArea, stats.Area)));
	if (source.Patches.Size > 0) {
		const PatchStats& patch_stats = patches.Stats;

		log(String::Fmt("  patches      {} patches, {} triangles ({} buried or facing the void removed), {} vertices "
						"smoothed across seams, {} T-junctions fixed",
						patch_stats.PatchCount, patch_stats.Triangles, patch_stats.CulledTriangles,
						patch_stats.SmoothedVertices, patch_stats.TJunctionsFixed));
	}

	log(String::Fmt("  collision    {} hulls, {} triangle meshes ({} triangles)", map.Hulls.Size,
					map.CollisionMeshes.Size, patches.Stats.CollisionTriangles));

	if (map.HasProbeLayout()) {
		log(String::Fmt("  probes       {} of {} in {} volumes ({} from probe volume brushes), {} grid points, {} left "
						"out of the void and map bounds",
						map.Probes.GetProbeCount(), Limits::MaxIrradianceProbes, map.Probes.VolumeCount,
						source.ProbeRegions.Size, map.Probes.GetGridPointCount(), map.Probes.VoidPointCount));

		if (source.ProbeBoxes.ObjectCount > source.ProbeBoxes.Boxes.Size) {
			log(String::Fmt("  warning      probes are only kept out of the first {} of {} brushes",
							source.ProbeBoxes.Boxes.Size, source.ProbeBoxes.ObjectCount));
		}
	}

	if (result.bHasLightmap) {
		const LightmapStats& baked = lightmap.Stats;

		log(String::Fmt("  lightmap     {}x{}, {} charts at {:.1f} texels per metre, {:.1f}% of {} texels lit, {} rays "
						"against {} brushes",
						baked.Width, baked.Height, baked.ChartCount, baked.Density, baked.LitFraction * 100.0,
						baked.TexelCount, baked.RayCount, baked.OccluderCount));

		const uint32 patch_occluders = baked.OccluderTriangleCount - baked.OccluderModelTriangleCount;

		if (patch_occluders > 0) {
			log(String::Fmt("  lightmap     {} patch triangles also cast shadows", patch_occluders));
		}

		if (baked.OccluderModelTriangleCount > 0) {
			log(String::Fmt("  lightmap     {} model triangles also cast shadows", baked.OccluderModelTriangleCount));
		}

		if (baked.bHasIndirect) {
			uint32 light_count = baked.IndirectLightCount;

			if (source.bHasSun) {
				light_count++;
			}

			log(String::Fmt(
				"  indirect     {} bounces, {} rays against {} triangles and {} nonvis triangles, {} lights, "
				"{:.0f} lux on average",
				options.Lightmap.IndirectBounces, baked.IndirectRayCount, baked.IndirectTriangleCount,
				baked.IndirectBlockerCount, light_count, baked.MeanIndirect));

			if (baked.IndirectModelTriangleCount > 0) {
				log(String::Fmt("  indirect     {} model triangles bounce light through {} cells of {:.2f} m",
								baked.IndirectModelTriangleCount, baked.IndirectModelCellCount,
								options.Lightmap.ModelCellSize));
			}

			if (baked.IndirectWaterVolumeCount > 0) {
				log(String::Fmt("  indirect     {} water volumes absorb the rays that cross them",
								baked.IndirectWaterVolumeCount));
			}

			if (baked.IndirectCachedTexelCount > 0) {
				log(String::Fmt("  hit cache    {} of {} texels ({:.1f}%) reuse their first bounce hits, {:.0f} MB",
								baked.IndirectCachedTexelCount, baked.IndirectTexelCount,
								100.0 * static_cast<double>(baked.IndirectCachedTexelCount) /
									static_cast<double>(baked.IndirectTexelCount),
								baked.IndirectCacheMegabytes));
			}

			if (baked.BakedProbeCount > 0) {
				log(String::Fmt("  probes       {} light probes baked with {} rays, {:.0f} lux on average",
								baked.BakedProbeCount, baked.ProbeRayCount, baked.MeanProbeIrradiance));
			}

			if (baked.MissingAlbedoCount > 0) {
				log(String::Fmt(
					"  warning      {} materials have no readable diffuse texture, they bounce light as grey",
					baked.MissingAlbedoCount));
			}
		}

		if (baked.Density < options.Lightmap.Density) {
			log(String::Fmt("  warning      the lightmap did not fit in {}x{} at {:.1f} texels per metre, so it was "
							"baked at {:.1f}",
							options.Lightmap.MaxSize, options.Lightmap.MaxSize, options.Lightmap.Density,
							baked.Density));
		}

		if (baked.bHasFogShadow) {
			const FogShadowStats& fog = baked.FogShadow;
			log(String::Fmt(
				"  fog shadow   {} of {} {:.0f} m pages shadowed, {} texels at {:.2f} m, {} rays, {:.1f} MB",
				fog.PageCount, fog.CellCount, options.Lightmap.FogShadow.PageSize, fog.TexelCount,
				options.Lightmap.FogShadow.TexelSize, fog.RayCount,
				static_cast<double>(fog.ByteCount) / (1024.0 * 1024.0)));
		}
		else if (source.bHasSun && options.Lightmap.FogShadow.bEnabled) {
			log("  fog shadow   none, the sun is too low or nothing casts a shadow");
		}

		if (!source.bHasSun) {
			log("  lightmap     no sun, the prototype has none or it is switched off");
		}
		else if (baked.ProbeCount > 0) {
			log(String::Fmt("  sun probes   {} of {} reference probes see the sun", baked.LitProbeCount,
							baked.ProbeCount));
		}
	}
	else if (!options.Lightmap.bEnabled) {
		log("  lightmap     off");
	}
	else if (!source.bHasSun && !options.Lightmap.bIndirect) {
		log("  lightmap     none, the prototype has no sun or it is switched off");
	}
	else if (source.Brushes.Size != 0) {
		log(String::Fmt("  warning      no lightmap, the surfaces do not fit in {}x{} texels", options.Lightmap.MaxSize,
						options.Lightmap.MaxSize));
	}

	if (!map.SDFPages.IsEmpty()) {
		const SDFPageStats& pages = result.SDFPages;
		String counts;

		for (uint32 level = 0; level < pages.LevelCount; level++) {
			if (level > 0) {
				counts += " / ";
			}

			counts += String::Fmt("{}", pages.SDFPageCounts[level]);
		}

		log(String::Fmt("  sdf pages    {} levels from {:.2f} m, {} triangles, {} pages, {} refs (at most {} in a "
						"page), {:.1f} MB, built in {:.2f} s",
						pages.LevelCount, options.SDFPages.Level0SDFPageSize, pages.TriangleCount, counts,
						pages.RefCount, pages.MaxRefsPerPage, static_cast<double>(pages.ByteCount) / (1024.0 * 1024.0),
						pages.Seconds));
	}
	else if (!options.SDFPages.bEnabled) {
		log("  sdf pages    off");
	}

	if (options.VerifyRays > 0 && source.Brushes.Size != 0) {
		const VerifyResult verify = VerifyTriangles(planes, bsp, source, sealed, surfaces.TrianglePoints,
													patches.ReferencePoints, options.VerifyRays);

		log(String::Fmt("  verify       {} rays, {} hit geometry, {} missing, {} extra, largest error {:.2e}",
						verify.RayCount, verify.HitCount, verify.MissingCount, verify.ExtraCount, verify.LargestError));

		if (verify.MissingCount > 0 || verify.ExtraCount > 0) {
			log("  verify       FAILED, the compiled triangles do not match the brushes");
			result.bVerifyFailed = true;
		}
	}

	if (options.VerifyRays > 0 && result.bHasLightmap && source.bHasSun) {
		// Model triangles shadow the lightmap too, so the reference traces them like patch triangles
		Array<DVec3> occluder_points = CopyArray(patches.ReferencePoints);
		Array<DVec3> occluder_facings = CopyArray(patches.ReferenceFacings);

		for (const ModelSurface& surface : models.Surfaces) {
			for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
				DVec3 facing = DVec3(0.0);

				for (uint32 corner = 0; corner < 3; corner++) {
					const ModelVertex& vertex = surface.Vertices[static_cast<size_t>(surface.Indices[index + corner])];

					occluder_points.Emplace(vertex.Position);
					facing = facing + vertex.Normal;
				}

				occluder_facings.Emplace(facing);
			}
		}

		const LightmapVerifyResult verify = VerifyLightmap(planes, source, map, occluder_points, occluder_facings,
														   lightmap.Stats.Density, options.VerifyRays);

		log(String::Fmt("  verify       {} lightmap points, {} lit, {} skipped beside a shadow edge, {} hidden "
						"behind patches, {} wrong",
						verify.PointCount, verify.LitCount, verify.EdgeCount, verify.HiddenCount, verify.WrongCount));

		constexpr uint32 scMaxWrongLightmapPoints = 200;

		if (verify.WrongCount * scMaxWrongLightmapPoints > verify.PointCount) {
			log("  verify       FAILED, the lightmap does not match the sun");
			result.bVerifyFailed = true;
		}
	}

	if (!options.Acoustics.bEnabled) {
		log("  acoustics    off");
	}
	else if (BakeAcoustics(options.Acoustics, map, result.Acoustics, log)) {
		const AcousticStats& acoustics = result.Acoustics;

		log(String::Fmt(
			"  acoustics    {} probes {:.1f} m apart from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f}), "
			"baked in {:.1f} s (reverb) + {:.1f} s (pathing), {:.1f} KB",
			acoustics.ProbeCount, options.Acoustics.ProbeSpacing, acoustics.ProbeMin.X, acoustics.ProbeMin.Y,
			acoustics.ProbeMin.Z, acoustics.ProbeMax.X, acoustics.ProbeMax.Y, acoustics.ProbeMax.Z,
			acoustics.ReflectionSeconds, acoustics.PathingSeconds, static_cast<double>(acoustics.DataSize) / 1024.0));
	}
	else if (!result.Acoustics.Error.IsEmpty()) {
		log(String::Fmt("  warning      no acoustics, {}", result.Acoustics.Error));
	}

	if (!map.Save(result.OutputPath)) {
		log(String::Fmt("Failed to write '{}'", result.OutputPath));
		return result;
	}

	if (!options.ObjPath.IsEmpty() && !WriteObj(options.ObjPath, map)) {
		log(String::Fmt("Failed to write '{}'", options.ObjPath));
		return result;
	}

	result.Seconds = GetSeconds(start_time);
	result.bSucceeded = true;

	log(String::Fmt("  wrote        {} ({:.2f} s)", result.OutputPath, result.Seconds));

	return result;
}

} // namespace fx::mapc
