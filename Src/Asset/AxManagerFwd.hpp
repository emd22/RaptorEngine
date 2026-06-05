#pragma once

#include <Asset/AxImage.hpp>

namespace fx::AssetManagerFwd {

TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size);
TSRef<AxImage> LoadImageFromPixels(eImageFormat format, const uint8* data, uint32 data_size);

}; // namespace fx::AssetManagerFwd
