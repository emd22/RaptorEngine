#include "AcousticBake.hpp"

#include <Audio/Acoustics.hpp>
#include <Map/CompiledMap.hpp>
#include <Material/MaterialDefinition.hpp>
#include <algorithm>
#include <unordered_map>

namespace fx::mapc {

namespace {

constexpr const char* scMaterialRoot = "RaptorData/Data/materials";
constexpr const char* scLockedMaterialName = "proto_white";
constexpr const char* scEditableMaterialName = "proto_grey";
constexpr int32 scEditableLibraryIndex = 1;
constexpr int32 scProgressSteps = 4;

String GetMaterialName(const CompiledMapMaterial& material)
{
	if (!material.Name.IsEmpty()) {
		return material.Name;
	}

	if (material.LibraryIndex == scEditableLibraryIndex) {
		return String(scEditableMaterialName);
	}

	return String(scLockedMaterialName);
}

} // namespace

bool BakeAcoustics(const AcousticOptions& options, CompiledMap& map, AcousticStats& out_stats,
				   const std::function<void(const String&)>& log)
{
	out_stats = AcousticStats {};
	map.AcousticProbes.Free();

	if (!options.bEnabled || map.Surfaces.Size == 0) {
		return false;
	}

	std::unordered_map<std::string, AcousticSurface> surfaces;

	for (const MaterialDefinition& definition : LoadMaterialDefinitions(scMaterialRoot)) {
		surfaces[definition.Name.CStr()] = AcousticSurface { .Name = definition.Surface, .Density = definition.Density };
	}

	AcousticGeometry geometry;

	for (size_t i = 0; i < map.Materials.Size; i++) {
		const auto found = surfaces.find(GetMaterialName(map.Materials[i]).CStr());
		AcousticSurface surface;

		if (found != surfaces.end()) {
			surface = found->second;
		}

		geometry.Surfaces.push_back(surface);
	}

	Vec3f bounds_min = map.Surfaces[0].Bounds.Min;
	Vec3f bounds_max = map.Surfaces[0].Bounds.Max;

	for (const CompiledMapSurface& surface : map.Surfaces) {
		bounds_min = Vec3f::Min(bounds_min, surface.Bounds.Min);
		bounds_max = Vec3f::Max(bounds_max, surface.Bounds.Max);

		for (size_t i = 0; i + 2 < surface.Indices.Size; i += 3) {
			const CompiledMapVertex& a = surface.Vertices[surface.Indices[i]];
			Vec3f b = surface.Vertices[surface.Indices[i + 1]].Position;
			Vec3f c = surface.Vertices[surface.Indices[i + 2]].Position;

			if ((b - a.Position).Cross(c - a.Position).Dot(a.Normal) < 0.0f) {
				std::swap(b, c);
			}

			geometry.AddTriangle(a.Position, b, c, surface.MaterialIndex);
		}
	}

	AcousticBakeSettings settings;
	settings.BoundsMin = bounds_min;
	settings.BoundsMax = bounds_max;
	settings.ProbeSpacing = static_cast<float32>(options.ProbeSpacing);
	settings.ProbeHeight = static_cast<float32>(options.ProbeHeight);
	settings.ReverbRays = static_cast<int32>(options.ReverbRays);
	settings.ReverbBounces = static_cast<int32>(options.ReverbBounces);
	settings.bPathing = options.bPathing;

	const char* last_stage = "";
	int32 last_step = -1;

	const auto progress = [&](const char* stage, float32 fraction)
	{
		const int32 step = static_cast<int32>(fraction * scProgressSteps);

		if (stage == last_stage && step <= last_step) {
			return;
		}

		last_stage = stage;
		last_step = step;

		log(String::Fmt("  acoustics    baking {} {:.0f}%", stage, fraction * 100.0f));
	};

	AcousticBakeResult result;

	if (!Acoustics::Bake(geometry, settings, result, progress)) {
		out_stats.Error = result.Error;
		return false;
	}

	map.AcousticProbes.InitSize(result.Data.size());
	std::copy(result.Data.begin(), result.Data.end(), map.AcousticProbes.pData);

	out_stats.bBaked = true;
	out_stats.bPathing = result.bHasPathing;
	out_stats.ProbeCount = result.ProbeCount;
	out_stats.ProbeMin = result.ProbeMin;
	out_stats.ProbeMax = result.ProbeMax;
	out_stats.DataSize = result.Data.size();
	out_stats.ReflectionSeconds = result.ReflectionSeconds;
	out_stats.PathingSeconds = result.PathingSeconds;

	return true;
}

} // namespace fx::mapc
