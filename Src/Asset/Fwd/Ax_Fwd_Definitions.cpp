#include "Ax_Fwd_Manager.hpp"

#include <Asset/AxManager.hpp>
#include <Engine.hpp>

namespace fx {


namespace Fwd::AssetManager {

TSRef<AxImage> LoadImageFromMemory(renderer::eImageFormat format, const uint8* data, uint32 data_size)
{
    return gAssetManager->LoadImageFromMemory(format, data, data_size);
}

} // namespace Fwd::AssetManager

} // namespace fx
