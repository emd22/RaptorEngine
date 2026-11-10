#if 0

#pragma once

#include <ft2build.h>

#include <Renderer/Backend/Fwd/Fwd_GetGpuAllocator.hpp>
#include <Renderer/Backend/Fwd/Fwd_SubmitUploadGpuCmd.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Backend/Image.hpp>
#include FT_FREETYPE_H

#include <Core/HashMap.hpp>
#include <array>
#include <string_view>

namespace fx::renderer {

struct GlyphMetrics
{
	FVec2 Bearing = FVec2::sZero;
	UVec2 Size = UVec2::sZero;

	float Advance = 0.0f;

	float Lsb = 0.0f;
	UVec2 AtlasOffset = UVec2::sZero;
};

class FontAtlas
{
public:
	FontAtlas() = default;

	bool Create(uint32 atlas_width, uint32 atlas_height, float font_size);

	void RenderGlyph(FT_GlyphSlot glyph, uint32 codepoint);
	void Upload(Image& out_image);

	float GetFontSize() const { return FontSize; }
	UVec2 GetSize() const { return AtlasSize; }

	// SizedArray<uint8>& GetAtlasData() { return AtlasData; }
	SizedArray<uint8> GetImageData(eImageFormat format) const;

	const GlyphMetrics* GetGlyph(uint32 codepoint) const
	{
		return Glyphs.Find(codepoint);
	}

	bool HasGlyph(uint32 codepoint) const { return Glyphs.Contains(codepoint); }

	void Destroy();

private:
	UVec2 FindSlot(uint32 width, uint32 height);

public:
	HashMap<uint32, GlyphMetrics> Glyphs;

	UVec2 AtlasSize = UVec2::sZero;
	float32 FontSize = 0.0f;

	SizedArray<uint8> AtlasData;

private:
	UVec2 Cursor = UVec2::sZero;
	uint32 RowHeight = 0;
};

struct Font
{
public:
	Font() = default;

	bool LoadFromFile(const String& path, float font_size);
	bool LoadFromMemory(const uint8* data, uint64 data_size, float font_size);

	void Destroy();

	FontAtlas& GetAtlas() { return Atlas; }
	const FontAtlas& GetAtlas() const { return Atlas; }

	void RenderAtlasToImage(Image& out_image);

	void SaveToFile(const String& path, eImageSaveFormat file_format) const;

	float GetFontSize() const { return FontSize; }
	const String& GetFamilyName() const { return FamilyName; }
	const String& GetStyleName() const { return StyleName; }

	FT_Library GetLibrary() { return Library; }
	FT_Face GetFace() { return Face; }

private:
	void InitCommon(float32 font_size);
	void BakeAtlas();

	void WriteMetaFile(const String& atlas_path) const;

public:
	String FamilyName;
	String StyleName;

private:
	FT_Library Library = nullptr;
	FT_Face Face = nullptr;

	FontAtlas Atlas;
	float32 FontSize = 0.0f;
};

} // namespace fx::renderer

#endif
