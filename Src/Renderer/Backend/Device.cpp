#include "Device.hpp"

#include "Core/Defines.hpp"

#include <Core/Assert.hpp>

FX_SET_MODULE_NAME("Device")

namespace fx::renderer {

void GpuDevice::Create(RxGpuInstance* instance, VkSurfaceKHR surface)
{
	mpRustDevice = rx_gpu_device_create(instance, reinterpret_cast<uint64>(surface));

	if (mpRustDevice == nullptr) {
		ModulePanic("Could not create the GPU device", 0);
	}

	const RxGpuDeviceInfo* info = rx_gpu_device_info(mpRustDevice);

	Physical = reinterpret_cast<VkPhysicalDevice>(info->physical);
	Device = reinterpret_cast<VkDevice>(info->device);

	mQueueFamilies.Set(info->graphics_family, info->present_family, info->transfer_family);

	MaxSamplerAnisotropy = info->max_sampler_anisotropy;
	bSupportsCubeArrays = (info->supports_cube_arrays != 0);
}

void GpuDevice::Destroy()
{
	rx_gpu_device_free(mpRustDevice);

	mpRustDevice = nullptr;
	Device = nullptr;
}

VkSurfaceFormatKHR GpuDevice::GetSurfaceFormat()
{
	int32 format = 0;
	int32 color_space = 0;

	if (rx_gpu_device_surface_format(mpRustDevice, &format, &color_space) == 0) {
		ModulePanic("Could not choose a surface format", 0);
	}

	return VkSurfaceFormatKHR { .format = static_cast<VkFormat>(format),
								.colorSpace = static_cast<VkColorSpaceKHR>(color_space) };
}

void GpuDevice::WaitForIdle()
{
	if (!Device) {
		return;
	}

	rx_gpu_device_wait_idle(mpRustDevice);
}

GpuDevice::~GpuDevice()
{
	Device = nullptr;
	Physical = nullptr;
}

} // namespace fx::renderer
