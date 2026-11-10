#include "MapSource.hpp"

#include <Asset/ConfigFile.hpp>
#include <BrushEntry.hpp>
#include <Core/HashMap.hpp>
#include <Core/String.hpp>
#include <LightEntry.hpp>
#include <Material/MaterialDefinition.hpp>
#include <Material/SpecialMaterial.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx::mapc {

struct CachedMaterials
{
	HashMap<String, bool> ModelColliders;
	HashMap<String, bool> TransparentMaterials;
	HashMap<String, bool> WaterMaterials;
};


namespace {

constexpr int32 scLockedMaterialIndex = 0;
constexpr int32 scEditableMaterialIndex = 1;

constexpr double scMinSunDistance = 1e-6;


struct Transform
{
	DVec3 Rows[3] = { DVec3(1.0, 0.0, 0.0), DVec3(0.0, 1.0, 0.0), DVec3(0.0, 0.0, 1.0) };
	DVec3 Pivot = DVec3(0.0);
	DVec3 Position = DVec3(0.0);

	DVec3 Rotate(const DVec3& vector) const { return Rows[0] * vector.X + Rows[1] * vector.Y + Rows[2] * vector.Z; }
	DVec3 Apply(const DVec3& point) const { return Rotate(point - Pivot) + Pivot + Position; }
};

Quat ReadRotation(ConfigEntry& entry)
{
	Quat rotation = Quat::scIdentity;

	ConfigEntry* euler_entry = entry.GetMember(HashStr32("rot"));

	if (euler_entry != nullptr) {
		rotation = Quat::FromEulerAngles(euler_entry->GetValue<FVec3>());
	}

	ConfigEntry* quat_entry = entry.GetMember(HashStr32("rotquat"));

	if (quat_entry != nullptr) {
		rotation = quat_entry->GetValue<Quat>();
	}

	return rotation;
}

Transform MakeTransform(const Quat rotation, const FVec3 pivot, const FVec3 position)
{
	Transform transform;
	transform.Pivot = DVec3(pivot);
	transform.Position = DVec3(position);

	const bool is_identity = (rotation.X == 0.0f && rotation.Y == 0.0f && rotation.Z == 0.0f);

	if (!is_identity) {
		const Mat4f matrix = Mat4f::AsRotation(rotation);

		for (int32 row = 0; row < 3; row++) {
			const DVec3 axis(matrix.Rows[row].X, matrix.Rows[row].Y, matrix.Rows[row].Z);
			transform.Rows[row] = axis.Normalized();
		}
	}

	return transform;
}

Mat4f MakeObjectMatrix(const Quat rotation, const FVec3 pivot, const FVec3 position)
{
	return Mat4f::AsScale(FVec3(1.0f)) * Mat4f::AsTranslation(-pivot) * Mat4f::AsRotation(rotation) *
		   Mat4f::AsTranslation(pivot) * Mat4f::AsTranslation(position);
}

void AddEntityPositions(const ConfigEntry* list, Array<DVec3>& out_entities)
{
	if (list == nullptr) {
		return;
	}

	for (const ConfigEntry& entry : list->Members) {
		const ConfigEntry* position = entry.GetMember(HashStr32("pos"));

		if (position != nullptr) {
			out_entities.Emplace(DVec3(position->GetValue<FVec3>()));
		}
	}
}

DVec3 GetLightColor(const Color& color, const double intensity)
{
	float32 linear[3];
	color.GetLinearRGB(linear);

	return DVec3(linear[0], linear[1], linear[2]) * intensity;
}

void AddLights(const ConfigEntry* list, Array<SourceLight>& out_lights)
{
	if (list == nullptr) {
		return;
	}

	for (const ConfigEntry& entry : list->Members) {
		const ConfigEntry* position = entry.GetMember(HashStr32("pos"));

		if (position == nullptr) {
			continue;
		}

		const LightEntry source = ReadLightEntry(entry);
		const double radius = source.Radius;

		SourceLight light;
		light.Position = DVec3(source.Position);
		light.Color = GetLightColor(source.Color, source.Intensity);
		light.InvRadiusSq = 1.0 / std::max(radius * radius, 1e-8);
		light.bSpot = (source.Type == ePrxLightType::Spot);

		if (light.bSpot) {
			const DVec3 direction = DVec3(source.Spot.Direction);

			if (direction.Length() > scMinSunDistance) {
				light.Direction = direction.Normalized();
			}

			constexpr double scHalfTurnDegrees = 180.0;

			const double max_angle = scHalfTurnDegrees * 0.5;
			const double outer = std::clamp<double>(source.Spot.OuterAngle, 0.0, max_angle);
			const double inner = std::clamp<double>(source.Spot.InnerAngle, 0.0, outer);

			const double cos_inner = std::cos(inner * (M_PI / scHalfTurnDegrees));
			const double cos_outer = std::cos(outer * (M_PI / scHalfTurnDegrees));

			light.SpotCosOuter = cos_outer;
			light.SpotAngleScale = 1.0 / std::max(cos_inner - cos_outer, 1e-4);
		}

		out_lights.Emplace(light);
	}
}

void ReadMaterialFlags(CachedMaterials& out_materials)
{
	for (const MaterialDefinition& definition : LoadMaterialDefinitions(scMaterialRoot)) {
		if (FindSpecialMaterial(definition.Name.CStr()) != eSpecialMaterial::None) {
			continue;
		}

		if (definition.IsTransparent()) {
			out_materials.TransparentMaterials.Insert(definition.Name, true);
		}

		if (definition.bWater) {
			out_materials.WaterMaterials.Insert(definition.Name, true);
		}
	}
}

} // namespace

