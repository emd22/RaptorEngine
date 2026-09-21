#pragma once

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Math/Vec2.hpp>

struct SDL_Window;

namespace fx {

#ifdef FX_IS_EDITOR
namespace editor {
class EditorViewport;
}
#endif

/**
 * @brief The window the engine renders into.
 *
 * In the game this is an SDL window. When FX_IS_EDITOR is defined it is the viewport panel inside the wxWidgets editor
 * frame instead (see Editor/EditorWindow.cpp).
 */
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

	/**
	 * @brief Returns the instance extensions needed to create a surface for this kind of window.
	 */
	static const char* const* GetRequiredInstanceExtensions(uint32* out_count);

	/**
	 * @brief Creates a Vulkan surface that presents into this window. Panics on failure.
	 */
	VkSurfaceKHR CreateSurface(VkInstance instance);

	/// Hides the cursor and reports mouse movement as deltas without the cursor moving
	void SetRelativeMouseMode(bool enabled);

	/// The cursor position in window coordinates
	Vec2f GetMousePosition() const;

	/// Moves the cursor to a position in window coordinates
	void WarpMouse(const Vec2f& position);

	float32 GetAspectRatio() const
	{
		return static_cast<float32>(mSize.X) / static_cast<float32>(mSize.Y);
	}

	FX_FORCE_INLINE const Vec2u& GetSize() const { return mSize; }

#ifdef FX_IS_EDITOR
	FX_FORCE_INLINE editor::EditorViewport* GetViewport() { return mpViewport; }
#else
	FX_FORCE_INLINE SDL_Window* GetWindow() { return mWindow; }
#endif

	~Window();

public:
private:
	Vec2u mSize = Vec2u::sZero;

#ifdef FX_IS_EDITOR
	editor::EditorViewport* mpViewport = nullptr;
#else
	SDL_Window* mWindow = nullptr;
#endif
};

} // namespace fx
