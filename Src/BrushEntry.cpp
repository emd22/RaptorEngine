#include "BrushEntry.hpp"

#include <Asset/ConfigFile.hpp>
#include <Map/CompiledMap.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fx {

eBrushKind ReadBrushKind(const ConfigEntry& entry)
{
	const ConfigEntry* material = entry.GetMember(HashStr32("mat"));
	const char* material_name = (material != nullptr && material->Type == ConfigEntry::ePrimitiveType::String)
									? material->Get<const char*>()
									: "";

	if (entry.GetMemberValue(HashStr32(scNvBrushName), 0) != 0 || std::strcmp(material_name, scNvBrushName) == 0) {
		return eBrushKind::NvBrush;
	}

	if (entry.GetMemberValue(HashStr32(scDBoundName), 0) != 0 || std::strcmp(material_name, scDBoundName) == 0) {
		return eBrushKind::DBound;
	}

	if (entry.GetMemberValue(HashStr32(scDecalName), 0) != 0) {
		return eBrushKind::Decal;
	}

	return eBrushKind::Geometry;
}

/// Values stored per plane in a blockout's `uvs` array: offset U and V, scale U and V, then rotation
constexpr uint32 scValuesPerFaceTexture = 5;

constexpr float32 scMinFaceTextureScale = 1e-4f;

static void ReadFaceMaterials(ConfigEntry& entry, Brush::PlaneList& planes)
{
	ConfigEntry* materials_entry = entry.GetMember(HashStr32(scFaceMaterialsName));

	if (materials_entry == nullptr) {
		return;
	}

	const PagedArray<ConfigPrimitive>& materials = materials_entry->GetArrayData();

	for (uint32 i = 0; i < planes.Size && i < materials.Size(); i++) {
		if (materials[i].Type != ConfigPrimitive::ePrimitiveType::String) {
			continue;
		}

		const char* name = materials[i].Get<const char*>();

		if (name != nullptr && std::strcmp(name, scBrushMaterialName) != 0) {
			planes[i].Material = name;
		}
	}
}

void WriteFaceMaterials(ConfigEntry& entry, const Brush& brush)
{
	if (!brush.HasFaceMaterials()) {
		return;
	}

	ConfigEntry materials_array = ConfigEntry::Array(scFaceMaterialsName, ConfigPrimitive::ePrimitiveType::String);

	for (const BrushPlane& plane : brush.Planes) {
		if (plane.Material.IsEmpty()) {
			materials_array.AppendValue(ConfigPrimitive::FromValue(scBrushMaterialName));
		}
		else {
			materials_array.AppendValue(ConfigPrimitive::FromValue(plane.Material.CStr()));
		}
	}

	entry.AddMember(std::move(materials_array));
}