const TextureMap* SourceBrush::FindMap(int32 plane_index, int32& out_material) const
{
	for (size_t i = 0; i < PlaneIndices.Size; i++) {
		if (PlaneIndices[i] == plane_index) {
			out_material = FaceMaterials[i];
			return &Maps[i];
		}
	}

	return nullptr;
}

void MapSource::AddModels(const ConfigEntry* models_entry, const ConfigEntry* brushes)
{
	if (models_entry == nullptr || !models_entry->Members.IsInited()) {
		Models.InitCapacity(0);
		return;
	}

	Models.InitCapacity(models_entry->Members.Size());

	for (const ConfigEntry& entry : models_entry->Members) {
		const ConfigEntry* mesh = entry.GetMember(HashStr32("mesh"));

		if (mesh == nullptr || mesh->Type != ConfigEntry::ePrimitiveType::String) {
			continue;
		}

		const FVec3 position = entry.GetMemberValue(HashStr32("pos"), FVec3(0.0f));
		Quat rotation = entry.GetMemberValue(HashStr32("rot"), Quat::scIdentity);
		rotation = entry.GetMemberValue(HashStr32("rotquat"), rotation);
		const float32 scale = entry.GetMemberValue(HashStr32("scale"), 1.0f);

		SourceModel& model = Models.Emplace();
		model.Name = entry.Name.Get();
		model.MeshPath = mesh->Get<const char*>();
		model.Transform = Mat4f::AsScale(FVec3(scale)) * Mat4f::AsRotation(rotation) * Mat4f::AsTranslation(position);
		model.bBaked = IsBakedModelEntry(entry, brushes);
		model.bProbeBox = entry.GetMemberValue(HashStr32("unlit"), 0) == 0 &&
						  entry.GetMemberValue(HashStr32("noprobe"), 0) == 0;
	}
}

int32 MapSource::FindOrAddMaterial(const String& name, int32 library_index, const CachedMaterials& mc)
{
	for (size_t i = 0; i < Materials.Size; i++) {
		if (Materials[i].Name == name && Materials[i].LibraryIndex == library_index) {
			return static_cast<int32>(i);
		}
	}

	Materials.Emplace(CompiledMapMaterial { name, library_index });
	OpaqueMaterials.Emplace(static_cast<uint8>(!mc.TransparentMaterials.Contains(name)));
	WaterMaterials.Emplace(static_cast<uint8>(mc.WaterMaterials.Contains(name)));

	return static_cast<int32>(Materials.Size - 1);
}


