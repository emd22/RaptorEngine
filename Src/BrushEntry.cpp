#include "BrushEntry.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/DynArray.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/HashMap.hpp>
#include <Map/CompiledMap.hpp>
#include <Material/SpecialMaterial.hpp>
#include <Object/ObjectLayer.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fx {

constexpr const char* scFaceMaterialsName = "facemats";

constexpr const char* scBrushMaterialName = "-";

constexpr const char* scPatchName = "patch";

bool IsDecalEntry(const ConfigEntry& entry) { return entry.GetMemberValue(HashStr32(scDecalName), 0) != 0; }

const char* ReadBrushMaterialName(const ConfigEntry& entry)
{
	for (const eSpecialMaterial special :
		 { eSpecialMaterial::NvBrush, eSpecialMaterial::DBound, eSpecialMaterial::Portal }) {
		const char* name = GetSpecialMaterialName(special);

		if (entry.GetMemberValue(HashStr32(name), 0) != 0) {
			return name;
		}
	}

	const ConfigEntry* material = entry.GetMember(HashStr32("mat"));

	if (material == nullptr || material->Type != ConfigEntry::ePrimitiveType::String) {
		return "";
	}

	const char* name = material->Get<const char*>();

	if (name == nullptr) {
		return "";
	}

	return name;
}

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
	// Values stored per plane in a blockout's `uvs` array: offset U and V, scale U and V, then rotation
	constexpr uint32 scValuesPerFaceTexture = 5;

	constexpr float32 scMinFaceTextureScale = 1e-4f;

	ConfigEntry* scale_entry = entry.GetMember(HashStr32("scale"));

	if (scale_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& scales = scale_entry->GetArrayData();

		if (scales.Size() < 6) {
			return {};
		}

		// Left, right, top, bottom, front, back extents
		const FVec3 min = -FVec3(scales[0].Get<float32>(), scales[3].Get<float32>(), scales[5].Get<float32>());
		const FVec3 max = FVec3(scales[1].Get<float32>(), scales[2].Get<float32>(), scales[4].Get<float32>());

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
			.Normal = FVec3(values[i * 4].Get<float32>(), values[i * 4 + 1].Get<float32>(),
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

		texture.Offset = FVec2(layout[0], layout[1]);
		texture.Scale = FVec2(layout[2], layout[3]);
		texture.Rotation = layout[4];
	}

	return Brush::FromPlanes(planes);
}

bool IsPatchEntry(const ConfigEntry& entry) { return entry.GetMember(HashStr32(scPatchName)) != nullptr; }

Patch ReadPatchEntry(ConfigEntry& entry)
{
	constexpr uint32 scValuesPerPatchPoint = 5;

	ConfigEntry* size_entry = entry.GetMember(HashStr32(scPatchName));
	ConfigEntry* points_entry = entry.GetMember(HashStr32("points"));

	if (size_entry == nullptr || points_entry == nullptr) {
		return {};
	}

	const PagedArray<ConfigPrimitive>& size = size_entry->GetArrayData();

	if (size.Size() < 2) {
		return {};
	}

	const bool is_detail_mesh = entry.GetMemberValue<int32>(HashStr32("detailmesh"), 0) != 0;

	Patch patch;

	if (is_detail_mesh) {
		patch = Patch::FromDetailMesh(size[0].Get<uint32>(), size[1].Get<uint32>());
	}
	else {
		patch = Patch::FromGrid(size[0].Get<uint32>(), size[1].Get<uint32>());
	}

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

		point.Position = FVec3(values[base].Get<float32>(), values[base + 1].Get<float32>(),
							   values[base + 2].Get<float32>());
		point.Texcoord = FVec2(values[base + 3].Get<float32>(), values[base + 4].Get<float32>());
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

	ConfigEntry* colors_entry = entry.GetMember(HashStr32("colors"));

	if (colors_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& colors = colors_entry->GetArrayData();

		if (colors.Size() == patch.Points.Size * 4) {
			for (uint32 i = 0; i < patch.Points.Size; i++) {
				PatchColor& color = patch.Points[i].Color;

				color.R = std::clamp(colors[i * 4].Get<float32>(), 0.0f, 1.0f);
				color.G = std::clamp(colors[i * 4 + 1].Get<float32>(), 0.0f, 1.0f);
				color.B = std::clamp(colors[i * 4 + 2].Get<float32>(), 0.0f, 1.0f);
				color.A = std::clamp(colors[i * 4 + 3].Get<float32>(), 0.0f, 1.0f);
			}
		}
		else {
			LogError("Patch '{}' has {} colour values, expected {}", entry.Name.Get(), colors.Size(),
					 patch.Points.Size * 4);
		}
	}

	ConfigEntry* turned_entry = entry.GetMember(HashStr32("turned"));

	if (is_detail_mesh && turned_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& turned = turned_entry->GetArrayData();

		for (uint32 i = 0; i < turned.Size(); i++) {
			const uint32 cell = turned[i].Get<uint32>();

			if (cell < patch.TurnedCells.Size) {
				patch.TurnedCells[cell] = 1;
			}
			else {
				LogError("Patch '{}' turns cell {}, which is outside its {} cells", entry.Name.Get(), cell,
						 patch.TurnedCells.Size);
			}
		}
	}

	ConfigEntry* subdivisions_entry = entry.GetMember(HashStr32("subdiv"));

	if (!is_detail_mesh && subdivisions_entry != nullptr && subdivisions_entry->GetArrayData().Size() >= 2) {
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

	if (patch.HasVertexColors()) {
		ConfigEntry colors_array = ConfigEntry::Array("colors", ConfigPrimitive::ePrimitiveType::Float);

		for (const PatchPoint& point : patch.Points) {
			colors_array.AppendValue(ConfigPrimitive::FromValue(point.Color.R));
			colors_array.AppendValue(ConfigPrimitive::FromValue(point.Color.G));
			colors_array.AppendValue(ConfigPrimitive::FromValue(point.Color.B));
			colors_array.AppendValue(ConfigPrimitive::FromValue(point.Color.A));
		}

		entry.AddMember(std::move(colors_array));
	}

	if (patch.bIsDetailMesh) {
		entry.AddMember(ConfigEntry::Literal("detailmesh", static_cast<int32>(1)));

		const bool has_turned = std::any_of(patch.TurnedCells.begin(), patch.TurnedCells.end(),
											[](uint8 flag) { return flag != 0; });

		if (has_turned) {
			ConfigEntry turned_array = ConfigEntry::Array("turned", ConfigPrimitive::ePrimitiveType::Int);

			for (uint32 cell = 0; cell < patch.TurnedCells.Size; cell++) {
				if (patch.TurnedCells[cell] != 0) {
					turned_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(cell)));
				}
			}

			entry.AddMember(std::move(turned_array));
		}
	}
	else {
		ConfigEntry subdivisions_array = ConfigEntry::Array("subdiv", ConfigPrimitive::ePrimitiveType::Int);
		subdivisions_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.SubdivisionsU)));
		subdivisions_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(patch.SubdivisionsV)));
		entry.AddMember(std::move(subdivisions_array));
	}

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

