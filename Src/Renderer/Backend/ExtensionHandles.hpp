#pragma once

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

namespace fx {

using VoidFunction = void (*)(void);

template <typename TFunc>
TFunc GetExtensionFunc(VkInstance instance, const char* name)
{
    VoidFunction handle = vkGetInstanceProcAddr(instance, name);

    if (!handle) {
        LogError("Could not load extension function '{}'", name);
    }

    return reinterpret_cast<TFunc>(handle);
}

VkResult Rx_EXT_SetDebugUtilsObjectName(VkInstance instance, VkDevice device,
                                        const VkDebugUtilsObjectNameInfoEXT* pNameInfo);

VkResult Rx_EXT_CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkDebugUtilsMessengerEXT* pDebugMessenger);

void Rx_EXT_DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                       const VkAllocationCallbacks* pAllocator);

} // namespace fx
