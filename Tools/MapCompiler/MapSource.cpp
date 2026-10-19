#include "MapSource.hpp"

#include <Asset/ConfigFile.hpp>
#include <BrushEntry.hpp>
#include <Core/String.hpp>
#include <Material/MaterialDefinition.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <cfloat>
#include <unordered_set>

namespace fx::mapc {

struct CachedMaterials
{
	std::unordered_set<String> ModelColliders;
	std::unordered_set<String> TransparentMaterials;
	std::unordered_set<String> CaulkMaterials;
};


namespace {

constexpr int32 scLockedMaterialIndex = 0;
constexpr int32 scEditableMaterialIndex = 1;

constexpr const char* scMaterialRoot = "RaptorData/Data/materials";


constexpr double scBrushExtent = 1e5;
constexpr double scMinSunDistance = 1e-6;


struct Transform
{
	Vec3d Rows[3] = { Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec3d(0.0, 0.0, 1.0) };
	Vec3d Pivot = Vec3d(0.0);
	Vec3d Position = Vec3d(0.0);

	Vec3d Rotate(const Vec3d& vector) const { return Rows[0] * vector.X + Rows[1] * vector.Y + Rows[2] * vector.Z; }
	Vec3d Apply(const Vec3d& point) const { return Rotate(point - Pivot) + Pivot + Position; }
};

Vec3d ToVec3d(const Vec3f vector) { return Vec3d(vector.X, vector.Y, vector.Z); }

Quat ReadRotation(ConfigEntry& entry)
{
	Quat rotation = Quat::scIdentity;

	ConfigEntry* euler_entry = entry.GetMember(HashStr32("rot"));

	if (euler_entry != nullptr) {
		rotation = Quat::FromEulerAngles(euler_entry->GetValue<Vec3f>());
	}

	ConfigEntry* quat_entry = entry.GetMember(HashStr32("rotquat"));

	if (quat_entry != nullptr) {
		rotation = quat_entry->GetValue<Quat>();
	}

	return rotation;
}

Transform MakeTransform(const Quat rotation, const Vec3f pivot, const Vec3f position)
{
	Transform transform;
	transform.Pivot = ToVec3d(pivot);
	transform.Position = ToVec3d(position);

	const bool is_identity = (rotation.X == 0.0f && rotation.Y == 0.0f && rotation.Z == 0.0f);

	if (!is_identity) {
		const Mat4f matrix = Mat4f::AsRotation(rotation);

		for (int32 row = 0; row < 3; row++) {
			const Vec3d axis(matrix.Rows[row].X, matrix.Rows[row].Y, matrix.Rows[row].Z);
			transform.Rows[row] = axis * (1.0 / axis.Length());
		}
	}

	return transform;
}

Mat4f MakeObjectMatrix(const Quat rotation, const Vec3f pivot, const Vec3f position)
{
	return Mat4f::AsScale(Vec3f(1.0f)) * Mat4f::AsTranslation(-pivot) * Mat4f::AsRotation(rotation) *
		   Mat4f::AsTranslation(pivot) * Mat4f::AsTranslation(position);
}

void AddEntityPositions(const ConfigEntry* list, Array<Vec3d>& out_entities)
{
	if (list == nullptr) {
		return;
	}

	for (const ConfigEntry& entry : list->Members) {
		const ConfigEntry* position = entry.GetMember(HashStr32("pos"));

		if (position != nullptr) {
			out_entities.Emplace(ToVec3d(position->GetValue<Vec3f>()));
		}
	}
}

void ReadMaterialFlags(CachedMaterials& out_materials)
{
	for (const MaterialDefinition& definition : LoadMaterialDefinitions(scMaterialRoot)) {
		if (definition.IsTransparent()) {
			out_materials.TransparentMaterials.insert(definition.Name);
		}

		if (definition.bCaulk) {
			out_materials.CaulkMaterials.insert(definition.Name);
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

int32 MapSource::FindOrAddMaterial(const String& name, int32 library_index, const CachedMaterials& mc)
{
	for (size_t i = 0; i < Materials.Size; i++) {
		if (Materials[i].Name == name && Materials[i].LibraryIndex == library_index) {
			return static_cast<int32>(i);
		}
	}

	Materials.Emplace(CompiledMapMaterial { name, library_index });
	OpaqueMaterials.Emplace(static_cast<uint8>(!mc.TransparentMaterials.contains(name)));

	return static_cast<int32>(Materials.Size - 1);
}


void MapSource::AddPatch(ConfigEntry& entry, const String& name, const CachedMaterials& mc)
{
	if (IsEntityBrushEntry(entry) || mc.ModelColliders.contains(name)) {
		EntityBrushCount++;
		return;
	}

	Patch patch = ReadPatchEntry(entry);

	if (!patch.IsValid()) {
		Warnings.Emplace(String::Fmt("patch '{}' is not a valid patch, skipped", name));
		InvalidBrushCount++;
		return;
	}

	const Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
	const Transform transform = MakeTransform(ReadRotation(entry), patch.GetCenter(), position);

	for (PatchPoint& point : patch.Points) {
		const Vec3d world = transform.Apply(ToVec3d(point.Position));

		point.Position = world.ToVec3f();

		Min = MinVec(Min, world);
		Max = MaxVec(Max, world);
	}

	const ConfigEntry* material_entry = entry.GetMember(HashStr32("mat"));
	String material_name;

	if (material_entry != nullptr && material_entry->Type == ConfigEntry::ePrimitiveType::String) {
		material_name = material_entry->Get<const char*>();
	}

	const bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	const bool is_transparent = !material_name.IsEmpty() && mc.TransparentMaterials.contains(material_name);

	const int32 material_index = FindOrAddMaterial(material_name,
												   is_locked ? scLockedMaterialIndex : scEditableMaterialIndex, mc);

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
		source.bCastsShadow = !is_transparent;

		if (piece == 0) {
			source.Shape = patch.Clone();
			source.Shape.Closure = ePatchClosure::None;
		}
		else {
			source.Shape = std::move(pieces[piece - 1]);

			for (const PatchPoint& point : source.Shape.Points) {
				Min = MinVec(Min, ToVec3d(point.Position));
				Max = MaxVec(Max, ToVec3d(point.Position));
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

	const Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
	const Quat rotation = ReadRotation(entry);
	const Transform transform = MakeTransform(rotation, brush.GetCenter(), position);

	const ConfigEntry* material_entry = entry.GetMember(HashStr32("mat"));
	const String material_name = (material_entry != nullptr &&
								  material_entry->Type == ConfigEntry::ePrimitiveType::String)
									 ? material_entry->Get<const char*>()
									 : "";

	const eBrushKind brush_kind = ReadBrushKind(entry);

	const bool is_probe_volume = entry.GetMemberValue(HashStr32("probevolume"), 0) != 0;
	const bool is_reflection_probe = entry.GetMemberValue(HashStr32("reflectionprobe"), 0) != 0;
	const bool is_marker = is_probe_volume || is_reflection_probe || brush_kind == eBrushKind::Decal ||
						   entry.GetMemberValue(HashStr32("trigger"), 0) != 0;

	const bool is_transparent = brush_kind == eBrushKind::Geometry && !material_name.IsEmpty() &&
								mc.TransparentMaterials.contains(material_name);

	const BBox local_bounds(brush.GetBoundsMin(), brush.GetBoundsMax());
	const Mat4f object_matrix = MakeObjectMatrix(rotation, brush.GetCenter(), position);

	if (is_probe_volume && !is_reflection_probe) {
		ProbeRegions.Emplace(OBBox::FromLocalBounds(local_bounds, object_matrix).GetWorldAABB());
	}
	else if (!is_marker && !is_transparent && brush_kind != eBrushKind::NvBrush) {
		SizedArray<ProbePlacementBoxes::Plane> hull_planes;
		hull_planes.InitCapacity(brush.IsBox() ? 0 : brush.Planes.Size);

		for (uint32 i = 0; i < hull_planes.Capacity; i++) {
			hull_planes.Insert({ brush.Planes[i].Normal, brush.Planes[i].Distance });
		}

		if (brush_kind == eBrushKind::DBound) {
			ProbePlacement::AddBound(ProbeBoxes, local_bounds, object_matrix, hull_planes);
		}
		else {
			ProbePlacement::AddBox(ProbeBoxes, local_bounds, object_matrix, hull_planes);
		}
	}

	if (IsEntityBrushEntry(entry) || mc.ModelColliders.contains(name)) {
		EntityBrushCount++;

		if (!is_marker) {
			Entities.Emplace(transform.Apply(transform.Pivot));
		}

		return;
	}

	SourceBrush source;
	source.Name = name;

	const bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;

	if (brush_kind == eBrushKind::NvBrush) {
		source.bVisible = false;
		source.bSolid = false;
		ClipBrushCount++;
	}
	else if (brush_kind == eBrushKind::DBound) {
		source.bVisible = false;
		source.bCollides = false;
		BoundBrushCount++;
	}
	else if (!material_name.IsEmpty()) {
		source.MaterialIndex = FindOrAddMaterial(material_name,
												 is_locked ? scLockedMaterialIndex : scEditableMaterialIndex, mc);

		if (is_transparent) {
			source.bSolid = false;
			source.bCastsShadow = false;
			TransparentBrushCount++;
		}
	}
	else {
		source.MaterialIndex = FindOrAddMaterial("", is_locked ? scLockedMaterialIndex : scEditableMaterialIndex, mc);
	}

	const int32 library_index = is_locked ? scLockedMaterialIndex : scEditableMaterialIndex;
	const bool is_caulk = mc.CaulkMaterials.contains(material_name);

	const auto get_face_material = [&](const BrushPlane& plane)
	{
		if (plane.Material.IsEmpty()) {
			if (is_caulk) {
				return SourceBrush::scDroppedFace;
			}

			return source.MaterialIndex;
		}

		if (mc.CaulkMaterials.contains(plane.Material)) {
			return SourceBrush::scDroppedFace;
		}

		return FindOrAddMaterial(plane.Material, library_index, mc);
	};

	const Vec3d offset = transform.Pivot + transform.Position;

	for (const BrushPlane& plane : brush.Planes) {
		const Vec3d local_normal = ToVec3d(plane.Normal);
		const Vec3d rotated_normal = transform.Rotate(local_normal);
		const double length = rotated_normal.Length();

		if (length <= 0.0) {
			continue;
		}

		const Vec3d world_normal = rotated_normal * (1.0 / length);
		const double world_distance = (static_cast<double>(plane.Distance) - local_normal.Dot(transform.Pivot)) /
										  length +
									  world_normal.Dot(offset);

		Vec3f u_gradient;
		Vec3f v_gradient;
		Brush::GetTextureGradients(plane, u_gradient, v_gradient);

		TextureMap map;
		map.GradientU = transform.Rotate(ToVec3d(u_gradient));
		map.GradientV = transform.Rotate(ToVec3d(v_gradient));
		map.OffsetU = static_cast<double>(plane.Texture.Offset.X) + ToVec3d(u_gradient).Dot(transform.Pivot) -
					  map.GradientU.Dot(offset);
		map.OffsetV = static_cast<double>(plane.Texture.Offset.Y) + ToVec3d(v_gradient).Dot(transform.Pivot) -
					  map.GradientV.Dot(offset);

		source.PlaneIndices.Emplace(planes.FindOrAdd(world_normal, world_distance));
		source.Maps.Emplace(map);
		source.FaceMaterials.Emplace(get_face_material(plane));

		if (source.bVisible && GetLast(source.FaceMaterials) == SourceBrush::scDroppedFace) {
			CaulkFaceCount++;
		}
	}

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
				cached_materials.ModelColliders.insert(collider->Get<const char*>());
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

	Min = Vec3d(DBL_MAX);
	Max = Vec3d(-DBL_MAX);

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
		PlayerSpawn = ToVec3d(spawn_entry->GetMember(HashStr32("pos"))->GetValue<Vec3f>());
		bHasPlayerSpawn = true;

		Entities.Emplace(PlayerSpawn);
	}

	const ConfigEntry* sun_entry = info.GetEntry(HashStr32("sun"));

	if (sun_entry != nullptr && sun_entry->GetMemberValue<int64>(HashStr32("enabled"), 1) != 0) {
		const Vec3d sun_position = ToVec3d(sun_entry->GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero));

		if (sun_position.Length() > scMinSunDistance) {
			SunDirection = sun_position.Normalized();
			bHasSun = true;
		}
	}

	AddEntityPositions(info.GetEntry(HashStr32("lights")), Entities);
	AddEntityPositions(models_entry, Entities);

	if (models_entry != nullptr) {
		ModelCount = static_cast<uint32>(models_entry->Members.Size());
	}

	return true;
}

} // namespace fx::mapc
