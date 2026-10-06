#pragma once

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Util/Key.hpp>
#include <raptor_ffi.h>

namespace fx {

class ControlManager
{
public:
	using WindowEventFunc = void (*)();

	static void Init() {}

	static ControlManager& GetInstance();

	static void CaptureMouse();
	static void ReleaseMouse();
	static bool IsMouseLocked() { return rx_controls_mouse_captured() != 0; }

	static Vec2f GetMouseDelta();

	static bool IsKeyDown(eKey scancode) { return rx_controls_is_down(static_cast<uint32>(scancode)) != 0; }
	static bool IsKeyUp(eKey scancode) { return rx_controls_is_up(static_cast<uint32>(scancode)) != 0; }
	static bool IsKeyPressed(eKey scancode) { return rx_controls_is_pressed(static_cast<uint32>(scancode)) != 0; }

	template <std::same_as<eKey>... KeyV>
	static bool IsComboDown(KeyV... keys)
	{
		return (IsKeyDown(keys) && ...);
	}

	template <std::same_as<eKey>... KeyV>
	static bool IsComboUp(KeyV... keys)
	{
		return (IsKeyUp(keys) && ...);
	}

	template <std::same_as<eKey>... KeyV>
	static bool IsComboPressed(KeyV... keys)
	{
		return (IsKeyDown(keys) && ...) && (IsKeyPressed(keys) || ...);
	}

	static char GetAlphaKey() { return static_cast<char>(rx_controls_typed_char()); }

	static void ResetKey(eKey scancode) { rx_controls_reset_key(static_cast<uint32>(scancode)); }

	static void Update();

	static void PostButtonEvent(eKey key_id, bool is_now_down)
	{
		rx_controls_post_button(static_cast<uint32>(key_id), is_now_down);
	}

	static void PostMouseMotion(const Vec2f& delta) { rx_controls_post_mouse_motion(delta.X, delta.Y); }

	static void ReleaseAllKeys() { rx_controls_release_all(); }
	static void ReleaseNonModifierKeys() { rx_controls_release_non_modifiers(); }

public:
	WindowEventFunc OnQuit = nullptr;
};

} // namespace fx
