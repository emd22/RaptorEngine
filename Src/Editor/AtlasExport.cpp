#include "AtlasPacker.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <stb_image_write.h>

namespace fx::editor {

static ConfigEntry MakeIntPair(const char* name, int32 a, int32 b)
{
	ConfigEntry entry = ConfigEntry::Array(name, ConfigPrimitive::ePrimitiveType::Int);
	entry.AppendValue(ConfigPrimitive::FromValue(static_cast<int64>(a)));
	entry.AppendValue(ConfigPrimitive::FromValue(static_cast<int64>(b)));
	return entry;
}

bool ExportAtlas(const DynArray<AtlasImage>& images, const AtlasLayout& layout, const String& png_path,
				 const String& config_path)
{
	const SizedArray<uint8> pixels = ComposeAtlas(images, layout);

	if (!stbi_write_png(png_path.CStr(), layout.GetWidth(), layout.GetHeight(), 4, pixels.pData,
						layout.GetWidth() * 4)) {
		return false;
	}

	ConfigFile config;

	config.AddEntry(ConfigEntry::Literal("image", GetPathFileName(png_path).CStr()));
	config.AddEntry(ConfigEntry::Literal("tile_size", static_cast<int64>(layout.TileSize)));
	config.AddEntry(MakeIntPair("atlas_size", layout.GetWidth(), layout.GetHeight()));
	config.AddEntry(MakeIntPair("atlas_tiles", layout.Columns, layout.Rows));

	ConfigEntry groups = ConfigEntry::Struct("groups");

	for (const AtlasGroup& group : layout.Groups) {
		ConfigEntry entry = ConfigEntry::Struct(group.Name.Str());

		entry.AddMember(MakeIntPair("tile", group.TileX, group.TileY));
		entry.AddMember(MakeIntPair("tiles", group.TilesWide, group.TilesHigh));
		entry.AddMember(MakeIntPair("source_size", group.SourceWidth, group.SourceHeight));

		groups.AddMember(std::move(entry));
	}

	config.AddEntry(std::move(groups));
	config.Write(config_path.Str());

	return FilesystemIO::FileExists(config_path);
}

} // namespace fx::editor
