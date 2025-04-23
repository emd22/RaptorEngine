#include "Device.hpp"
#include "Core/Defines.hpp"
#include "Core/StaticArray.hpp"

namespace vulkan {

#include "vulkan/vulkan_core.h"

#include <Core/Log.hpp>
#include <climits>

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("Device")

///////////////////////////////
// Queue Families
///////////////////////////////


void QueueFamilies::FindQueueFamilies(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);

    RawFamilies.InitSize(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, RawFamilies.Data);

    Log::Info("Amount of queue families: %d", family_count);

    uint32 index = 0;
    for (const auto &family : RawFamilies) {
        if (mPresentIndex != UINT_MAX && mGraphicsIndex != UINT_MAX) {
            break;
        }

        if (family.queueCount == 0)
            continue;

        // check for a graphics family
        {
            if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                mGraphicsIndex = index;
            }
        }

        // check for a present family
        {
            uint32 present_support = 0;

            VkResult status = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &present_support);
            if (status != VK_SUCCESS) {
                Log::Error("Could not retrieve physical device surface support", status);
                continue;
            }

            Log::Info("Present support: %d\n", present_support);
            if (present_support > 0) {
                mPresentIndex = index;
            }
        }

        index++;
    }
}


///////////////////////////////
// GPU Device
///////////////////////////////

bool GPUDevice::IsPhysicalDeviceSuitable(VkPhysicalDevice &physical)
{
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;

    QueueFamilies new_families;
    new_families.FindQueueFamilies(physical, mSurface);

    vkGetPhysicalDeviceFeatures(physical, &features);
    vkGetPhysicalDeviceProperties(physical, &properties);

    const uint32 version = properties.apiVersion;

    if (version > VK_MAKE_VERSION(1, 2, 0) && new_families.IsComplete()) {
        return true;
    }

    Log::Info("Device not suitable: (Vk: %d.%d.%d), Graphics?: %s, Present?: %s, IsComplete?: %s",
        VK_VERSION_MAJOR(version),
        VK_VERSION_MINOR(version),
        VK_VERSION_PATCH(version),
        Log::YesNo(new_families.GetGraphicsFamily() != QueueFamilies::QueueNull),
        Log::YesNo(new_families.GetPresentFamily() != QueueFamilies::QueueNull)
    );

    return false;
}

void GPUDevice::QueryQueues()
{
    const auto indices = mQueueFamilies.GetQueueIndexList();

    if (indices.size() < 2) {
        FxPanic("Not enough queue families", 0);
    }

    vkGetDeviceQueue(Device, indices[0], 0, &GraphicsQueue);
    vkGetDeviceQueue(Device, indices[1], 0, &PresentQueue);
}

void GPUDevice::CreateLogicalDevice()
{
    if (Physical == nullptr) {
        PickPhysicalDevice();
    }

    if (!mQueueFamilies.IsComplete()) {
        mQueueFamilies.FindQueueFamilies(Physical, mSurface);
    }

    const auto queue_family_indices = mQueueFamilies.GetQueueIndexList();

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(queue_family_indices.size());

    const float queue_priority = 1.0f;

    int index = 0;

    for (const auto family_index : queue_family_indices) {
        if (family_index == QueueFamilies::QueueNull) {
            continue;
        }

        queue_create_infos.push_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family_index,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority
        });

        // check if our queue family index's are the same
        {
            const uint32 next_index = index + 1;
            // if the next queue family (presentation) is the same as the current one, return as we
            // do not need to create a new queue family
            if (next_index < queue_family_indices.size() && family_index == queue_family_indices[next_index]) {
                break;
            }
        }

        index++;
    }

    const VkPhysicalDeviceFeatures device_features{};
    const char *device_extensions[] = {
        // TODO: ifdef macOS/__APPLE__ here
        "VK_KHR_portability_subset",
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_create_infos.data(),
        .queueCreateInfoCount = (uint32)queue_create_infos.size(),
        .pEnabledFeatures = &device_features,

        // device specific extensions
        .enabledExtensionCount = sizeof(device_extensions) / sizeof(device_extensions[0]),
        .ppEnabledExtensionNames = device_extensions,

        // device specific layers (not used in modern vulkan)
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
    };

    const VkResult status = vkCreateDevice(Physical, &create_info, nullptr, &Device);

    if (status != VK_SUCCESS) {
        FxPanic("Could not create logical device", status);
    }

    QueryQueues();
}

void GPUDevice::Create(VkInstance instance, VkSurfaceKHR surface)
{
    mInstance = instance;
    mSurface = surface;

    PickPhysicalDevice();

    mQueueFamilies.FindQueueFamilies(Physical, surface);

    CreateLogicalDevice();
}

void GPUDevice::Destroy()
{
    vkDestroyDevice(Device, nullptr);
}

VkSurfaceFormatKHR GPUDevice::GetBestSurfaceFormat()
{
    uint32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, mSurface, &format_count, nullptr);

    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, mSurface, &format_count, surface_formats.data());

    for (const auto &format : surface_formats) {
        if (format.format == VK_FORMAT_B8G8R8_SRGB) {
            return format;
        }
    }

    return surface_formats[0];
}

void GPUDevice::PickPhysicalDevice()
{
    // enumerate physical devices

    const char * enum_err = "Could not enumerate physical devices";

    uint32 device_count;
    VkTry(vkEnumeratePhysicalDevices(mInstance, &device_count, nullptr), enum_err);


    if (device_count == 0) {
        FxPanic("No usable physical devices found (no vulkan support!)", 0);
    }

    StaticArray<VkPhysicalDevice> physical_devices(device_count);
    physical_devices.InitSize();

    VkTry(vkEnumeratePhysicalDevices(mInstance, &device_count, physical_devices.Data), enum_err);

    for (VkPhysicalDevice &device : physical_devices) {
        if (IsPhysicalDeviceSuitable(device)) {
            Physical = device;
            break;
        }
    }
    if (Physical == nullptr) {
        FxPanic("Could not find a suitable physical device!", 0);
    }
}

void GPUDevice::WaitForIdle()
{
    vkDeviceWaitIdle(Device);
}

}; // namespace vulkan
