#include "Font.hpp"

#include <Asset/ConfigFile.hpp>
#include <Asset/Loader/AxLoaderStb.hpp>
#include <Core/Panic.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Font")

/////////////////////////////////////
// Font Atlas functions
/////////////////////////////////////

bool FontAtlas::Create(uint32 atlas_width, uint32 atlas_height, float font_size)
{
    Assert(atlas_width > 0 && atlas_height > 0);
    Assert(font_size > 0.0f);

    AtlasSize = Vec2u(atlas_width, atlas_height);
    FontSize = font_size;

    AtlasData.InitSize(atlas_width * atlas_height);
    memset(AtlasData.pData, 0, AtlasData.GetSizeInBytes());

    Cursor = Vec2u::sZero;
    RowHeight = 0;

    return true;
}

Vec2u FontAtlas::FindSlot(uint32 width, uint32 height)
{
    if (Cursor.X + width > AtlasSize.X) {
        Cursor.X = 0;
        Cursor.Y += RowHeight;

        RowHeight = 0;
    }

    if (Cursor.Y + height > AtlasSize.Y) {
        LogError("Font atlas out of space");
        return Vec2u(0, 0);
    }

    Vec2u slot = Cursor;

    Cursor.X += width;
    RowHeight = std::max(RowHeight, height);

    return slot;
}

void FontAtlas::RenderGlyph(FT_GlyphSlot glyph, uint32 codepoint)
{
    Assert(glyph);
    Assert(glyph->format == FT_GLYPH_FORMAT_BITMAP);

    FT_Bitmap& bitmap = glyph->bitmap;

    uint32 glyph_width = bitmap.width;
    uint32 glyph_height = bitmap.rows;


    if (glyph_width == 0 || glyph_height == 0) {
        GlyphMetrics metrics {};
        metrics.Advance = static_cast<float32>(glyph->advance.x) / 64.0f;
        metrics.Lsb = static_cast<float32>(glyph->metrics.horiBearingX) / 64.0f;
        metrics.Size = Vec2u::sZero;
        metrics.Bearing = Vec2f::sZero;
        Glyphs[codepoint] = metrics;
        return;
    }

    Vec2u slot = FindSlot(glyph_width, glyph_height);

    for (uint32 y = 0; y < glyph_height; ++y) {
        for (uint32 x = 0; x < glyph_width; ++x) {
            uint8 gray = bitmap.buffer[y * bitmap.pitch + x];
            AtlasData[slot.X + x + (slot.Y + y) * AtlasSize.X] = gray;
        }
    }

    GlyphMetrics metrics {};
    metrics.AtlasOffset = slot;
    metrics.Size = Vec2u(static_cast<uint32>(glyph_width), static_cast<uint32>(glyph_height));
    metrics.Bearing = Vec2f(static_cast<float32>(glyph->bitmap_left), static_cast<float32>(glyph->bitmap_top));
    metrics.Advance = static_cast<float32>(glyph->advance.x) / 64.0f;
    metrics.Lsb = static_cast<float32>(glyph->metrics.horiBearingX) / 64.0f;

    Glyphs[codepoint] = metrics;
}


SizedArray<uint8> FontAtlas::GetImageData(eImageFormat format) const
{
    const uint32 num_components = ImageFormatUtil::GetSize(format);

    const uint32 data_size = AtlasSize.X * AtlasSize.Y * num_components;

    SizedArray<uint8> image_data;
    image_data.InitSize(data_size);

    for (uint32 y = 0; y < AtlasSize.Y; y++) {
        for (uint32 x = 0; x < AtlasSize.X; x++) {
            uint32 index = (y * AtlasSize.X + x);

            const uint8 value = AtlasData[index];
            const uint32 offset = index * num_components;

            // Set each component to the grayscale value in the font atlas (including alpha).
            memset(image_data.pData + offset, value, num_components);
        }
    }

    return image_data;
}

void FontAtlas::Upload(renderer::Image& out_image)
{
    SizedArray<uint8> image_data = GetImageData(eImageFormat::RGBA8_UNorm);


    RawGpuBuffer rgba_staging;
    rgba_staging.Create(eGpuBufferType::Transfer, image_data.Size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                        eGpuBufferFlags::TransferReceiver);
    rgba_staging.Upload(image_data);

    const VkImageUsageFlags usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    out_image.Create(eImageType::Flat, AtlasSize, eImageFormat::RGBA8_UNorm, VK_IMAGE_TILING_OPTIMAL, usage_flags,
                     eImageAspectFlag::Color);

    Fx_Fwd_SubmitUploadCmd(
        [&](CommandBuffer& cmd)
        {
            out_image.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 1,
                                       TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                   .DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT });

            VkBufferImageCopy copy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageExtent = VkExtent3D { .width = AtlasSize.X, .height = AtlasSize.Y, .depth = 1 },
            };

            vkCmdCopyBufferToImage(cmd, rgba_staging.Buffer, out_image.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);

            out_image.TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 1,
                                       TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                   .DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT });
        });
}

