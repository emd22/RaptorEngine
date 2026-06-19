#include "Device.hpp"

#include "Core/Defines.hpp"
#include "Core/SizedArray.hpp"

#include <vulkan/vulkan.h>

#include <Core/Assert.hpp>

FX_SET_MODULE_NAME("Device")

// This isn't defined on some platforms/drivers.
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

namespace fx::renderer {

///////////////////////////////
// Queue Families
///////////////////////////////

void QueueFamilies::FindGraphicsFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    for (int32 index = 0; index < RawFamilies.Size; index++) {
        VkQueueFamilyProperties& family_props = RawFamilies[index];

        // Presentation families will usually always have graphics support. Best to choose the most
        // capable here as our main family.
        if ((family_props.queueFlags & VK_QUEUE_GRAPHICS_BIT) && FamilyHasPresentSupport(device, surface, index)) {
            mGraphicsIndex = index;
            mPresentIndex = index;

            return;
        }
    }
}

void QueueFamilies::FindTransferFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    for (int32 index = 0; index < RawFamilies.Size; index++) {
        VkQueueFamilyProperties& family_props = RawFamilies[index];

        // We want a separate queue here for loading data onto the graphics card
        if ((family_props.queueFlags & VK_QUEUE_TRANSFER_BIT) && index != mGraphicsIndex) {
            mTransferIndex = index;
            return;
        }
    }

    Assert(mGraphicsIndex != scNullQueue);

    // There is no independent transfer queue available, use the graphics queue.
    //
    VkQueueFamilyProperties& graphics_props = RawFamilies[mGraphicsIndex];
    if (!(graphics_props.queueFlags & VK_QUEUE_TRANSFER_BIT)) {
        Panic("Device", "Could not find a queue that supports transfer!");
    }
    mTransferIndex = mGraphicsIndex;
}

bool QueueFamilies::FamilyHasPresentSupport(VkPhysicalDevice device, VkSurfaceKHR surface, uint32 family_index)
{
    uint32 support = 0;

    VkResult status = vkGetPhysicalDeviceSurfaceSupportKHR(device, family_index, surface, &support);

    if (status != VK_SUCCESS) {
        LogError("Could not query present support for family at index {:d}", family_index);
        return false;
    }

    return support > 0;
}

void QueueFamilies::FindQueueFamilies(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);

    RawFamilies.InitSize(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, RawFamilies.pData);

    LogInfo(LC_RENDER, "Amount of queue families: {:d}", family_count);


    FindGraphicsFamily(physical_device, surface);
    FindTransferFamily(physical_device, surface);
    // uint32 index = 0;
    // for (const auto &family : RawFamilies) {
    //     if (mPresentIndex != QueueNull && mGraphicsIndex != QueueNull && mTransferIndex != QueueNull) {
    //         break;
    //     }

    //     if (family.queueCount == 0)
    //         continue;

    //     // check for a graphics family
    //     {
    //         if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
    //             mGraphicsIndex = index;
    //         }
    //     }

    //     // check for a present family
    //     {
    //         uint32 present_support = 0;

    //         VkResult status = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface,
    //         &present_support); if (status != VK_SUCCESS) {
    //             Log::Error("Could not retrieve physical device surface support", status);
    //             continue;
    //         }

    //         Log::Info("Present support: %d\n", present_support);
    //         if (present_support > 0) {
    //             mPresentIndex = index;
    //         }
    //     }

    //     if (mPresentIndex != QueueNull && mGraphicsIndex != QueueNull && mTransferIndex == QueueNull &&
    //     (family.queueFlags & VK_QUEUE_TRANSFER_BIT)) {
    //         mTransferIndex = index;
    //     }

    //     index++;
    // }
}


///////////////////////////////
// GPU Device
///////////////////////////////

bool GpuDevice::IsPhysicalDeviceSuitable(VkPhysicalDevice& physical)
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

    LogInfo(LC_RENDER, "Device not suitable: (Vk: {}.{}.{}), Graphics?: {}, Present?: {}, Xfer?: {}, IsComplete?: {}",
            VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version),
            (new_families.GetGraphicsFamily() != QueueFamilies::scNullQueue),
            (new_families.GetPresentFamily() != QueueFamilies::scNullQueue),
            (new_families.GetTransferFamily() != QueueFamilies::scNullQueue), (new_families.IsComplete()));

    return false;
}

void GpuDevice::QueryQueues()
{
    vkGetDeviceQueue(Device, mQueueFamilies.GetGraphicsFamily(), 0, &mGraphicsQueue);
    vkGetDeviceQueue(Device, mQueueFamilies.GetPresentFamily(), 0, &mPresentQueue);
    vkGetDeviceQueue(Device, mQueueFamilies.GetTransferFamily(), 0, &mTransferQueue);


    AssertMsg(mTransferQueue != nullptr, "Queue cannot be null!");
    AssertMsg(mPresentQueue != nullptr, "Queue cannot be null!");
    AssertMsg(mGraphicsQueue != nullptr, "Queue cannot be null!");
}

