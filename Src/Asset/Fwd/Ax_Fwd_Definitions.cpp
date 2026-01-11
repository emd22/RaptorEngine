#include "Ax_Fwd_Manager.hpp"

#include <Asset/AxManager.hpp>

namespace Fwd::AssetManager {

FxRef<AxImage> LoadImageFromMemory(RxImageFormat format, const uint8* data, uint32 data_size)
{
    return AxManager::LoadImageFromMemory(format, data, data_size);
}

} // namespace Fwd::AssetManager
