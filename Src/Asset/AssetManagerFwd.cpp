#include "AssetManagerFwd.hpp"

#include "AssetManager.hpp"

#include <Engine.hpp>

namespace fx {

namespace AssetManagerFwd {

AssetTicket LoadImageFromMemory(eImageType image_type, eImageFormat format, const Slice<const uint8> data,
								eImageCreateFlags flags)
{
	return gAssetManager->LoadImageFromMemory(image_type, format, data, flags);
}

} // namespace AssetManagerFwd

} // namespace fx