BrushPlacement ReadBrushPlacement(const ConfigEntry& entry)
{
	BrushPlacement placement;

	const ConfigEntry* euler_entry = entry.GetMember(HashStr32("rot"));

	if (euler_entry != nullptr) {
		placement.Rotation = FQuat::FromEulerAngles(euler_entry->GetValue<FVec3>());
	}

	Serializable<BrushPlacement>::LoadFromConfig(entry, placement);

	return placement;
}

void WriteBrushPlacement(ConfigEntry& entry, const BrushPlacement& placement)
{
	Serializable<BrushPlacement>::SaveToConfig(placement, entry);
}

BrushPlacement PlaceInSubview(const BrushPlacement& local, const FVec3 brush_center, const BrushPlacement& instance)
{
	BrushPlacement world;
	world.Rotation = (instance.Rotation * local.Rotation).Normalize();
	world.Position = instance.Position + (local.Position + brush_center).Rotate(instance.Rotation) - brush_center;

	return world;
}

BrushPlacement RemoveFromSubview(const BrushPlacement& world, const FVec3 brush_center, const BrushPlacement& instance)
{
	const FQuat inverse = instance.Rotation.Conjugate();

	BrushPlacement local;
	local.Rotation = (inverse * world.Rotation).Normalize();
	local.Position = (world.Position + brush_center - instance.Position).Rotate(inverse) - brush_center;

	return local;
}

BrushPlacement FindSubviewPlacement(const BrushPlacement& world, const BrushPlacement& local, const FVec3 brush_center)
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
	HashMap<String, bool> taken;
	HashMap<String, bool> seen;

	for (const ConfigEntry& entry : blocks_entry.Members) {
		taken.Insert(entry.Name.Get(), true);
	}

	uint32 renamed_count = 0;

	for (ConfigEntry& entry : blocks_entry.Members) {
		const String name = entry.Name.Get();

		if (!seen.Contains(name)) {
			seen.Insert(name, true);
			continue;
		}

		String unique_name;
		uint32 suffix = 1;

		do {
			unique_name = String::Fmt("{}_{}", name, suffix++);
		} while (taken.Contains(unique_name));

		entry.Name = Name(unique_name);

		taken.Insert(unique_name, true);
		seen.Insert(unique_name, true);
		renamed_count++;
	}

	return renamed_count;
}

ConfigEntry PlaceSubviewBrushEntry(ConfigEntry& brush_entry, const String& name, const BrushPlacement& instance)
{
	ConfigEntry placed(name);

	FVec3 center = FVec3::sZero;

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

	const BrushPlacement world = PlaceInSubview(ReadBrushPlacement(brush_entry), center, instance);

	WriteBrushPlacement(placed, world);

	for (const ConfigEntry& member : brush_entry.Members) {
		const Hash32 member_name = member.Name.GetHash();

		if (member_name == HashStr32("pos") || member_name == HashStr32("rot") || member_name == HashStr32("rotquat")) {
			continue;
		}

		placed.AddMember(member.Clone(member.Name.Get()));
	}

	return placed;
}