Brush ReadBrushEntry(ConfigEntry& entry)
{
	ConfigEntry* scale_entry = entry.GetMember(HashStr32("scale"));

	if (scale_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& scales = scale_entry->GetArrayData();

		if (scales.Size() < 6) {
			return {};
		}

		// Left, right, top, bottom, front, back extents
		const Vec3f min = -Vec3f(scales[0].Get<float32>(), scales[3].Get<float32>(), scales[5].Get<float32>());
		const Vec3f max = Vec3f(scales[1].Get<float32>(), scales[2].Get<float32>(), scales[4].Get<float32>());

		return Brush::FromBox(min, max);
	}

	ConfigEntry* planes_entry = entry.GetMember(HashStr32("planes"));

	if (planes_entry == nullptr) {
		return {};
	}

	const PagedArray<ConfigPrimitive>& values = planes_entry->GetArrayData();

	// Four values per plane: the outward normal, then the distance from the origin
	const uint32 plane_count = static_cast<uint32>(values.Size() / 4);

	if (plane_count > Brush::scMaxPlanes) {
		LogError("Blockout '{}' has {} planes, the maximum is {}", entry.Name.Get(), plane_count, Brush::scMaxPlanes);
		return {};
	}

	Brush::PlaneList planes;

	for (uint32 i = 0; i < plane_count; i++) {
		planes.Insert(BrushPlane {
			.Normal = Vec3f(values[i * 4].Get<float32>(), values[i * 4 + 1].Get<float32>(),
							values[i * 4 + 2].Get<float32>()),
			.Distance = values[i * 4 + 3].Get<float32>(),
		});
	}

	ReadFaceMaterials(entry, planes);

	ConfigEntry* uvs_entry = entry.GetMember(HashStr32("uvs"));

	if (uvs_entry == nullptr) {
		// Without a layout of their own, the faces get the default one, which depends on the finished brush
		Brush brush = Brush::FromPlanes(planes);

		for (uint32 i = 0; i < brush.Planes.Size; i++) {
			brush.ResetFaceTexture(i);
		}

		return brush;
	}

	const PagedArray<ConfigPrimitive>& uvs = uvs_entry->GetArrayData();

	for (uint32 i = 0; i < plane_count && (i + 1) * scValuesPerFaceTexture <= uvs.Size(); i++) {
		const uint32 base = i * scValuesPerFaceTexture;
		BrushFaceTexture& texture = planes[i].Texture;

		const float32 layout[scValuesPerFaceTexture] = {
			uvs[base].Get<float32>(),	  uvs[base + 1].Get<float32>(), uvs[base + 2].Get<float32>(),
			uvs[base + 3].Get<float32>(), uvs[base + 4].Get<float32>(),
		};

		const bool is_finite = std::all_of(std::begin(layout), std::end(layout),
										   [](float32 value) { return std::isfinite(value); });

		if (!is_finite || std::fabs(layout[2]) < scMinFaceTextureScale ||
			std::fabs(layout[3]) < scMinFaceTextureScale) {
			LogWarning(LC_ASSET,
					   "Blockout '{}' has a broken texture layout on face {}, so it was given the default one",
					   entry.Name.Get(), i);
			continue;
		}

		texture.Offset = Vec2f(layout[0], layout[1]);
		texture.Scale = Vec2f(layout[2], layout[3]);
		texture.Rotation = layout[4];
	}

	return Brush::FromPlanes(planes);
}

constexpr uint32 scValuesPerPatchPoint = 5;

bool IsPatchEntry(const ConfigEntry& entry) { return entry.GetMember(HashStr32(scPatchName)) != nullptr; }

Patch ReadPatchEntry(ConfigEntry& entry)
{
	ConfigEntry* size_entry = entry.GetMember(HashStr32(scPatchName));
	ConfigEntry* points_entry = entry.GetMember(HashStr32("points"));

	if (size_entry == nullptr || points_entry == nullptr) {
		return {};
	}

	const PagedArray<ConfigPrimitive>& size = size_entry->GetArrayData();

	if (size.Size() < 2) {
		return {};
	}

	Patch patch = Patch::FromGrid(size[0].Get<uint32>(), size[1].Get<uint32>());

	if (patch.Points.IsEmpty()) {
		LogError("Patch '{}' has an invalid size", entry.Name.Get());
		return {};
	}

	const PagedArray<ConfigPrimitive>& values = points_entry->GetArrayData();

	if (values.Size() != patch.Points.Size * scValuesPerPatchPoint) {
		LogError("Patch '{}' has {} point values, expected {}", entry.Name.Get(), values.Size(),
				 patch.Points.Size * scValuesPerPatchPoint);
		return {};
	}

	for (uint32 i = 0; i < patch.Points.Size; i++) {
		const uint32 base = i * scValuesPerPatchPoint;
		PatchPoint& point = patch.Points[i];

		point.Position = Vec3f(values[base].Get<float32>(), values[base + 1].Get<float32>(),
							   values[base + 2].Get<float32>());
		point.Texcoord = Vec2f(values[base + 3].Get<float32>(), values[base + 4].Get<float32>());
	}

	ConfigEntry* weights_entry = entry.GetMember(HashStr32("weights"));

	if (weights_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& weights = weights_entry->GetArrayData();

		if (weights.Size() == patch.Points.Size) {
			for (uint32 i = 0; i < patch.Points.Size; i++) {
				patch.Points[i].Weight = std::max(weights[i].Get<float32>(), Patch::scMinWeight);
			}
		}
		else {
			LogError("Patch '{}' has {} weights, expected {}", entry.Name.Get(), weights.Size(), patch.Points.Size);
		}
	}

	ConfigEntry* subdivisions_entry = entry.GetMember(HashStr32("subdiv"));

	if (subdivisions_entry != nullptr && subdivisions_entry->GetArrayData().Size() >= 2) {
		const PagedArray<ConfigPrimitive>& subdivisions = subdivisions_entry->GetArrayData();

		patch.SubdivisionsU = std::clamp(subdivisions[0].Get<uint32>(), Patch::scMinSubdivisions,
										 Patch::scMaxSubdivisions);
		patch.SubdivisionsV = std::clamp(subdivisions[1].Get<uint32>(), Patch::scMinSubdivisions,
										 Patch::scMaxSubdivisions);
	}

	patch.bSmooth = entry.GetMemberValue<int32>(HashStr32("smooth"), 1) != 0;
	patch.SmoothingGroup = static_cast<uint32>(entry.GetMemberValue<int32>(HashStr32("smoothgroup"), 0));

	if (entry.GetMember(HashStr32("thicken")) != nullptr) {
		patch.Closure = ePatchClosure::Thicken;
		patch.Thickness = entry.GetMemberValue<float32>(HashStr32("thicken"), Patch::scDefaultThickness);
	}
	else if (entry.GetMemberValue<int32>(HashStr32("cap"), 0) != 0) {
		patch.Closure = ePatchClosure::Cap;
	}

	return patch;
}