bool GpuDevice::SupportsPortabilityExtension() const
{
    uint32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(Physical, nullptr, &extension_count, nullptr);
    SizedArray<VkExtensionProperties> exts;
    exts.InitSize(extension_count);
    vkEnumerateDeviceExtensionProperties(Physical, nullptr, &extension_count, exts.pData);

    for (const VkExtensionProperties& ext : exts) {
        if (!std::strncmp(ext.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, 256)) {
            return true;
        }
    }

    return false;
}


void GpuDevice::CreateLogicalDevice()
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

    const float graphics_priorities[] = { 1.0f };
    float transfer_priorities[] = { 1.0f };

    queue_create_infos.push_back(VkDeviceQueueCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = mQueueFamilies.GetGraphicsFamily(),
        .queueCount = 1,
        .pQueuePriorities = graphics_priorities,
    });

    // If the device has an independent transfer queue, push the new queue to be created.
    // This is the best scenario as it means we can have fully async uploads. Drivers such as KosmicKrisp do not allow
    // this as Metal doesnt offer something equivalent. For some reason MoltenVK emulates this through 4 "virtual"
    // queues, but thats essentially an internal mutex.
    if (mQueueFamilies.HasIndependentTransfer()) {
        queue_create_infos.push_back(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = mQueueFamilies.GetTransferFamily(),
            .queueCount = 1,
            .pQueuePriorities = transfer_priorities,
        });
    }

    const VkPhysicalDeviceFeatures device_features {};

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    };

    const bool requires_portability_extension = SupportsPortabilityExtension();

    if (requires_portability_extension) {
        LogInfo(LC_RENDER, "Device is using portability extension!");
        device_extensions.emplace_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    VkPhysicalDevicePortabilitySubsetFeaturesKHR portability_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
        .pNext = nullptr,
        .mutableComparisonSamplers = VK_TRUE, // For samplers that use compareOp's / SampleCmp in shaders
    };

    VkPhysicalDeviceVulkan11Features vk11_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = nullptr,
        .shaderDrawParameters = VK_TRUE,

    };

    if (requires_portability_extension) {
        vk11_features.pNext = &portability_features;
    }

    {
        VkPhysicalDeviceDriverProperties driver_properties {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        };

        VkPhysicalDeviceProperties2 physical_properties {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &driver_properties,
        };

        vkGetPhysicalDeviceProperties2(Physical, &physical_properties);

        LogInfo(LC_RENDER, "Creating device for physical device (Id={}) {} -- driver {}",
                physical_properties.properties.deviceID, physical_properties.properties.deviceName,
                driver_properties.driverName);
    }


    const VkDeviceCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk11_features,

        .queueCreateInfoCount = static_cast<uint32>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),

        // Device specific layers. Not used in modern Vulkan.
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,

        // Device specific extensions
        .enabledExtensionCount = static_cast<uint32>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),

        .pEnabledFeatures = &device_features,

    };

    const VkResult status = vkCreateDevice(Physical, &create_info, nullptr, &Device);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create logical device", status);
    }

    QueryQueues();
}

void GpuDevice::Create(VkInstance instance, VkSurfaceKHR surface)
{
    mInstance = instance;
    mSurface = surface;

    PickPhysicalDevice();

    mQueueFamilies.FindQueueFamilies(Physical, surface);

    CreateLogicalDevice();

    LogInfo(LC_RENDER, "Device Queue Families: \n\tPresent: {:d}\n\tGraphics: {:d}\n\tTransfer: {:d}",
            mQueueFamilies.GetPresentFamily(), mQueueFamilies.GetGraphicsFamily(), mQueueFamilies.GetTransferFamily());
}

void GpuDevice::Destroy()
{
    vkDestroyDevice(Device, nullptr);

    Device = nullptr;
}

VkSurfaceFormatKHR GpuDevice::GetSurfaceFormat()
{
    uint32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, mSurface, &format_count, nullptr);

    SizedArray<VkSurfaceFormatKHR> surface_formats;
    surface_formats.InitSize(format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, mSurface, &format_count, surface_formats.pData);

    VkSurfaceFormatKHR best_format = surface_formats[0];

    for (VkSurfaceFormatKHR surface_format : surface_formats) {
        if (surface_format.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            return surface_format;
        }

        if (surface_format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            best_format = surface_format;
        }
    }

    return best_format;
}

void GpuDevice::PickPhysicalDevice()
{
    // enumerate physical devices
    //
    uint32 device_count;
    VkTry(vkEnumeratePhysicalDevices(mInstance, &device_count, nullptr), "Could not enumerate physical devices");

    if (device_count == 0) {
        ModulePanic("No usable physical devices found (no vulkan support!)", 0);
    }

    SizedArray<VkPhysicalDevice> physical_devices;
    physical_devices.InitSize(device_count);

    VkTry(vkEnumeratePhysicalDevices(mInstance, &device_count, physical_devices.pData),
          "Could not enumerate physical devices");

    for (VkPhysicalDevice& device : physical_devices) {
        if (IsPhysicalDeviceSuitable(device)) {
            Physical = device;
            break;
        }
    }
    if (Physical == nullptr) {
        ModulePanic("Could not find a suitable physical device!", 0);
    }
}

void GpuDevice::WaitForIdle()
{
    if (!Device) {
        return;
    }

    vkDeviceWaitIdle(Device);
}

GpuDevice::~GpuDevice()
{
    Device = nullptr;
    Physical = nullptr;
}

} // namespace fx::renderer
