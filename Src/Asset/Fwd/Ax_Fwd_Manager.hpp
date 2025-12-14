#pragma once

#include <Asset/AxImage.hpp>

namespace Fwd::AssetManager {

FxRef<AxImage> LoadImageFromMemory(VkFormat format, const uint8* data, uint32 data_size);

}; // namespace Fwd::AssetManager
