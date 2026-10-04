#include "Swapchain.hpp"

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Renderer/TiledForwardRenderer.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Swapchain")

void Swapchain::Init(Vec2u size, VkSurfaceKHR surface, GpuDevice* device)
{
	mDevice = device;

	CreateSwapchain(size, surface);
	CreateSwapchainImages();
	CreateImageViews();
	CreateFramebuffers();

	bInitialized = true;
}

void Swapchain::Rebuild(Vec2u new_size, VkSurfaceKHR surface)
{
	// Wait until the GPU has stopped processing commands and all images are unbound
	mDevice->WaitForIdle();


	LogInfo(LC_RENDER, "Recreating Swapchain");

	CreateSwapchain(new_size, surface);
	CreateSwapchainImages();
	CreateImageViews();
}

void Swapchain::CreateSwapchainImages()
{
	OutputImages.Free();

	RxGpuDevice* device = mDevice->GetRustDevice();

	const uint32 image_count = rx_gpu_swapchain_images(device, RxRaw(mSwapchain), nullptr, 0);

	SizedArray<uint64> raw_images;
	raw_images.InitSize(image_count);

	rx_gpu_swapchain_images(device, RxRaw(mSwapchain), raw_images.pData, image_count);

	OutputImages.InitCapacity(image_count);

	for (uint64& raw_image : raw_images) {
		Image* image = OutputImages.Insert();
		image->InternalImage = RxFromRaw<VkImage>(raw_image);
		image->View = nullptr;
		image->Allocation = nullptr;
		image->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image->Info.Format = Surface.Format;

		image->Info.Size = Extent;
		image->Info.MipLevel = 0;
		image->Info.MipCount = 1;
	}
}

void Swapchain::CreateFramebuffers() {}

void Swapchain::CreateImageViews()
{
	RxGpuDevice* device = mDevice->GetRustDevice();

	for (int32 i = 0; i < OutputImages.Size; i++) {
		if (OutputImages[i].View != nullptr) {
			rx_gpu_view_destroy(device, RxRaw(OutputImages[i].View));
		}

		uint64 view = 0;

		const VkResult status = static_cast<VkResult>(rx_gpu_color_view_create(
			device, RxRaw(OutputImages[i].InternalImage), static_cast<uint16>(Surface.Format), &view));

		if (status != VK_SUCCESS) {
			ModulePanicVulkan("Could not create swapchain image view", status);
		}

		OutputImages[i].View = RxFromRaw<VkImageView>(view);
	}
}

void Swapchain::CreateSwapchain(Vec2u size, VkSurfaceKHR surface)
{
	Extent = size;

	RxSwapchainResult result {};

	if (rx_gpu_swapchain_create(mDevice->GetRustDevice(), RxRaw(surface), size.X, size.Y, RxRaw(mSwapchain),
								&result) == 0) {
		ModulePanic("Could not create swapchain");
	}

	mSwapchain = RxFromRaw<VkSwapchainKHR>(result.handle);

	Surface.Format = static_cast<eImageFormat>(result.format);
	Surface.ColorSpace = static_cast<VkColorSpaceKHR>(result.color_space);
}

void Swapchain::DestroyFramebuffersAndImageViews()
{
	for (int i = 0; i < FramesInFlight; i++) {
		// HACK: Clear the images so that we only destroy the image view.
		OutputImages[i].InternalImage = nullptr;
	}

	OutputImages.Free();
}

void Swapchain::DestroyInternalSwapchain() { rx_gpu_swapchain_destroy(mDevice->GetRustDevice(), RxRaw(mSwapchain)); }

void Swapchain::Destroy()
{
	if (!bInitialized) {
		return;
	}

	DestroyFramebuffersAndImageViews();
	DestroyInternalSwapchain();

	bInitialized = false;
}

Swapchain::~Swapchain() { Destroy(); }

} // namespace fx::renderer
