#include "ExtensionHandles.hpp"

#include <Core/Log.hpp>

namespace fx {

VkResult Rx_EXT_SetDebugUtilsObjectName(VkInstance instance, VkDevice device,
                                        const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
{
    using TFn = VkResult (*)(VkDevice, const VkDebugUtilsObjectNameInfoEXT*);
    const auto ext_function = GetExtensionFunc<TFn>(instance, "vkSetDebugUtilsObjectNameEXT");
    return ext_function(device, pNameInfo);
}

} // namespace fx
