#include "BrushEntry.hpp"

#include <Asset/ConfigFile.hpp>
#include <Map/CompiledMap.hpp>
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

	return eBrushKind::Geometry;
}

/// Values stored per plane in a blockout's `uvs` array: offset U and V, scale U and V, then rotation
constexpr uint32 scValuesPerFaceTexture = 5;

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

		texture.Offset = Vec2f(uvs[base].Get<float32>(), uvs[base + 1].Get<float32>());
		texture.Scale = Vec2f(uvs[base + 2].Get<float32>(), uvs[base + 3].Get<float32>());
		texture.Rotation = uvs[base + 4].Get<float32>();
	}

	return Brush::FromPlanes(planes);
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

	const Brush brush = ReadBrushEntry(brush_entry);

	if (!brush.IsValid()) {
		return placed;
	}

	BrushPlacement local;
	local.Position = brush_entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
	local.Rotation = ReadBrushRotation(brush_entry);

	const BrushPlacement world = PlaceInSubview(local, brush.GetCenter(), instance);

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
	"dynamic", "probevolume", "reflectionprobe", "spawn", "trigger",
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

bool ReadSubviewBrushes(const ConfigFile& info, const String& prototype_path, const String& subview_name,
						ConfigEntry& out_brushes)
{
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

	if (brushes == nullptr) {
		return false;
	}

	if (file.GetEntry(HashStr32(scSubviewInstancesEntryName)) != nullptr) {
		LogWarning(LC_ASSET, "The subview '{}' places subviews of its own, which are left out", subview_name);
	}

	out_brushes = brushes->Clone(subview_name);

	return true;
}

static const char* ReadInstanceSubviewName(const ConfigEntry& instance)
{
	const ConfigEntry* subview_name = instance.GetMember(HashStr32(scSubviewMemberName));

	if (subview_name == nullptr || subview_name->Type != ConfigEntry::ePrimitiveType::String) {
		return nullptr;
	}

	return subview_name->Get<const char*>();
}

uint64 HashPrototype(const String& prototype_path, const ConfigFile& info)
{
	constexpr uint64 scHashPrime = 1099511628211ull;

	uint64 hash = CompiledMap::HashFile(prototype_path);

	if (hash == 0) {
		return 0;
	}

	const ConfigEntry* instances = info.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (instances == nullptr) {
		return hash;
	}

	const ConfigEntry* inline_subviews = info.GetEntry(HashStr32(scSubviewsEntryName));

	std::unordered_set<String> seen;

	for (const ConfigEntry& instance : instances->Members) {
		const char* subview_name = ReadInstanceSubviewName(instance);

		if (subview_name == nullptr || !seen.insert(subview_name).second) {
			continue;
		}

		if (inline_subviews != nullptr && inline_subviews->GetMember(HashStr32(subview_name)) != nullptr) {
			continue;
		}

		hash = (hash ^ CompiledMap::HashFile(GetSubviewFilePath(prototype_path, subview_name))) * scHashPrime;
	}

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

		BrushPlacement placement;
		placement.Position = instance.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);
		placement.Rotation = ReadBrushRotation(instance);

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
