#pragma once

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

typedef void (* RvkVoidFunction)(void);

/**
 * @brief Get the raw extension function pointer
 */
RvkVoidFunction RvkGetExtensionFunc_(VkInstance instance, const char* name);

template <typename TFunc>
TFunc RvkGetExtensionFunc(VkInstance instance, const char* name)
{
    return reinterpret_cast<TFunc>(RvkGetExtensionFunc_(instance, name));
}

VkResult Rvk_EXT_SetDebugUtilsObjectName(VkInstance instance, VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo);
VkResult Rvk_EXT_CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
void     Rvk_EXT_DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator);
