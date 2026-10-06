#include "Controls.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

#ifdef FX_IS_EDITOR
#include <Editor/RaptorEditor.hpp>
#endif

namespace fx {

ControlManager& ControlManager::GetInstance()
{
	static ControlManager instance;

	return instance;
}

Vec2f ControlManager::GetMouseDelta()
{
	float32 delta[2];
	rx_controls_mouse_delta(delta);

	return Vec2f(delta[0], delta[1]);
}

void ControlManager::CaptureMouse()
{
	Ref<Window> window = renderer::gGraphics->GetWindow();

	const Vec2f position = window->GetMousePosition();
	rx_controls_set_mouse_captured(true, position.X, position.Y);

	window->SetRelativeMouseMode(true);
}

void ControlManager::ReleaseMouse()
{
	if (renderer::gGraphics == nullptr || !renderer::gGraphics->GetWindow()) {
		rx_controls_set_mouse_captured(false, 0.0f, 0.0f);
		return;
	}

	Ref<Window> window = renderer::gGraphics->GetWindow();

	float32 captured[2];
	rx_controls_captured_mouse_position(captured);

	window->WarpMouse(Vec2f(captured[0], captured[1]));
	window->SetRelativeMouseMode(false);

	rx_controls_set_mouse_captured(false, 0.0f, 0.0f);
}

void ControlManager::Update()
{
	rx_controls_begin_frame();

#ifdef FX_IS_EDITOR
	if (!gEditor->PumpEvents()) {
		if (GetInstance().OnQuit != nullptr) {
			GetInstance().OnQuit();
		}
	}
#else
	const uint32 events = rx_controls_pump_sdl();

	if ((events & RX_WINDOW_EVENT_QUIT) != 0 && GetInstance().OnQuit != nullptr) {
		GetInstance().OnQuit();
	}

	if ((events & RX_WINDOW_EVENT_RESIZED) != 0) {
		renderer::gGraphics->RebuildToResizedWindow();
	}
#endif
}

} // namespace fx
