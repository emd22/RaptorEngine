#pragma once

#include <wx/window.h>

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <atomic>

#if defined(FX_PLATFORM_MACOS)
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>
#elif defined(FX_PLATFORM_WINDOWS)
#include <windows.h>
// vulkan_win32.h needs the core Vulkan types and the Win32 types (HINSTANCE, HWND) declared before it
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#else
#error "Unsupported platform for the editor"
#endif

namespace fx::editor {

class EditorViewport : public wxWindow
{
public:
	EditorViewport(wxWindow* parent, const wxSize& size);

#ifdef FX_PLATFORM_MACOS
	const CAMetalLayer* GetInternalSurface() const { return mpInternalSurface; }
#endif

	void SetRelativeMouseMode(bool enabled);

	void PollRelativeMouse();

	Vec2u GetCachedSize() const { return Vec2u(mCachedWidth.load(), mCachedHeight.load()); }

	void UpdateCachedSize();

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

	wxPoint mSavedMousePos = wxDefaultPosition;

	std::atomic<uint32> mCachedWidth = 1;
	std::atomic<uint32> mCachedHeight = 1;

	std::atomic<bool> mbResizePending = false;

	bool mbRelativeMouse = false;
};

} // namespace fx::editor
