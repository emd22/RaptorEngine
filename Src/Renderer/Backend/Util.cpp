#include "Util.hpp"

#include "ExtensionHandles.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

void Util::SetDebugLabel_(const char* name, VkObjectType object_type, unsigned long long obj_handle)
{
	VkDebugUtilsObjectNameInfoEXT debug_info { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
											   .pNext = nullptr,
											   .objectType = object_type,
											   .objectHandle = static_cast<uint64>(obj_handle),
											   .pObjectName = name };

	const GpuDevice* device = gGraphics->GetDevice();

	Rx_EXT_SetDebugUtilsObjectName(gGraphics->GetVulkanInstance(), device->Device, &debug_info);
}

} // namespace fx::renderer
