#include "AtlasPacker.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace fx::editor {

static bool IsGroupNameChar(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

static uint32 FindLastSeparator(const String& path)
{
	const uint32 slash = path.FindLast('/');
	const uint32 backslash = path.FindLast('\\');

	if (slash == String::scNotFound) {
		return backslash;
	}

	if (backslash == String::scNotFound) {
		return slash;
	}

	return std::max(slash, backslash);
}

static bool IsGroupNameUsed(const DynArray<AtlasGroup>& groups, const String& name)
{
	for (const AtlasGroup& group : groups) {
		if (group.Name == name) {
			return true;
		}
	}

	return false;
}

String GetPathFileName(const String& path)
{
	const uint32 separator = FindLastSeparator(path);

	if (separator == String::scNotFound) {
		return path;
	}

	return path.SubStr(separator + 1, path.GetLength() - separator - 1);
}

String GetPathDirectory(const String& path)
{
	const uint32 separator = FindLastSeparator(path);

	if (separator == String::scNotFound) {
		return String();
	}

	return path.SubStr(0, separator);
}

String ReplacePathExtension(const String& path, const char* extension)
{
	const uint32 separator = FindLastSeparator(path);
	const uint32 dot = path.FindLast('.');

	const bool has_extension = dot != String::scNotFound && (separator == String::scNotFound || dot > separator + 1);

	return (has_extension ? path.SubStr(0, dot) : path) + extension;
}

bool LoadAtlasImage(const String& path, AtlasImage& out_image)
{
	int width = 0;
	int height = 0;
	int channels = 0;

	uint8* data = stbi_load(path.CStr(), &width, &height, &channels, 4);

	if (data == nullptr || width <= 0 || height <= 0) {
		if (data != nullptr) {
			stbi_image_free(data);
		}
		return false;
	}

	const String file_name = GetPathFileName(path);
	const uint32 dot = file_name.FindLast('.');

	out_image.Path = path;
	out_image.Name = MakeGroupName(dot == String::scNotFound || dot == 0 ? file_name : file_name.SubStr(0, dot));
	out_image.Width = width;
	out_image.Height = height;
	out_image.Pixels = SizedArray<uint8>::CreateCopyOf(data, static_cast<size_t>(width) * height * 4);

	stbi_image_free(data);
	return true;
}

String MakeGroupName(const String& raw)
{
	const bool needs_prefix = raw.IsEmpty() || std::isdigit(static_cast<unsigned char>(raw.CStr()[0])) != 0;
	const uint32 offset = needs_prefix ? 1 : 0;

	SizedArray<char> name(raw.GetLength() + offset + 1);
	name.MarkFull();

	if (needs_prefix) {
		name[0] = '_';
	}

	for (uint32 i = 0; i < raw.GetLength(); i++) {
		const char ch = raw.CStr()[i];
		name[i + offset] = IsGroupNameChar(ch) ? ch : '_';
	}

	return String(name.pData, raw.GetLength() + offset);
}

int32 GetTilesForSize(int32 pixels, int32 tile_size)
{
	if (tile_size <= 0) {
		return 0;
	}

	return std::max(1, (pixels + tile_size - 1) / tile_size);
}

AtlasLayout PackAtlas(const DynArray<AtlasImage>& images, int32 tile_size, int32 columns)
{
	AtlasLayout layout;
	layout.TileSize = std::max(1, tile_size);
	layout.Columns = std::max(1, columns);
	layout.Groups.SetPageSize(16);

	int32 max_rows = 1;

	for (const AtlasImage& image : images) {
		layout.Columns = std::max(layout.Columns, GetTilesForSize(image.Width, layout.TileSize));
		max_rows += GetTilesForSize(image.Height, layout.TileSize);
	}

	const size_t grid_size = static_cast<size_t>(layout.Columns) * max_rows;
	SizedArray<uint8> occupied(grid_size);
	occupied.MarkFull();
	std::memset(occupied.pData, 0, grid_size);

	auto is_free = [&](int32 x, int32 y, int32 w, int32 h)
	{
		for (int32 row = y; row < y + h; row++) {
			for (int32 col = x; col < x + w; col++) {
				if (occupied[static_cast<size_t>(row) * layout.Columns + col] != 0) {
					return false;
				}
			}
		}

		return true;
	};

	for (const AtlasImage& image : images) {
		AtlasGroup group;
		group.TilesWide = GetTilesForSize(image.Width, layout.TileSize);
		group.TilesHigh = GetTilesForSize(image.Height, layout.TileSize);
		group.SourceWidth = image.Width;
		group.SourceHeight = image.Height;

		const String name = MakeGroupName(image.Name);
		group.Name = name;

		for (int32 suffix = 2; IsGroupNameUsed(layout.Groups, group.Name); suffix++) {
			group.Name = name + "_" + String::From(suffix);
		}

		bool placed = false;

		int32 y = 0;

		while (true) {
			if (placed) {
				break;
			}

			for (int32 x = 0; x + group.TilesWide <= layout.Columns; x++) {
				if (is_free(x, y, group.TilesWide, group.TilesHigh)) {
					group.TileX = x;
					group.TileY = y;
					placed = true;
					break;
				}
			}

			++y;
		}

		for (int32 row = group.TileY; row < group.TileY + group.TilesHigh; row++) {
			for (int32 col = group.TileX; col < group.TileX + group.TilesWide; col++) {
				occupied[static_cast<size_t>(row) * layout.Columns + col] = 1;
			}
		}

		layout.Rows = std::max(layout.Rows, group.TileY + group.TilesHigh);
		layout.Groups.Emplace(std::move(group));
	}

	layout.Rows = std::max(layout.Rows, 1);

	return layout;
}

SizedArray<uint8> ComposeAtlas(const DynArray<AtlasImage>& images, const AtlasLayout& layout)
{
	const size_t atlas_width = static_cast<size_t>(layout.GetWidth());
	const size_t atlas_bytes = atlas_width * layout.GetHeight() * 4;

	SizedArray<uint8> atlas(atlas_bytes);
	atlas.MarkFull();
	std::memset(atlas.pData, 0, atlas_bytes);

	for (uint32 i = 0; i < images.Size && i < layout.Groups.Size; i++) {
		const AtlasImage& image = images[i];
		const AtlasGroup& group = layout.Groups[i];

		const size_t dest_x = static_cast<size_t>(group.TileX) * layout.TileSize;
		const size_t dest_y = static_cast<size_t>(group.TileY) * layout.TileSize;

		for (int32 row = 0; row < image.Height; row++) {
			const uint8* source = image.Pixels.pData + static_cast<size_t>(row) * image.Width * 4;
			uint8* dest = atlas.pData + ((dest_y + row) * atlas_width + dest_x) * 4;

			std::memcpy(dest, source, static_cast<size_t>(image.Width) * 4);
		}
	}

	return atlas;
}

} // namespace fx::editor
