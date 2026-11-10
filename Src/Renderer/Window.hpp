#pragma once

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Math/Vec2.hpp>

struct SDL_Window;

namespace fx {

enum class eWindowFlags
{
	None = (0),
	/// Raises the window above all current windows, focusing it.
	RaiseWindow = (1 << 0),
	/// Never puts the window on screen, for runs that do not draw anything.
	Hidden = (1 << 1),
};

FxEnumFlags(eWindowFlags);


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
	Window() = default;
	Window(const Window& other) = delete;
	Window(const char* title, const UVec2& size, const eWindowFlags flags) { Create(title, size, flags); }

	void Create(const char* title, const UVec2& size, const eWindowFlags flags);

	void HandleResize();

	void SetTitle(const char* title);

	/**
	 * @brief Returns the instance extensions needed to create a surface for this kind of window.
	 */
	static const char* const* GetRequiredInstanceExtensions(uint32* out_count);

	/**
	 * @brief Creates a Vulkan surface that presents into this window. Panics on failure.
	 */
	VkSurfaceKHR CreateSurface(VkInstance instance);

	void SetRelativeMouseMode(bool enabled);

	/**
	 * @brief True if the window currently has OS input focus (not minimized/backgrounded/alt-tabbed away from)
	 */
	bool IsFocused() const;

	float32 GetAspectRatio() const { return static_cast<float32>(mSize.X) / static_cast<float32>(mSize.Y); }

	FX_FORCE_INLINE const UVec2& GetSize() const { return mSize; }

#ifdef FX_IS_EDITOR
	FX_FORCE_INLINE editor::EditorViewport* GetViewport() { return mpViewport; }
#else
	FX_FORCE_INLINE SDL_Window* GetWindow() { return mWindow; }
#endif

	~Window();

public:
private:
	UVec2 mSize = UVec2::scZero;

#ifdef FX_IS_EDITOR
	editor::EditorViewport* mpViewport = nullptr;
#else
	SDL_Window* mWindow = nullptr;

	FVec2 mSavedMousePos = FVec2::sZero;
#endif
};

} // namespace fx
