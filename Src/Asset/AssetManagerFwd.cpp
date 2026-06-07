#include "AssetManagerFwd.hpp"

#include "AxManager.hpp"

#include <Engine.hpp>

namespace fx {

namespace AssetManagerFwd {

TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size)
{
    return gAssetManager->LoadImageFromMemory(format, data, data_size);
}

TSRef<AxImage> LoadImageFromPixels(const ImageInfo& img_info) { return gAssetManager->LoadImageFromPixels(img_info); }

} // namespace AssetManagerFwd

} // namespace fx
