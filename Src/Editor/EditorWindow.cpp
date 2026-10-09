// fx::Window for the editor build. The game build uses the SDL window in Renderer/Window.cpp.

#include "EditorFrame.hpp"
#include "EditorPlatform.hpp"
#include "EditorThread.hpp"
#include "EditorViewport.hpp"
#include "Engine.hpp"
#include "RaptorEditor.hpp"

#include <wx/utils.h>

#include <Core/Assert.hpp>
#include <Renderer/Window.hpp>
#include <algorithm>
#include <string>

#if defined(FX_PLATFORM_MACOS)
#include <vulkan/vulkan_metal.h>
#elif defined(FX_PLATFORM_WINDOWS)
#include <windows.h>
// vulkan_win32.h needs the core Vulkan types and the Win32 types (HINSTANCE, HWND) declared before it
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#else
#error "The editor can only create a Vulkan surface on macOS and Windows"
#endif

namespace fx {

void Window::Create(const char* title, const Vec2u& size)
{
	if (size.X > 8000 || size.Y > 8000) {
		Panic("Window", "Window size is too large! (Size: {})", size);
	}

	mSize = size;

	editor::EditorFrame* frame = nullptr;
	editor::thread::RunOnUIAndWait([&frame, title, &size] { frame = gEditor->CreateMainFrame(title, size); });

	mpViewport = frame->GetViewport();

	HandleResize();
}

void Window::SetTitle(const char* title)
{
	editor::thread::PostToUI(
		[title = std::string(title)]
		{
			if (gEditor != nullptr && gEditor->GetMainFrame() != nullptr) {
				gEditor->GetMainFrame()->SetTitle(wxString::FromUTF8(title));
			}
		});
}

void Window::HandleResize()
{
	const Vec2u size = mpViewport->GetCachedSize();

	// A zero sized swapchain isn't valid, and the aspect ratio would divide by zero
	mSize = Vec2u(std::max(size.X, 1u), std::max(size.Y, 1u));
}

const char* const* Window::GetRequiredInstanceExtensions(uint32* out_count)
{
	static const char* const scExtensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(FX_PLATFORM_MACOS)
		VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#elif defined(FX_PLATFORM_WINDOWS)
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
	};

	*out_count = static_cast<uint32>(std::size(scExtensions));

	return scExtensions;
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance)
{
	VkSurfaceKHR surface = nullptr;
	VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;

#if defined(FX_PLATFORM_MACOS)
	auto create_surface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
		vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));

	if (create_surface != nullptr) {
		const VkMetalSurfaceCreateInfoEXT create_info {
			.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
			.pLayer = mpViewport->GetInternalSurface(),
		};

		result = create_surface(instance, &create_info, nullptr, &surface);
	}
#elif defined(FX_PLATFORM_WINDOWS)
	auto create_surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
		vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));

	if (create_surface != nullptr) {
		const VkWin32SurfaceCreateInfoKHR create_info {
			.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
			.hinstance = GetModuleHandleW(nullptr),
			.hwnd = static_cast<HWND>(mpViewport->GetHandle()),
		};

		result = create_surface(instance, &create_info, nullptr, &surface);
	}
#endif

	if (result != VK_SUCCESS) {
		Panic("Window", "Could not create a Vulkan surface for the editor viewport! (VkResult: {})",
			  static_cast<int32>(result));
	}

	return surface;
}

void Window::SetRelativeMouseMode(bool enabled)
{
	editor::EditorViewport* viewport = mpViewport;

	editor::thread::PostToUI([viewport, enabled] { viewport->SetRelativeMouseMode(enabled); });
}

bool Window::IsFocused() const { return gEditor->IsAppActive(); }

// The viewport belongs to the editor frame, which editor::Shutdown() destroys
Window::~Window() = default;

} // namespace fx