void WritePatchEntry(ConfigEntry& entry, const Patch& patch)
{
	ConfigEntry size_array = ConfigEntry::Array(scPatchName, ConfigPrimitive::ePrimitiveType::Int);
	size_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.Width)));
	size_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.Height)));
	entry.AddMember(std::move(size_array));

	ConfigEntry points_array = ConfigEntry::Array("points", ConfigPrimitive::ePrimitiveType::Float);

	for (const PatchPoint& point : patch.Points) {
		points_array.AppendValue(ConfigPrimitive::FromValue(point.Position.X));
		points_array.AppendValue(ConfigPrimitive::FromValue(point.Position.Y));
		points_array.AppendValue(ConfigPrimitive::FromValue(point.Position.Z));
		points_array.AppendValue(ConfigPrimitive::FromValue(point.Texcoord.X));
		points_array.AppendValue(ConfigPrimitive::FromValue(point.Texcoord.Y));
	}

	entry.AddMember(std::move(points_array));

	const bool is_rational = std::any_of(patch.Points.begin(), patch.Points.end(),
										 [](const PatchPoint& point) { return point.Weight != 1.0f; });

	if (is_rational) {
		ConfigEntry weights_array = ConfigEntry::Array("weights", ConfigPrimitive::ePrimitiveType::Float);

		for (const PatchPoint& point : patch.Points) {
			weights_array.AppendValue(ConfigPrimitive::FromValue(point.Weight));
		}

		entry.AddMember(std::move(weights_array));
	}

	ConfigEntry subdivisions_array = ConfigEntry::Array("subdiv", ConfigPrimitive::ePrimitiveType::Int);
	subdivisions_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.SubdivisionsU)));
	subdivisions_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.SubdivisionsV)));
	entry.AddMember(std::move(subdivisions_array));

	entry.AddMember(ConfigEntry::Literal("smooth", static_cast<int32>(patch.bSmooth)));

	if (patch.SmoothingGroup != 0) {
		entry.AddMember(ConfigEntry::Literal("smoothgroup", static_cast<int32>(patch.SmoothingGroup)));
	}

	if (patch.Closure == ePatchClosure::Cap) {
		entry.AddMember(ConfigEntry::Literal("cap", static_cast<int32>(1)));
	}
	else if (patch.Closure == ePatchClosure::Thicken) {
		entry.AddMember(ConfigEntry::Literal("thicken", patch.Thickness));
	}
}

/////////////////////////////////////
// Subviews
/////////////////////////////////////

Quat ReadBrushRotation(const ConfigEntry& entry)
{
	Quat rotation = Quat::scIdentity;

	const ConfigEntry* euler_entry = entry.GetMember(HashStr32("rot"));

	if (euler_entry != nullptr) {
		rotation = Quat::FromEulerAngles(euler_entry->GetValue<Vec3f>());
	}

	const ConfigEntry* quat_entry = entry.GetMember(HashStr32("rotquat"));

	if (quat_entry != nullptr) {
		rotation = quat_entry->GetValue<Quat>();
	}

	return rotation;
}

BrushPlacement ReadBrushPlacement(const ConfigEntry& entry)
{
	BrushPlacement placement;
	placement.Position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
	placement.Rotation = ReadBrushRotation(entry);

	return placement;
}

