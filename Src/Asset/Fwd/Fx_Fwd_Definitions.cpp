#include "Fx_Fwd_AssetManager.hpp"

#include <Asset/FxAssetManager.hpp>

namespace Fwd::AssetManager {

FxRef<FxAssetImage> LoadImageFromMemory(VkFormat format, const uint8* data, uint32 data_size)
{
    return FxAssetManager::LoadImageFromMemory(format, data, data_size);
}

} // namespace Fwd::AssetManager