void MapSource::AddPatch(ConfigEntry& entry, const String& name, const CachedMaterials& mc)
{
	if (IsEntityBrushEntry(entry) || mc.ModelColliders.Contains(name)) {
		EntityBrushCount++;
		return;
	}

	Patch patch = ReadPatchEntry(entry);

	if (!patch.IsValid()) {
		Warnings.Emplace(String::Fmt("patch '{}' is not a valid patch, skipped", name));
		InvalidBrushCount++;
		return;
	}

	const FVec3 position = entry.GetMemberValue<FVec3>(HashStr32("pos"), FVec3::sZero);
	const Transform transform = MakeTransform(ReadRotation(entry), patch.GetCenter(), position);

	for (PatchPoint& point : patch.Points) {
		const DVec3 world = transform.Apply(DVec3(point.Position));

		point.Position = world.ToFVec3();

		Min = MinVec(Min, world);
		Max = MaxVec(Max, world);
	}

	const ConfigEntry* material_entry = entry.GetMember(HashStr32("mat"));
	String material_name;

	if (material_entry != nullptr && material_entry->Type == ConfigEntry::ePrimitiveType::String) {
		material_name = material_entry->Get<const char*>();
	}

	const bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	const bool is_transparent = !material_name.IsEmpty() && mc.TransparentMaterials.Contains(material_name);

	int32 library_index = scEditableMaterialIndex;
	if (is_locked) {
		library_index = scLockedMaterialIndex;
	}

	const int32 material_index = FindOrAddMaterial(material_name, library_index, mc);

	// Ignore transparent surfaces as they are excempt from probe baking
	if (!is_transparent) {
		ProbePlacement::AddPatchSurfaces(ProbeBoxes, patch, Mat4f::scIdentity);
	}

	Patch pieces[Patch::scMaxClosurePieces];
	const uint32 piece_count = patch.BuildClosure(pieces);

	for (uint32 piece = 0; piece <= piece_count; piece++) {
		SourcePatch& source = Patches.Emplace();

		source.Name = name;
		source.MaterialIndex = material_index;
		source.bCastsShadow = !is_transparent && entry.GetMemberValue(HashStr32("shadows"), 1) != 0;

		if (piece == 0) {
			source.Shape = patch.Clone();
			source.Shape.Closure = ePatchClosure::None;
		}
		else {
			source.Shape = std::move(pieces[piece - 1]);

			for (const PatchPoint& point : source.Shape.Points) {
				Min = MinVec(Min, DVec3(point.Position));
				Max = MaxVec(Max, DVec3(point.Position));
			}
		}
	}

	PatchCount++;
}

