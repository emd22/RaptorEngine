#pragma once

#include <Core/Ref.hpp>
#include <Math/Vec2.hpp>

struct SDL_Window;

namespace fx {

class Window
{
public:
    static Ref<Window> New(const char* title, const Vec2u& size) { return Ref<Window>::New(title, size); }

public:
    Window() = default;
    Window(const Window& other) = delete;
    Window(const char* title, const Vec2u& size) { Create(title, size); }

    void Create(const char* title, const Vec2u& size);

    void HandleResize();

    float32 GetAspectRatio() const
    {
        return static_cast<float32>(mSize.X) / static_cast<float32>(mSize.Y);
    }

    FX_FORCE_INLINE const Vec2u& GetSize() const { return mSize; }
    FX_FORCE_INLINE SDL_Window* GetWindow() { return mWindow; }

    ~Window();

public:
private:
    Vec2u mSize = Vec2u::sZero;
    SDL_Window* mWindow;
};

} // namespace fx