/////////////////////////////////////
// Font functions
/////////////////////////////////////

bool Font::LoadFromFile(const String& path, float font_size)
{
    FT_Library library;
    FT_Error error = FT_Init_FreeType(&library);
    if (error != 0) {
        ModulePanic("Failed to initialize FreeType");
        return false;
    }

    FT_Face face;
    error = FT_New_Face(library, path.CStr(), 0, &face);
    if (error != 0) {
        LogError("Failed to load font from file: {}", path);
        FT_Done_FreeType(library);
        return false;
    }

    Library = library;
    Face = face;
    FontSize = font_size;

    InitCommon(font_size);

    return true;
}

void Font::WriteMetaFile(const String& atlas_path) const
{
    ConfigFile cf;

    cf.AddEntry(ConfigEntry::Literal("Atlas", atlas_path.Str()));
    cf.AddEntry(ConfigEntry::Literal("Family", FamilyName.Str()));
    cf.AddEntry(ConfigEntry::Literal("Style", StyleName.Str()));

    ConfigEntry* glyphs_entry = cf.AddEntry("Glyphs");

    for (const std::pair<uint32, GlyphMetrics>& value : Atlas.Glyphs) {
        const GlyphMetrics& metrics = value.second;

        ConfigEntry entry = ConfigEntry::Struct(std::format("{}", value.first));


        // Bearings
        {
            ConfigEntry bearing_entry = ConfigEntry::Array("Bearing", ConfigEntry::eValueType::Float);
            // Left bearing
            bearing_entry.AppendValue(metrics.Lsb);
            // Vertical bearing
            bearing_entry.AppendValue(metrics.Bearing.X);
            bearing_entry.AppendValue(metrics.Bearing.Y);

            entry.AddMember(std::move(bearing_entry));
        }


        {
            ConfigEntry size_entry = ConfigEntry::Array("Size", ConfigEntry::eValueType::Int);
            size_entry.AppendValue(metrics.Size.X);
            size_entry.AppendValue(metrics.Size.Y);

            entry.AddMember(std::move(size_entry));
        }

        {
            ConfigEntry offset_entry = ConfigEntry::Array("Offset", ConfigEntry::eValueType::Int);
            offset_entry.AppendValue(metrics.AtlasOffset.X);
            offset_entry.AppendValue(metrics.AtlasOffset.Y);

            entry.AddMember(std::move(offset_entry));
        }

        glyphs_entry->AddMember(std::move(entry));
    }

    cf.Write("./Meta.conf");
}

void Font::SaveToFile(const String& path, eImageSaveFormat file_format) const
{
    static constexpr eImageFormat scSaveFormat = eImageFormat::RGBA8_UNorm;

    WriteMetaFile(path);

    SizedArray<uint8> image_data = Atlas.GetImageData(scSaveFormat);
    AxLoaderStb::SaveToFile(file_format, image_data, Atlas.GetSize(), path, eImageSaveFlags::None);
}

bool Font::LoadFromMemory(const uint8* data, uint64 data_size, float font_size)
{
    FT_Library library;
    FT_Error error = FT_Init_FreeType(&library);

    if (error != 0) {
        ModulePanic("Failed to initialize FreeType");
        return false;
    }

    FT_Face face;
    error = FT_New_Memory_Face(library, data, static_cast<FT_Long>(data_size), 0, &face);
    if (error != 0) {
        LogError("Failed to load font from memory");
        FT_Done_FreeType(library);
        return false;
    }

    Library = library;
    Face = face;
    FontSize = font_size;

    InitCommon(font_size);

    return true;
}

void Font::InitCommon(float32 font_size)
{
    FT_Set_Pixel_Sizes(Face, 0, static_cast<FT_UInt>(font_size));

    FamilyName = Face->family_name ? Face->family_name : "Unknown";
    StyleName = Face->style_name ? Face->style_name : "Regular";

    Atlas.Create(512, 256, font_size);

    BakeAtlas();
}

void Font::BakeAtlas()
{
    static constexpr uint32 scFirstChar = '!';
    static constexpr uint32 scLastChar = '~';

    for (uint32 char_code = scFirstChar; char_code < scLastChar; char_code++) {
        FT_Error error = FT_Load_Char(Face, char_code, FT_LOAD_RENDER);

        if (error != 0) {
            LogError("Error loading glyph");
            continue;
        }

        Atlas.RenderGlyph(Face->glyph, char_code);
    }
}

void Font::RenderAtlasToImage(Image& out_image) { Atlas.Upload(out_image); }

void Font::Destroy()
{
    if (Face) {
        FT_Done_Face(Face);
        Face = nullptr;
    }

    if (Library) {
        FT_Done_FreeType(Library);
        Library = nullptr;
    }

    Atlas.Destroy();
}


void FontAtlas::Destroy()
{
    AtlasData.Free();
    Glyphs.clear();
    Cursor = Vec2u::sZero;
    RowHeight = 0;
}


} // namespace fx::renderer
