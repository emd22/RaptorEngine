#include "Compiler.hpp"

#include "Bsp.hpp"
#include "MapSource.hpp"
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

constexpr float32 scHullPointEpsilon = 1e-5f;
constexpr uint32 scMaxWrongLightmapPoints = 200;

bool HasFloorBelow(const PlaneSet& planes, const MapSource& source, const Vec3d& point)
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

void AddHulls(const MapSource& source, CompiledMap& map)
{
	size_t hull_count = 0;

	for (const SourceBrush& brush : source.Brushes) {
		hull_count += brush.bCollides ? 1 : 0;
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
			for (const Vec3d& point : side.Points) {
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

		map.Hulls.Emplace(std::move(hull));
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

		const String material_name = material.Name.IsEmpty() ? String::Fmt("library_{}", material.LibraryIndex)
															 : material.Name;

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

double GetPercentRemoved(double before, double after) { return (before > 0.0) ? (1.0 - after / before) * 100.0 : 0.0; }

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

	result.OutputPath = options.OutputPath.IsEmpty() ? CompiledMap::GetPathForPrototype(String(input_path.string()))
													 : options.OutputPath;

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
		"  brushes      {} world ({} nvbrush, {} dbound, {} transparent), {} left to the engine, {} invalid",
		source.Brushes.Size, source.ClipBrushCount, source.BoundBrushCount, source.TransparentBrushCount,
		source.EntityBrushCount, source.InvalidBrushCount));


	CompiledMap map;
	map.SourceHash = HashPrototypeFile(String(input_path.string()));

	if (source.Brushes.Size == 0) {
		log("  nothing to compile");
	}

	Bsp bsp;
	FloodResult flood;
	SurfaceBuilder surfaces;
	PatchBuilder patches;

	bool sealed = false;

	if (source.Brushes.Size != 0) {
		Array<Polyhedron> shapes;

		for (const SourceBrush& brush : source.Brushes) {
			if (brush.bSolid) {
				shapes.Emplace(brush.Shape.Clone());
			}
		}

		bsp.Build(planes, std::move(shapes), source.Min, source.Max);
		bsp.BuildPortals();

		flood = bsp.FloodFill(source.Entities);
		sealed = flood.bSealed && !options.bKeepOutside;

		log(String::Fmt("  bsp          {} nodes, {} empty and {} solid leaves, {} portals, depth {}", bsp.Nodes.Size,
						bsp.EmptyLeafCount, bsp.SolidLeafCount, bsp.PortalCount / 2, bsp.MaxDepth));

		if (flood.EntitiesInSolid > 0) {
			log(String::Fmt("  warning      {} entities are inside solid brushes", flood.EntitiesInSolid));
		}

		if (source.bHasPlayerSpawn) {
			const Vec3d& spawn = source.PlayerSpawn;

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

		SurfaceOptions surface_options = options.Surface;
		surface_options.bMakeLightmapCharts = options.Lightmap.bEnabled && source.bHasSun;

		patches.Build(source, bsp, sealed, surface_options.bCullHidden);

		surfaces.Build(planes, bsp, source, sealed, surface_options, &patches, map);
		map.bSealed = sealed;

		AddHulls(source, map);
		patches.WriteCollision(surface_options.ChunkSize, map);
	}

	if (options.bPlaceProbes && !source.ProbeBoxes.IsEmpty()) {
		ProbePlacementBoxes& boxes = source.ProbeBoxes;

		if (sealed) {
			boxes.IsOpen = [&bsp](const Vec3f point)
			{
				const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(Vec3d(point.X, point.Y, point.Z)))];
				return leaf.bSolid || leaf.bReached;
			};
		}

		ProbePlacement::BuildLevelLayout(
			map.Probes, boxes, Slice<BBox>(source.ProbeRegions.pData, source.ProbeRegions.Size), options.Probes);
		map.ProbeSettings = options.Probes;

		result.ProbeCount = map.Probes.GetProbeCount();
		result.ProbeVolumeCount = map.Probes.VolumeCount;
	}

	LightmapBaker lightmap;

	result.bHasLightmap = lightmap.Bake(planes, source, surfaces, &patches, options.Lightmap, map);
	result.Lightmap = lightmap.Stats;

	const String leak_path = ReplaceExtension(result.OutputPath, ".leak");

	std::filesystem::remove(leak_path.CStr(), error);

	if (flood.bSealed) {
		result.Seal = eSealState::Sealed;
		log(String::Fmt("  seal         sealed{}", options.bKeepOutside ? ", faces towards the void kept" : ""));
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
			for (const Vec3d& point : flood.LeakPath) {
				std::fprintf(leak_file, "%.6f %.6f %.6f\n", point.X, point.Y, point.Z);
			}

			std::fclose(leak_file);
		}

		const Vec3d& from = flood.LeakPath[0];
		const Vec3d& to = GetLast(flood.LeakPath);

		log(String::Fmt("  seal         LEAKED, faces towards the void are kept. The path from the entity at ({:.2f}, "
						"{:.2f}, {:.2f}) to the void at ({:.2f}, {:.2f}, {:.2f}) is in {}",
						from.X, from.Y, from.Z, to.X, to.Y, to.Z, leak_path));
	}

	const SurfaceStats& stats = surfaces.Stats;
	result.Stats = stats;

	log(String::Fmt("  faces        {}, {} removed, {} clipped, {} overlaps cut", stats.NumSrcFaces, stats.FacesRemoved,
					stats.FacesClipped, stats.OverlapsRemoved));
	log(String::Fmt("  polygons     {} pieces merged into {}, {} T-junctions fixed", stats.NumPolygonsBeforeMerge,
					stats.NumPolygons, stats.TJunctionsFixed));

	if (stats.MergeFallbacks > 0) {
		log(String::Fmt("  warning      {} groups of faces could not be merged and were left as separate pieces",
						stats.MergeFallbacks));
	}

	log(String::Fmt("  triangles    {} -> {} ({:+.1f}%), {} vertices in {} surfaces", stats.NumSrcTriangles,
					stats.NumTriangles, -GetPercentRemoved(stats.NumSrcTriangles, stats.NumTriangles),
					stats.NumVertices, map.Surfaces.Size));
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

		if (source.ModelCount > 0) {
			log(String::Fmt(
				"  warning      {} models are not known to the compiler, so probes are not kept out of them",
				source.ModelCount));
		}
	}

	if (result.bHasLightmap) {
		const LightmapStats& baked = lightmap.Stats;

		log(String::Fmt("  lightmap     {}x{}, {} charts at {:.1f} texels per metre, {:.1f}% of {} texels lit, {} rays "
						"against {} brushes",
						baked.Width, baked.Height, baked.ChartCount, baked.Density, baked.LitFraction * 100.0,
						baked.TexelCount, baked.RayCount, baked.OccluderCount));

		if (baked.OccluderTriangleCount > 0) {
			log(String::Fmt("  lightmap     {} patch triangles also cast shadows", baked.OccluderTriangleCount));
		}

		if (baked.Density < options.Lightmap.Density) {
			log(String::Fmt("  warning      the lightmap did not fit in {}x{} at {:.1f} texels per metre, so it was "
							"baked at {:.1f}",
							options.Lightmap.MaxSize, options.Lightmap.MaxSize, options.Lightmap.Density,
							baked.Density));
		}

		if (baked.ProbeCount > 0) {
			log(String::Fmt("  sun probes   {} of {} light probes see the sun", baked.LitProbeCount, baked.ProbeCount));
		}
		else {
			log("  warning      there are no light probes, so models and other objects without a lightmap are not "
				"shadowed by the world");
		}

		if (source.ModelCount > 0) {
			log(String::Fmt("  warning      {} models are not known to the compiler, so they cast no baked shadows",
							source.ModelCount));
		}
	}
	else if (!options.Lightmap.bEnabled) {
		log("  lightmap     off");
	}
	else if (!source.bHasSun) {
		log("  lightmap     none, the prototype has no sun or it is switched off");
	}
	else if (source.Brushes.Size != 0) {
		log(String::Fmt("  warning      no lightmap, the surfaces do not fit in {}x{} texels", options.Lightmap.MaxSize,
						options.Lightmap.MaxSize));
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

	if (options.VerifyRays > 0 && result.bHasLightmap) {
		const LightmapVerifyResult verify = VerifyLightmap(planes, source, map, patches.ReferencePoints,
														   lightmap.Stats.Density, options.VerifyRays);

		log(String::Fmt("  verify       {} lightmap points, {} lit, {} skipped beside a shadow edge, {} wrong",
						verify.PointCount, verify.LitCount, verify.EdgeCount, verify.WrongCount));

		if (verify.WrongCount * scMaxWrongLightmapPoints > verify.PointCount) {
			log("  verify       FAILED, the lightmap does not match the sun");
			result.bVerifyFailed = true;
		}
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
