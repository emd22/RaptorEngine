#pragma once

#include <Core/FxPanic.hpp>
#include <Core/Types.hpp>

#include <SDL3/SDL.h>
#include <memory>

class FxWindow {
public:
    FxWindow() = default;

    FxWindow(const char *title, int32 width, int32 height)
    {
        Create(title, width, height);
    }

    void Create(const char *title, int32 width, int32 height)
    {
        const uint64 window_flags = SDL_WINDOW_VULKAN;

        mWindow = SDL_CreateWindow(title, width, height, window_flags);

        if (mWindow == nullptr) {
            FxPanic("Window", "Could not create SDL window (SDL err: %s)", SDL_GetError());
        }
    }

    static std::shared_ptr<FxWindow> New(const char *title, int32 width, int32 height)
    {
        return std::shared_ptr<FxWindow>(new FxWindow(title, width, height));
    }

    SDL_Window *GetWindow() { return mWindow; }

    ~FxWindow()
    {
        SDL_DestroyWindow(mWindow);
    }

private:
    SDL_Window *mWindow;
};
