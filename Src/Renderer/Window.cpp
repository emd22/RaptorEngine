// The editor build replaces this with the wxWidgets window in Editor/EditorWindow.cpp
#ifndef FX_IS_EDITOR

#include "Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <Core/Assert.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>

namespace fx {

void Window::Create(const char* title, const Vec2u& size)
{
    mSize = size;

    if (size.X > 8000 || size.Y > 8000) {
        Panic("Window", "Window size is too large! (Size: {})", size);
    }

    const uint64 window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

    SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1");

    mWindow = SDL_CreateWindow(title, static_cast<int32>(size.X), static_cast<int32>(size.Y), window_flags);


    if (mWindow == nullptr) {
        Panic("Window", "Could not create SDL window (SDL err: {})", SDL_GetError());
    }

    HandleResize();
}

void Window::HandleResize()
{
    int width, height;

    // Get window size from SDL
    if (!SDL_GetWindowSize(mWindow, &width, &height)) {
        LogError(LC_RENDER, "Error retrieving window size from SDL! (SDL err: {})", SDL_GetError());
        return;
    }

    mSize = Vec2u(static_cast<uint32>(width), static_cast<uint32>(height));
}

const char* const* Window::GetRequiredInstanceExtensions(uint32* out_count)
{
    return SDL_Vulkan_GetInstanceExtensions(out_count);
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance)
{
    VkSurfaceKHR surface = nullptr;

    if (!SDL_Vulkan_CreateSurface(mWindow, instance, nullptr, &surface)) {
        Panic("Window", "Could not attach Vulkan instance to window! (SDL err: {})", SDL_GetError());
    }

    return surface;
}

void Window::SetRelativeMouseMode(bool enabled) { SDL_SetWindowRelativeMouseMode(mWindow, enabled); }

Vec2f Window::GetMousePosition() const
{
    float32 x, y;
    SDL_GetMouseState(&x, &y);

    return Vec2f(x, y);
}

void Window::WarpMouse(const Vec2f& position) { SDL_WarpMouseInWindow(mWindow, position.GetX(), position.GetY()); }

Window::~Window() { SDL_DestroyWindow(mWindow); }

} // namespace fx

#endif