bool IsEntityBrushEntry(const ConfigEntry& entry)
{
	static constexpr const char* scEntityBrushMembers[] = {
		"dynamic", "probevolume", "reflectionprobe", "spawn", "trigger", scDecalName,
	};

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
		resolved.GetMember(HashStr32(scSubviewMemberName))->Set(ResolveNestedSubviewName(subview_name, reference));

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
						   DynArray<String, GrowthFunctions::Double>& stack, ConfigEntry& out_brushes)
{
	ConfigEntry instances;

	if (!ReadSubviewFile(info, prototype_path, subview_name, out_brushes, instances)) {
		return false;
	}

	if (!instances.Members.IsInited()) {
		return true;
	}

	stack.Insert(subview_name);

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

	stack.RemoveLast();

	return true;
}

bool ReadSubviewBrushes(const ConfigFile& info, const String& prototype_path, const String& subview_name,
						ConfigEntry& out_brushes)
{
	DynArray<String, GrowthFunctions::Double> stack;

	return FlattenSubview(info, prototype_path, subview_name, stack, out_brushes);
}

static void HashSubviewFiles(const ConfigFile& info, const String& prototype_path, const ConfigEntry& instances,
							 HashMap<String, bool>& seen, uint64& hash)
{
	constexpr uint64 scHashPrime = 1099511628211ull;

	if (!instances.Members.IsInited()) {
		return;
	}

	const ConfigEntry* inline_subviews = info.GetEntry(HashStr32(scSubviewsEntryName));

	for (const ConfigEntry& instance : instances.Members) {
		const char* subview_name = ReadInstanceSubviewName(instance);

		if (subview_name == nullptr || seen.Contains(subview_name)) {
			continue;
		}

		seen.Insert(subview_name, true);

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

bool IsBakedModelEntry(const ConfigEntry& model, const ConfigEntry* brushes)
{
	const ConfigEntry* mesh = model.GetMember(HashStr32("mesh"));

	if (mesh == nullptr || mesh->Type != ConfigEntry::ePrimitiveType::String) {
		return false;
	}

	if (model.GetMemberValue(HashStr32("shadows"), 0) == 0 || model.GetMemberValue(HashStr32("unlit"), 0) != 0 ||
		model.GetMemberValue(HashStr32("spawn"), 0) != 0 || model.GetMemberValue(HashStr32("nobake"), 0) != 0 ||
		model.GetMember(HashStr32("script")) != nullptr) {
		return false;
	}

	if (model.GetMemberValue(HashStr32("layer"), 0) == static_cast<int64>(eObjectLayer::PlayerLayer)) {
		return false;
	}

	const ConfigEntry* collider = model.GetMember(HashStr32("collider"));

	if (collider != nullptr && collider->Type == ConfigEntry::ePrimitiveType::String && brushes != nullptr) {
		const ConfigEntry* collider_brush = brushes->GetMember(HashStr32(collider->Get<const char*>()));

		if (collider_brush != nullptr && collider_brush->GetMemberValue(HashStr32("dynamic"), 0) != 0) {
			return false;
		}
	}

	return true;
}

static void HashModelFiles(const ConfigFile& info, uint64& hash)
{
	constexpr uint64 scHashPrime = 1099511628211ull;

	const ConfigEntry* models = info.GetEntry(HashStr32("objects"));

	if (models == nullptr || !models->Members.IsInited()) {
		return;
	}

	const ConfigEntry* brushes = info.GetEntry(HashStr32("all"));

	for (const ConfigEntry& model : models->Members) {
		if (!IsBakedModelEntry(model, brushes)) {
			continue;
		}

		const char* mesh_path = model.GetMember(HashStr32("mesh"))->Get<const char*>();

		hash = (hash ^ CompiledMap::HashFile(FilesystemIO::ResolvePath(mesh_path))) * scHashPrime;
	}
}

uint64 HashPrototype(const String& prototype_path, const ConfigFile& info)
{
	uint64 hash = CompiledMap::HashFile(prototype_path);

	if (hash == 0) {
		return 0;
	}

	HashModelFiles(info, hash);

	const ConfigEntry* instances = info.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (instances == nullptr) {
		return hash;
	}

	HashMap<String, bool> seen;

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

	HashMap<String, ConfigEntry> subviews;

	for (const ConfigEntry& instance : instances->Members) {
		const char* subview_name = ReadInstanceSubviewName(instance);

		if (subview_name == nullptr) {
			LogWarning(LC_ASSET, "Subview instance '{}' does not name its subview", instance.Name.Get());
			continue;
		}

		ConfigEntry* found = subviews.Find(subview_name);

		if (found == nullptr) {
			ConfigEntry brushes;

			if (!ReadSubviewBrushes(info, prototype_path, subview_name, brushes)) {
				LogWarning(LC_ASSET, "Subview instance '{}' uses the subview '{}', which can't be read",
						   instance.Name.Get(), subview_name);
			}

			found = &subviews.Insert(subview_name, std::move(brushes));
		}

		ConfigEntry& subview = *found;

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
