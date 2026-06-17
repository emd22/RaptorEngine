#pragma once

#include <Asset/AxImage.hpp>

namespace fx {

struct ImageInfo;

namespace AssetManagerFwd {

TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size);
TSRef<AxImage> LoadImageFromPixels(const ImageInfo& img_info);
void LoadImageFromPixels(TSRef<AxImage>& image, const ImageInfo& img_info);

}; // namespace AssetManagerFwd
} // namespace fx
