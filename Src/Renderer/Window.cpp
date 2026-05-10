#include "Window.hpp"

#include <SDL3/SDL.h>

#include <Core/Panic.hpp>
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

    mWindow = SDL_CreateWindow(title, static_cast<int32>(size.X), static_cast<int32>(size.Y), window_flags);


    if (mWindow == nullptr) {
        Panic("Window", "Could not create SDL window (SDL err: {})", SDL_GetError());
    }
}

const Vec2u& Window::GetSize()
{
    int width, height;
    bool success = SDL_GetWindowSize(mWindow, &width, &height);

    mSize = Vec2u(static_cast<uint32>(width), static_cast<uint32>(height));

    if (!success) {
        LogError("Error retrieving window size from SDL! (SDL err: {})", SDL_GetError());
        return mSize;
    }

    return mSize;
}

Window::~Window() { SDL_DestroyWindow(mWindow); }

} // namespace fx
