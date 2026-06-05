#include "AxManagerFwd.hpp"

#include "AxManager.hpp"

#include <Engine.hpp>

namespace fx {


namespace AssetManagerFwd {

TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size)
{
    return gAssetManager->LoadImageFromMemory(format, data, data_size);
}

TSRef<AxImage> LoadImageFromPixels(eImageFormat format, const uint8* pixels, uint32 data_size)
{
    return gAssetManager->LoadImageFromPixels(format, pixels, data_size);
}

} // namespace AssetManagerFwd

} // namespace fx