void MapSource::AddBrush(PlaneSet& planes, ConfigEntry& entry, const String& name, const CachedMaterials& mc)
{
	const Brush brush = ReadBrushEntry(entry);

	if (!brush.IsValid()) {
		Warnings.Emplace(String::Fmt("brush '{}' does not describe a valid convex brush, skipped", name));
		InvalidBrushCount++;
		return;
	}

	const FVec3 position = entry.GetMemberValue<FVec3>(HashStr32("pos"), FVec3::sZero);
	const Quat rotation = ReadRotation(entry);
	const Transform transform = MakeTransform(rotation, brush.GetCenter(), position);

	const String material_name = ReadBrushMaterialName(entry);
	const eSpecialMaterial special = FindSpecialMaterial(material_name.CStr());

	const bool is_probe_volume = entry.GetMemberValue(HashStr32("probevolume"), 0) != 0;
	const bool is_reflection_probe = entry.GetMemberValue(HashStr32("reflectionprobe"), 0) != 0;
	const bool is_marker = is_probe_volume || is_reflection_probe || IsDecalEntry(entry) ||
						   entry.GetMemberValue(HashStr32("trigger"), 0) != 0;

	const bool is_transparent = !IsDecalEntry(entry) && mc.TransparentMaterials.Contains(material_name);

	const BBox local_bounds(brush.GetBoundsMin(), brush.GetBoundsMax());
	const Mat4f object_matrix = MakeObjectMatrix(rotation, brush.GetCenter(), position);

	if (is_probe_volume && !is_reflection_probe) {
		ProbeRegions.Emplace(OBBox::FromLocalBounds(local_bounds, object_matrix).GetWorldAABB());
	}
	else if (!is_marker && !is_transparent && special != eSpecialMaterial::NvBrush &&
			 special != eSpecialMaterial::Portal) {
		SizedArray<ProbePlacementBoxes::Plane> hull_planes;

		uint32 hull_plane_count = 0;
		if (!brush.IsBox()) {
			hull_plane_count = brush.Planes.Size;
		}

		hull_planes.InitCapacity(hull_plane_count);

		for (uint32 i = 0; i < hull_planes.Capacity; i++) {
			hull_planes.Insert({ brush.Planes[i].Normal, brush.Planes[i].Distance });
		}

		if (special == eSpecialMaterial::DBound) {
			ProbePlacement::AddBound(ProbeBoxes, local_bounds, object_matrix, hull_planes);
		}
		else {
			ProbePlacement::AddBox(ProbeBoxes, local_bounds, object_matrix, hull_planes);
		}
	}

	if (IsEntityBrushEntry(entry) || mc.ModelColliders.Contains(name)) {
		EntityBrushCount++;

		if (!is_marker) {
			Entities.Emplace(transform.Apply(transform.Pivot));
		}

		return;
	}

	SourceBrush source;
	source.Name = name;

	const bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;

	int32 library_index = scEditableMaterialIndex;
	if (is_locked) {
		library_index = scLockedMaterialIndex;
	}

	switch (special) {
	case eSpecialMaterial::NvBrush:
		source.bVisible = false;
		source.bSolid = false;
		ClipBrushCount++;
		break;
	case eSpecialMaterial::DBound:
		source.bVisible = false;
		source.bCollides = false;
		BoundBrushCount++;
		break;
	case eSpecialMaterial::Portal:
		source.bVisible = false;
		source.bCollides = false;
		source.bSolid = false;
		source.bCastsShadow = false;
		source.bPortal = true;
		PortalBrushCount++;
		break;
	case eSpecialMaterial::NonVis:
		break;
	default:
		source.MaterialIndex = FindOrAddMaterial(material_name, library_index, mc);

		if (is_transparent) {
			source.bSolid = false;
			source.bCastsShadow = false;
			TransparentBrushCount++;
		}

		// Water can be walked into, unlike glass
		if (mc.WaterMaterials.Contains(material_name)) {
			source.bCollides = false;
			source.bWater = true;
		}
		break;
	}

	if (entry.GetMemberValue(HashStr32("shadows"), 1) == 0) {
		source.bCastsShadow = false;
	}

	const auto get_face_material = [&](const BrushPlane& plane)
	{
		const String* face_material = &plane.Material;
		if (plane.Material.IsEmpty()) {
			face_material = &material_name;
		}

		switch (FindSpecialMaterial(face_material->CStr())) {
		case eSpecialMaterial::NonVis:
		case eSpecialMaterial::NvBrush:
		case eSpecialMaterial::DBound:
		case eSpecialMaterial::Portal:
			return SourceBrush::scDroppedFace;
		default:
			break;
		}

		if (plane.Material.IsEmpty()) {
			return source.MaterialIndex;
		}

		return FindOrAddMaterial(plane.Material, library_index, mc);
	};

	const DVec3 offset = transform.Pivot + transform.Position;

	for (const BrushPlane& plane : brush.Planes) {
		const DVec3 local_normal = DVec3(plane.Normal);
		const DVec3 rotated_normal = transform.Rotate(local_normal);
		const double length = rotated_normal.Length();

		if (length <= 0.0) {
			continue;
		}

		const DVec3 world_normal = rotated_normal * (1.0 / length);
		const double world_distance = (static_cast<double>(plane.Distance) - local_normal.Dot(transform.Pivot)) /
										  length +
									  world_normal.Dot(offset);

		FVec3 u_gradient;
		FVec3 v_gradient;
		Brush::GetTextureGradients(plane, u_gradient, v_gradient);

		TextureMap map;
		map.GradientU = transform.Rotate(DVec3(u_gradient));
		map.GradientV = transform.Rotate(DVec3(v_gradient));
		map.OffsetU = static_cast<double>(plane.Texture.Offset.X) + DVec3(u_gradient).Dot(transform.Pivot) -
					  map.GradientU.Dot(offset);
		map.OffsetV = static_cast<double>(plane.Texture.Offset.Y) + DVec3(v_gradient).Dot(transform.Pivot) -
					  map.GradientV.Dot(offset);

		source.PlaneIndices.Emplace(planes.FindOrAdd(world_normal, world_distance));
		source.Maps.Emplace(map);
		source.FaceMaterials.Emplace(get_face_material(plane));

		if (source.bVisible && GetLast(source.FaceMaterials) == SourceBrush::scDroppedFace) {
			NonVisFaceCount++;
		}
	}

	constexpr double scBrushExtent = 1e5;

	if (!MakePolyhedron(planes, source.PlaneIndices, scBrushExtent, source.Shape)) {
		Warnings.Emplace(String::Fmt("brush '{}' has no volume, skipped", name));
		InvalidBrushCount++;
		return;
	}

	source.Shape.Source = static_cast<int32>(Brushes.Size);

	Min = MinVec(Min, source.Shape.Min);
	Max = MaxVec(Max, source.Shape.Max);

	Brushes.Emplace(std::move(source));
}

