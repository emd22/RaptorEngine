#include "Ax_Fwd_Manager.hpp"

#include <Asset/AxManager.hpp>
#include <Engine.hpp>

namespace fx {


namespace Fwd::AssetManager {

TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size)
{
    return gAssetManager->LoadImageFromMemory(format, data, data_size);
}

TSRef<AxImage> LoadImageFromPixels(eImageFormat format, const uint8* pixels, uint32 data_size)
{
    return gAssetManager->LoadImageFromPixels(format, pixels, data_size);
}

} // namespace Fwd::AssetManager

} // namespace fx
