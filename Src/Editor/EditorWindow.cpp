// fx::Window for the editor build. The game build uses the SDL window in Renderer/Window.cpp.

#include "EditorApp.hpp"
#include "EditorFrame.hpp"
#include "EditorViewport.hpp"

#include <wx/utils.h>

#include <Core/Assert.hpp>
#include <Renderer/Window.hpp>

#if defined(FX_PLATFORM_MACOS)
#include <vulkan/vulkan_metal.h>
#elif defined(FX_PLATFORM_WINDOWS)
#include <vulkan/vulkan_win32.h>
#include <windows.h>
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
	mpViewport = editor::CreateMainFrame(title, size)->GetViewport();

	HandleResize();
}

void Window::HandleResize()
{
	const wxSize size = mpViewport->GetClientSize();

	// A zero sized swapchain isn't valid, and the aspect ratio would divide by zero
	mSize = Vec2u(static_cast<uint32>(std::max(size.x, 1)), static_cast<uint32>(std::max(size.y, 1)));
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

void Window::SetRelativeMouseMode(bool enabled) { mpViewport->SetRelativeMouseMode(enabled); }

Vec2f Window::GetMousePosition() const
{
	const wxPoint position = mpViewport->ScreenToClient(wxGetMousePosition());
	return Vec2f(static_cast<float32>(position.x), static_cast<float32>(position.y));
}

void Window::WarpMouse(const Vec2f& position)
{
	mpViewport->WarpPointer(static_cast<int>(position.GetX()), static_cast<int>(position.GetY()));
}

bool Window::IsFocused() const { return editor::GetMainFrame()->IsActive(); }

// The viewport belongs to the editor frame, which editor::Shutdown() destroys
Window::~Window() = default;

} // namespace fx