bool MapSource::Load(const String& path, PlaneSet& planes)
{
	ConfigFile info {};
	info.Load(path.CStr());

	if (info.HasErrors()) {
		return false;
	}

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	if (blocks_entry == nullptr) {
		Warnings.InitCapacity(1);
		Warnings.Emplace("the prototype has no 'all' entry");
		return false;
	}

	const uint32 renamed_count = MakeBrushEntryNamesUnique(*blocks_entry);

	const ConfigEntry* models_entry = info.GetEntry(HashStr32("objects"));

	CachedMaterials cached_materials {};


	if (models_entry != nullptr) {
		for (const ConfigEntry& model : models_entry->Members) {
			const ConfigEntry* collider = model.GetMember(HashStr32("collider"));

			if (collider != nullptr && collider->Type == ConfigEntry::ePrimitiveType::String) {
				cached_materials.ModelColliders.Insert(String(collider->Get<const char*>()), true);
			}
		}
	}

	ReadMaterialFlags(cached_materials);

	const size_t entry_count = blocks_entry->Members.Size() + CountSubviewBrushes(info, path);

	Brushes.InitCapacity(entry_count);
	Materials.InitCapacity(entry_count * (Brush::scMaxPlanes + 1));
	Warnings.InitCapacity(entry_count + 1);

	if (renamed_count > 0) {
		Warnings.Emplace(String::Fmt("{} brushes shared a name with another brush and were renamed, save the "
									 "prototype from the editor to keep the new names",
									 renamed_count));
	}

	Min = DVec3(DBL_MAX);
	Max = DVec3(-DBL_MAX);

	Patches.InitCapacity(entry_count);

	for (ConfigEntry& entry : blocks_entry->Members) {
		const String name = entry.Name.Get();

		if (IsPatchEntry(entry)) {
			AddPatch(entry, name, cached_materials);
		}
		else {
			AddBrush(planes, entry, name, cached_materials);
		}
	}

	ForEachSubviewBrush(info, path,
						[&](ConfigEntry& entry, const ConfigEntry&, uint32)
						{
							const String name = entry.Name.Get();
							AddBrush(planes, entry, name, cached_materials);
						});

	const ConfigEntry* spawn_entry = info.GetEntry(HashStr32("player_spawn"));

	if (spawn_entry != nullptr && spawn_entry->GetMember(HashStr32("pos")) != nullptr) {
		PlayerSpawn = DVec3(spawn_entry->GetMember(HashStr32("pos"))->GetValue<FVec3>());
		bHasPlayerSpawn = true;

		Entities.Emplace(PlayerSpawn);
	}

	const ConfigEntry* sun_entry = info.GetEntry(HashStr32("sun"));

	if (sun_entry != nullptr) {
		const SunLightEntry sun = ReadSunLightEntry(*sun_entry);
		const DVec3 sun_position = DVec3(sun.Position);

		if (sun.bEnabled && sun_position.Length() > scMinSunDistance) {
			SunDirection = sun_position.Normalized();
			SunColor = GetLightColor(sun.Color, sun.Intensity);
			bHasSun = true;
		}
	}

	AddLights(info.GetEntry(HashStr32("lights")), Lights);
	AddEntityPositions(info.GetEntry(HashStr32("lights")), Entities);
	AddEntityPositions(models_entry, Entities);

	AddModels(models_entry, blocks_entry);

	return true;
}

} // namespace fx::mapc
