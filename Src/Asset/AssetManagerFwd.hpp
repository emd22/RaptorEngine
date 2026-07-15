#pragma once

#include "AssetTicket.hpp"

#include <Renderer/Backend/Image.hpp>

namespace fx {

struct ImageInfo;

namespace AssetManagerFwd {

AssetTicket LoadImageFromMemory(eImageType image_type, eImageFormat format, const Slice<const uint8> data,
								eImageCreateFlags flags);

}; // namespace AssetManagerFwd
} // namespace fx
