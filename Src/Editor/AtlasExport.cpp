#include "AtlasPacker.hpp"

#include <stb_image_write.h>

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>

namespace fx::editor {

static ConfigEntry MakeIntPair(const char* name, IVec2 value)
{
	ConfigEntry entry = ConfigEntry::Array(name, ConfigPrimitive::ePrimitiveType::Int);
	entry.AppendValue(ConfigPrimitive::FromValue(static_cast<int64>(value.X)));
	entry.AppendValue(ConfigPrimitive::FromValue(static_cast<int64>(value.Y)));
	return entry;
}

bool ExportAtlas(const AtlasImageArray& images, const AtlasLayout& layout, const String& png_path,
				 const String& config_path)
{
	const SizedArray<uint8> pixels = ComposeAtlas(images, layout);
	const IVec2 pixel_size = layout.GetPixelSize();

	if (!stbi_write_png(png_path.CStr(), pixel_size.X, pixel_size.Y, 4, pixels.pData, pixel_size.X * 4)) {
		return false;
	}

	ConfigFile config;

	config.AddEntry(ConfigEntry::Literal("image", GetPathFileName(png_path).CStr()));
	config.AddEntry(ConfigEntry::Literal("tile_size", static_cast<int64>(layout.TileSize)));
	config.AddEntry(MakeIntPair("atlas_size", pixel_size));
	config.AddEntry(MakeIntPair("atlas_tiles", layout.TileCount));

	ConfigEntry groups = ConfigEntry::Struct("groups");

	for (const AtlasGroup& group : layout.Groups) {
		ConfigEntry entry = ConfigEntry::Struct(group.Name.Str());

		entry.AddMember(MakeIntPair("tile", group.TilePos));
		entry.AddMember(MakeIntPair("tiles", group.TileCount));
		entry.AddMember(MakeIntPair("source_size", group.SourceSize));

		groups.AddMember(std::move(entry));
	}

	config.AddEntry(std::move(groups));
	config.Write(config_path.Str());

	return FilesystemIO::FileExists(config_path);
}

} // namespace fx::editor
