#pragma once

#include <Asset/FxAssetImage.hpp>

namespace Fwd::AssetManager {

FxRef<FxAssetImage> LoadImageFromMemory(VkFormat format, const uint8* data, uint32 data_size);

}; // namespace Fwd::AssetManager
