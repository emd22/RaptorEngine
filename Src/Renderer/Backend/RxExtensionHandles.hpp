#pragma once

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

typedef void (* RxVoidFunction)(void);

/**
 * @brief Get the raw extension function pointer
 */
RxVoidFunction RxGetExtensionFunc_(VkInstance instance, const char* name);

template <typename TFunc>
TFunc RxGetExtensionFunc(VkInstance instance, const char* name)
{
    return reinterpret_cast<TFunc>(RxGetExtensionFunc_(instance, name));
}

VkResult Rx_EXT_SetDebugUtilsObjectName(VkInstance instance, VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo);
VkResult Rx_EXT_CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
void     Rx_EXT_DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator);
