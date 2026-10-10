#include "BrushEntry.hpp"

#include <Asset/ConfigFile.hpp>
#include <cstring>

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

} // namespace fx
