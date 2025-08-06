#include "RvkExtensionHandles.hpp"

RvkVoidFunction RvkGetExtensionFunc_(VkInstance instance, const char *name)
{
    RvkVoidFunction raw_ptr = vkGetInstanceProcAddr(instance, name);

    if (raw_ptr != nullptr) {
        return raw_ptr;
    }

    Log::Error("Could not load extension function '%s'", name);
    return nullptr;
}


VkResult Rvk_EXT_SetDebugUtilsObjectName(VkInstance instance, VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
{
    using TFn = VkResult (*)(VkDevice, const VkDebugUtilsObjectNameInfoEXT*);
    const auto ext_function = RvkGetExtensionFunc<TFn>(instance, "vkSetDebugUtilsObjectNameEXT");
    return ext_function(device, pNameInfo);
}


VkResult Rvk_EXT_CreateDebugUtilsMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger
)
{
    using TFn = VkResult (*)(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT *, const VkAllocationCallbacks *, VkDebugUtilsMessengerEXT *);

    const auto ext_function = RvkGetExtensionFunc<TFn>(instance, "vkCreateDebugUtilsMessengerEXT");
    return ext_function(instance, pCreateInfo, pAllocator, pDebugMessenger);
}




void Rvk_EXT_DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator) {
    using TFn = void (*)(VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks *);

    const auto ext_function = RvkGetExtensionFunc<TFn>(instance, "vkDestroyDebugUtilsMessengerEXT");
    return ext_function(instance, messenger, pAllocator);
}
