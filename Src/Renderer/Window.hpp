#pragma once

#include <Core/Ref.hpp>
#include <Math/Vec2.hpp>

struct SDL_Window;

namespace fx {

class Window
{
public:
    static Ref<Window> New(const char* title, const Vec2i& size) { return Ref<Window>::New(title, size); }

public:
    Window() = default;
    Window(const Window& other) = delete;
    Window(const char* title, const Vec2i& size) { Create(title, size); }

    void Create(const char* title, const Vec2i& size);

    Vec2i GetSize();

    float32 GetAspectRatio()
    {
        Vec2i size = GetSize();
        return static_cast<float32>(size.X) / (size.Y);
    }

    SDL_Window* GetWindow() { return mWindow; }

    ~Window();

public:
private:
    Vec2i mSize = Vec2i::sZero;
    SDL_Window* mWindow;
};

} // namespace fx