BrushPlacement PlaceInSubview(const BrushPlacement& local, const Vec3f brush_center, const BrushPlacement& instance)
{
	BrushPlacement world;
	world.Rotation = (instance.Rotation * local.Rotation).Normalize();
	world.Position = instance.Position + (local.Position + brush_center).Rotate(instance.Rotation) - brush_center;

	return world;
}

BrushPlacement RemoveFromSubview(const BrushPlacement& world, const Vec3f brush_center, const BrushPlacement& instance)
{
	const Quat inverse = instance.Rotation.Conjugate();

	BrushPlacement local;
	local.Rotation = (inverse * world.Rotation).Normalize();
	local.Position = (world.Position + brush_center - instance.Position).Rotate(inverse) - brush_center;

	return local;
}

BrushPlacement FindSubviewPlacement(const BrushPlacement& world, const BrushPlacement& local, const Vec3f brush_center)
{
	BrushPlacement instance;
	instance.Rotation = (world.Rotation * local.Rotation.Conjugate()).Normalize();
	instance.Position = world.Position + brush_center - (local.Position + brush_center).Rotate(instance.Rotation);

	return instance;
}

String MakeSubviewBrushName(const String& instance_name, uint32 member_index)
{
	return String::Fmt("{}_m{}", instance_name, member_index);
}

uint32 MakeBrushEntryNamesUnique(ConfigEntry& blocks_entry)
{
	std::unordered_set<String> taken;
	std::unordered_set<String> seen;

	for (const ConfigEntry& entry : blocks_entry.Members) {
		taken.insert(entry.Name.Get());
	}

	uint32 renamed_count = 0;

	for (ConfigEntry& entry : blocks_entry.Members) {
		const String name = entry.Name.Get();

		if (seen.insert(name).second) {
			continue;
		}

		String unique_name;
		uint32 suffix = 1;

		do {
			unique_name = String::Fmt("{}_{}", name, suffix++);
		} while (taken.contains(unique_name));

		entry.Name = Name(unique_name);

		taken.insert(unique_name);
		seen.insert(unique_name);
		renamed_count++;
	}

	return renamed_count;
}

ConfigEntry PlaceSubviewBrushEntry(ConfigEntry& brush_entry, const String& name, const BrushPlacement& instance)
{
	ConfigEntry placed(name);

	Vec3f center = Vec3f::sZero;

	if (IsPatchEntry(brush_entry)) {
		const Patch patch = ReadPatchEntry(brush_entry);

		if (!patch.IsValid()) {
			return placed;
		}

		center = patch.GetCenter();
	}
	else {
		const Brush brush = ReadBrushEntry(brush_entry);

		if (!brush.IsValid()) {
			return placed;
		}

		center = brush.GetCenter();
	}

	BrushPlacement local;
	local.Position = brush_entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
	local.Rotation = ReadBrushRotation(brush_entry);

	const BrushPlacement world = PlaceInSubview(local, center, instance);

	placed.AddMember(ConfigEntry::Literal("pos", world.Position));
	placed.AddMember(ConfigEntry::Literal("rotquat", world.Rotation));

	for (const ConfigEntry& member : brush_entry.Members) {
		const Hash32 member_name = member.Name.GetHash();

		if (member_name == HashStr32("pos") || member_name == HashStr32("rot") || member_name == HashStr32("rotquat")) {
			continue;
		}

		placed.AddMember(member.Clone(member.Name.Get()));
	}

	return placed;
}

constexpr const char* scEntityBrushMembers[] = {
	"dynamic", "probevolume", "reflectionprobe", "spawn", "trigger", scDecalName,
};

bool IsEntityBrushEntry(const ConfigEntry& entry)
{
	for (const char* member : scEntityBrushMembers) {
		if (entry.GetMemberValue(HashStr32(member), 0) != 0) {
			return true;
		}
	}

	return entry.GetMember(HashStr32("script")) != nullptr;
}

String GetSubviewDisplayName(const String& subview_name)
{
	return String(std::filesystem::path(subview_name.CStr()).stem().string());
}

String GetSubviewFilePath(const String& prototype_path, const String& subview_name)
{
	std::filesystem::path path = std::filesystem::path(prototype_path.CStr()).parent_path() / subview_name.CStr();

	if (!path.has_extension()) {
		path += CompiledMap::scPrototypeExtension;
	}

	return String(path.generic_string());
}

