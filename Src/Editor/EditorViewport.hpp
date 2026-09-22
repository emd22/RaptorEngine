#pragma once

#include <wx/window.h>

#include <Core/Defines.hpp>
#include <Math/Vec2.hpp>

#if defined(FX_PLATFORM_MACOS)
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>
#elif defined(FX_PLATFORM_WINDOWS)
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#else
#error "Unsupported platform for the editor"
#endif

namespace fx::editor {

/**
 * @brief The panel the engine renders into
 */
class EditorViewport : public wxWindow
{
public:
	EditorViewport(wxWindow* parent, const wxSize& size);

#ifdef FX_PLATFORM_MACOS
	const CAMetalLayer* GetInternalSurface() const { return mpInternalSurface; }
#endif

	void SetRelativeMouseMode(bool enabled);
	void PollRelativeMouse();

	/**
	 * @brief Returns true once if the viewport has changed size.
	 */
	bool ConsumeResize();

private:
	void OnKey(wxKeyEvent& event);
	void OnMouseButton(wxMouseEvent& event);
	void OnMouseMove(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	wxPoint GetCenter() const;

private:
#ifdef FX_PLATFORM_MACOS
	/// The drawable surface for the engie to render to
	const CAMetalLayer* mpInternalSurface = nullptr;
#endif

	wxPoint mLastMousePos = wxDefaultPosition;

	bool mbRelativeMouse = false;
	bool mbResizePending = false;
};

} // namespace fx::editor
