#pragma once

#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

/**
 * @brief Writes 8 bit RGBA pixels to a PNG or JPEG file. Returns whether it was written.
 */
bool SaveImageFile(eImageSaveFormat format, const Slice<const uint8>& data, const Vec2u& size, const String& path,
				   eImageSaveFlags flags = eImageSaveFlags::None);

} // namespace fx