String ResolveNestedSubviewName(const String& parent_name, const String& reference)
{
	const std::filesystem::path folder = std::filesystem::path(parent_name.CStr()).parent_path();

	return String((folder / reference.CStr()).lexically_normal().generic_string());
}

String MakeNestedSubviewReference(const String& parent_name, const String& subview_name)
{
	const std::filesystem::path folder = std::filesystem::path(parent_name.CStr()).parent_path();

	if (folder.empty()) {
		return subview_name;
	}

	const std::filesystem::path reference = std::filesystem::path(subview_name.CStr()).lexically_relative(folder);

	if (reference.empty()) {
		return subview_name;
	}

	return String(reference.generic_string());
}

const char* ReadInstanceSubviewName(const ConfigEntry& instance)
{
	const ConfigEntry* subview_name = instance.GetMember(HashStr32(scSubviewMemberName));

	if (subview_name == nullptr || subview_name->Type != ConfigEntry::ePrimitiveType::String) {
		return nullptr;
	}

	return subview_name->Get<const char*>();
}

bool ReadSubviewFile(const ConfigFile& info, const String& prototype_path, const String& subview_name,
					 ConfigEntry& out_brushes, ConfigEntry& out_instances)
{
	out_instances = ConfigEntry::Struct(scSubviewInstancesEntryName);

	const ConfigEntry* subviews = info.GetEntry(HashStr32(scSubviewsEntryName));

	if (subviews != nullptr) {
		const ConfigEntry* subview = subviews->GetMember(HashStr32(subview_name.CStr()));

		if (subview != nullptr) {
			out_brushes = subview->Clone(subview_name);
			return true;
		}
	}

	const String path = GetSubviewFilePath(prototype_path, subview_name);

	ConfigFile file {};
	file.Load(path.CStr());

	if (file.HasErrors()) {
		return false;
	}

	const ConfigEntry* brushes = file.GetEntry(HashStr32("all"));
	const ConfigEntry* instances = file.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (brushes == nullptr && instances == nullptr) {
		return false;
	}

	if (brushes != nullptr) {
		out_brushes = brushes->Clone(subview_name);
	}
	else {
		out_brushes = ConfigEntry::Struct(subview_name);
	}

	if (instances == nullptr || !instances->Members.IsInited()) {
		return true;
	}

	for (const ConfigEntry& instance : instances->Members) {
		const char* reference = ReadInstanceSubviewName(instance);

		if (reference == nullptr) {
			LogWarning(LC_ASSET, "Subview instance '{}' in '{}' does not name its subview", instance.Name.Get(),
					   subview_name);
			continue;
		}

		ConfigEntry resolved = instance.Clone(instance.Name.Get());
		resolved.GetMember(HashStr32(scSubviewMemberName))
			->Set(ResolveNestedSubviewName(subview_name, reference));

		out_instances.AddMember(std::move(resolved));
	}

	return true;
}

void AppendPlacedSubviewBrushes(ConfigEntry& out_brushes, ConfigEntry& subview_brushes, const String& instance_name,
								const BrushPlacement& instance)
{
	if (!subview_brushes.Members.IsInited()) {
		return;
	}

	uint32 member_index = 0;

	for (ConfigEntry& brush_entry : subview_brushes.Members) {
		ConfigEntry placed = PlaceSubviewBrushEntry(brush_entry, MakeSubviewBrushName(instance_name, member_index++),
													instance);

		if (placed.Members.IsInited() && placed.Members.Size() > 0) {
			out_brushes.AddMember(std::move(placed));
		}
	}
}

static bool FlattenSubview(const ConfigFile& info, const String& prototype_path, const String& subview_name,
						   std::vector<String>& stack, ConfigEntry& out_brushes)
{
	ConfigEntry instances;

	if (!ReadSubviewFile(info, prototype_path, subview_name, out_brushes, instances)) {
		return false;
	}

	if (!instances.Members.IsInited()) {
		return true;
	}

	stack.push_back(subview_name);

	for (const ConfigEntry& instance : instances.Members) {
		const String child_name = ReadInstanceSubviewName(instance);

		if (std::find(stack.begin(), stack.end(), child_name) != stack.end()) {
			LogWarning(LC_ASSET, "The subview '{}' places '{}', which already holds it, so it is left out",
					   subview_name, child_name);
			continue;
		}

		ConfigEntry child_brushes;

		if (!FlattenSubview(info, prototype_path, child_name, stack, child_brushes)) {
			LogWarning(LC_ASSET, "Subview instance '{}' in '{}' uses the subview '{}', which can't be read",
					   instance.Name.Get(), subview_name, child_name);
			continue;
		}

		AppendPlacedSubviewBrushes(out_brushes, child_brushes, instance.Name.Get(), ReadBrushPlacement(instance));
	}

	stack.pop_back();

	return true;
}

