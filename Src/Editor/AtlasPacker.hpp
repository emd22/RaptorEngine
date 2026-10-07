#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx::editor {

struct AtlasImage
{
	String Name;
	String Path;
	int32 Width = 0;
	int32 Height = 0;
	SizedArray<uint8> Pixels;
};

struct AtlasGroup
{
	String Name;
	int32 TileX = 0;
	int32 TileY = 0;
	int32 TilesWide = 0;
	int32 TilesHigh = 0;
	int32 SourceWidth = 0;
	int32 SourceHeight = 0;
};

struct AtlasLayout
{
	int32 TileSize = 0;
	int32 Columns = 0;
	int32 Rows = 0;
	DynArray<AtlasGroup> Groups;

	int32 GetWidth() const { return Columns * TileSize; }
	int32 GetHeight() const { return Rows * TileSize; }
};

bool LoadAtlasImage(const String& path, AtlasImage& out_image);

String GetPathFileName(const String& path);
String GetPathDirectory(const String& path);
String ReplacePathExtension(const String& path, const char* extension);

String MakeGroupName(const String& raw);

int32 GetTilesForSize(int32 pixels, int32 tile_size);

AtlasLayout PackAtlas(const DynArray<AtlasImage>& images, int32 tile_size, int32 columns);

SizedArray<uint8> ComposeAtlas(const DynArray<AtlasImage>& images, const AtlasLayout& layout);

bool ExportAtlas(const DynArray<AtlasImage>& images, const AtlasLayout& layout, const String& png_path,
				 const String& config_path);

} // namespace fx::editor
