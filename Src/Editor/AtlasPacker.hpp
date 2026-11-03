#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>

namespace fx::editor {

struct AtlasImage
{
	String Name;
	String Path;
	Vec2i Size = Vec2i::scZero;
	SizedArray<uint8> Pixels;
};

struct AtlasGroup
{
	String Name;
	Vec2i TilePos = Vec2i::scZero;
	Vec2i TileCount = Vec2i::scZero;
	Vec2i SourceSize = Vec2i::scZero;
};

using AtlasImageArray = DynArray<AtlasImage, GrowthFunctions::Double>;
using AtlasGroupArray = DynArray<AtlasGroup, GrowthFunctions::Double>;

struct AtlasLayout
{
	int32 TileSize = 0;
	Vec2i TileCount = Vec2i::scZero;
	AtlasGroupArray Groups;

	Vec2i GetPixelSize() const { return TileCount * TileSize; }
};

bool LoadAtlasImage(const String& path, AtlasImage& out_image);

String GetPathFileName(const String& path);
String GetPathDirectory(const String& path);
String ReplacePathExtension(const String& path, const char* extension);

String MakeGroupName(const String& raw);

int32 GetTilesForSize(int32 pixels, int32 tile_size);

AtlasLayout PackAtlas(const AtlasImageArray& images, int32 tile_size, int32 columns);

SizedArray<uint8> ComposeAtlas(const AtlasImageArray& images, const AtlasLayout& layout);

bool ExportAtlas(const AtlasImageArray& images, const AtlasLayout& layout, const String& png_path,
				 const String& config_path);

} // namespace fx::editor