bool ReadSubviewBrushes(const ConfigFile& info, const String& prototype_path, const String& subview_name,
						ConfigEntry& out_brushes)
{
	std::vector<String> stack;

	return FlattenSubview(info, prototype_path, subview_name, stack, out_brushes);
}

static void HashSubviewFiles(const ConfigFile& info, const String& prototype_path, const ConfigEntry& instances,
							 std::unordered_set<String>& seen, uint64& hash)
{
	constexpr uint64 scHashPrime = 1099511628211ull;

	if (!instances.Members.IsInited()) {
		return;
	}

	const ConfigEntry* inline_subviews = info.GetEntry(HashStr32(scSubviewsEntryName));

	for (const ConfigEntry& instance : instances.Members) {
		const char* subview_name = ReadInstanceSubviewName(instance);

		if (subview_name == nullptr || !seen.insert(subview_name).second) {
			continue;
		}

		if (inline_subviews != nullptr && inline_subviews->GetMember(HashStr32(subview_name)) != nullptr) {
			continue;
		}

		hash = (hash ^ CompiledMap::HashFile(GetSubviewFilePath(prototype_path, subview_name))) * scHashPrime;

		ConfigEntry brushes;
		ConfigEntry nested;

		if (ReadSubviewFile(info, prototype_path, subview_name, brushes, nested)) {
			HashSubviewFiles(info, prototype_path, nested, seen, hash);
		}
	}
}

uint64 HashPrototype(const String& prototype_path, const ConfigFile& info)
{
	uint64 hash = CompiledMap::HashFile(prototype_path);

	if (hash == 0) {
		return 0;
	}

	const ConfigEntry* instances = info.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (instances == nullptr) {
		return hash;
	}

	std::unordered_set<String> seen;

	HashSubviewFiles(info, prototype_path, *instances, seen, hash);

	return hash;
}

uint32 CountSubviewBrushes(const ConfigFile& info, const String& prototype_path)
{
	uint32 count = 0;

	ForEachSubviewBrush(info, prototype_path, [&](ConfigEntry&, const ConfigEntry&, uint32) { count++; });

	return count;
}

void ForEachSubviewBrush(const ConfigFile& info, const String& prototype_path,
						 const std::function<void(ConfigEntry&, const ConfigEntry&, uint32)>& callback)
{
	const ConfigEntry* instances = info.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (instances == nullptr) {
		return;
	}

	std::unordered_map<String, ConfigEntry> subviews;

	for (const ConfigEntry& instance : instances->Members) {
		const char* subview_name = ReadInstanceSubviewName(instance);

		if (subview_name == nullptr) {
			LogWarning(LC_ASSET, "Subview instance '{}' does not name its subview", instance.Name.Get());
			continue;
		}

		auto found = subviews.find(subview_name);

		if (found == subviews.end()) {
			ConfigEntry brushes;

			if (!ReadSubviewBrushes(info, prototype_path, subview_name, brushes)) {
				LogWarning(LC_ASSET, "Subview instance '{}' uses the subview '{}', which can't be read",
						   instance.Name.Get(), subview_name);
			}

			found = subviews.emplace(subview_name, std::move(brushes)).first;
		}

		ConfigEntry& subview = found->second;

		if (!subview.Members.IsInited()) {
			continue;
		}

		const BrushPlacement placement = ReadBrushPlacement(instance);

		uint32 member_index = 0;

		for (ConfigEntry& brush_entry : subview.Members) {
			ConfigEntry placed = PlaceSubviewBrushEntry(
				brush_entry, MakeSubviewBrushName(instance.Name.Get(), member_index), placement);

			if (placed.Members.IsInited() && placed.Members.Size() > 0) {
				callback(placed, instance, member_index);
			}

			member_index++;
		}
	}
}

} // namespace fx
