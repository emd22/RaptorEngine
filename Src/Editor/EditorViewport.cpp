#include "EditorViewport.hpp"

#include "EditorPlatform.hpp"

#include <wx/dcclient.h>
#include <wx/utils.h>

#include <Controls.hpp>
#include <Util/Key.hpp>

namespace fx::editor {

static eKey OffsetKey(eKey base, int32 offset) { return static_cast<eKey>(static_cast<int32>(base) + offset); }

/**
 * @brief Converts a wxWidgets key code to its corresponding Key ID.
 */
static eKey ConvertWxKeyToKey(int32 key_code)
{
	if (key_code >= 'A' && key_code <= 'Z') {
		return OffsetKey(eKey::FX_KEY_A, key_code - 'A');
	}
	if (key_code >= 'a' && key_code <= 'z') {
		return OffsetKey(eKey::FX_KEY_A, key_code - 'a');
	}
	if (key_code >= '1' && key_code <= '9') {
		return OffsetKey(eKey::FX_KEY_1, key_code - '1');
	}
	if (key_code >= WXK_F1 && key_code <= WXK_F12) {
		return OffsetKey(eKey::FX_KEY_F1, key_code - WXK_F1);
	}
	if (key_code >= WXK_NUMPAD1 && key_code <= WXK_NUMPAD9) {
		return OffsetKey(eKey::FX_KEY_NUMPAD_1, key_code - WXK_NUMPAD1);
	}

	switch (key_code) {
	// The shifted characters are in case the platform reports them instead of the key underneath
	case '0':
	case ')':
		return eKey::FX_KEY_0;
	case '!':
		return eKey::FX_KEY_1;
	case '@':
		return eKey::FX_KEY_2;
	case '#':
		return eKey::FX_KEY_3;
	case '$':
		return eKey::FX_KEY_4;
	case '%':
		return eKey::FX_KEY_5;
	case '^':
		return eKey::FX_KEY_6;
	case '&':
		return eKey::FX_KEY_7;
	case '*':
		return eKey::FX_KEY_8;
	case '(':
		return eKey::FX_KEY_9;

	case WXK_RETURN:
		return eKey::FX_KEY_RETURN;
	case WXK_ESCAPE:
		return eKey::FX_KEY_ESCAPE;
	case WXK_BACK:
		return eKey::FX_KEY_BACKSPACE;
	case WXK_TAB:
		return eKey::FX_KEY_TAB;
	case WXK_SPACE:
		return eKey::FX_KEY_SPACE;

	case '-':
	case '_':
		return eKey::FX_KEY_MINUS;
	case '=':
	case '+':
		return eKey::FX_KEY_EQUALS;
	case '[':
	case '{':
		return eKey::FX_KEY_LBRACKET;
	case ']':
	case '}':
		return eKey::FX_KEY_RBRACKET;
	case '\\':
	case '|':
		return eKey::FX_KEY_BACKSLASH;
	case ';':
	case ':':
		return eKey::FX_KEY_SEMICOLON;
	case '\'':
	case '"':
		return eKey::FX_KEY_APOSTROPHE;
	case '`':
	case '~':
		return eKey::FX_KEY_GRAVE;
	case ',':
	case '<':
		return eKey::FX_KEY_COMMA;
	case '.':
	case '>':
		return eKey::FX_KEY_PERIOD;
	case '/':
	case '?':
		return eKey::FX_KEY_SLASH;

	case WXK_CAPITAL:
		return eKey::FX_KEY_CAPSLOCK;

	case WXK_INSERT:
		return eKey::FX_KEY_INSERT;
	case WXK_HOME:
		return eKey::FX_KEY_HOME;
	case WXK_PAGEUP:
		return eKey::FX_KEY_PAGEUP;
	case WXK_DELETE:
		return eKey::FX_KEY_DELETE;
	case WXK_END:
		return eKey::FX_KEY_END;
	case WXK_PAGEDOWN:
		return eKey::FX_KEY_PAGEDOWN;
	case WXK_RIGHT:
		return eKey::FX_KEY_RIGHT;
	case WXK_LEFT:
		return eKey::FX_KEY_LEFT;
	case WXK_DOWN:
		return eKey::FX_KEY_DOWN;
	case WXK_UP:
		return eKey::FX_KEY_UP;

	case WXK_NUMLOCK:
		return eKey::FX_KEY_NUMLOCK;
	case WXK_NUMPAD_DIVIDE:
		return eKey::FX_KEY_NUMPAD_DIVIDE;
	case WXK_NUMPAD_MULTIPLY:
		return eKey::FX_KEY_NUMPAD_MULTIPLY;
	case WXK_NUMPAD_SUBTRACT:
		return eKey::FX_KEY_NUMPAD_MINUS;
	case WXK_NUMPAD_ADD:
		return eKey::FX_KEY_NUMPAD_PLUS;
	case WXK_NUMPAD_ENTER:
		return eKey::FX_KEY_NUMPAD_ENTER;
	case WXK_NUMPAD0:
		return eKey::FX_KEY_NUMPAD_0;
	case WXK_NUMPAD_DECIMAL:
		return eKey::FX_KEY_NUMPAD_PERIOD;

	// wxWidgets doesn't tell left and right modifiers apart
	case WXK_SHIFT:
		return eKey::FX_KEY_LSHIFT;
	case WXK_ALT:
		return eKey::FX_KEY_LALT;
#ifdef __WXOSX__
	// WXK_CONTROL is Command on macOS, which SDL (and the engine) calls meta
	case WXK_CONTROL:
		return eKey::FX_KEY_LMETA;
	case WXK_RAW_CONTROL:
		return eKey::FX_KEY_LCTRL;
#else
	case WXK_CONTROL:
		return eKey::FX_KEY_LCTRL;
	case WXK_WINDOWS_LEFT:
		return eKey::FX_KEY_LMETA;
	case WXK_WINDOWS_RIGHT:
		return eKey::FX_KEY_RMETA;
#endif

	default:
		return eKey::FX_KEY_UNKNOWN;
	}
}

static eKey ConvertWxMouseButtonToKey(int32 button)
{
	switch (button) {
	case wxMOUSE_BTN_LEFT:
		return eKey::FX_MOUSE_LEFT;
	case wxMOUSE_BTN_MIDDLE:
		return eKey::FX_MOUSE_MIDDLE;
	case wxMOUSE_BTN_RIGHT:
		return eKey::FX_MOUSE_RIGHT;
	default:
		return eKey::FX_KEY_UNKNOWN;
	}
}

EditorViewport::EditorViewport(wxWindow* parent, const wxSize& size)
	: wxWindow(parent, wxID_ANY, wxDefaultPosition, size, wxWANTS_CHARS | wxBORDER_NONE)
{
	// Vulkan presents to this window, so wxWidgets shouldn't paint over it
	SetBackgroundStyle(wxBG_STYLE_PAINT);

#ifdef FX_PLATFORM_MACOS
	mpInternalSurface = platform::AttachMetalLayer(GetHandle());
#endif

	Bind(wxEVT_PAINT, [this](wxPaintEvent&) { wxPaintDC dc(this); });

	Bind(wxEVT_KEY_DOWN, &EditorViewport::OnKey, this);
	Bind(wxEVT_KEY_UP, &EditorViewport::OnKey, this);

	Bind(wxEVT_LEFT_DOWN, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_LEFT_UP, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_LEFT_DCLICK, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_MIDDLE_DOWN, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_MIDDLE_UP, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_MIDDLE_DCLICK, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_RIGHT_DOWN, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_RIGHT_UP, &EditorViewport::OnMouseButton, this);
	Bind(wxEVT_RIGHT_DCLICK, &EditorViewport::OnMouseButton, this);

	Bind(wxEVT_MOTION, &EditorViewport::OnMouseMove, this);
	Bind(wxEVT_SIZE, &EditorViewport::OnSize, this);
	Bind(wxEVT_KILL_FOCUS, &EditorViewport::OnKillFocus, this);
}

void EditorViewport::OnKey(wxKeyEvent& event)
{
	const eKey key_id = ConvertWxKeyToKey(event.GetKeyCode());

	if (key_id == eKey::FX_KEY_UNKNOWN) {
		// Let wxWidgets handle the keys the engine doesn't use
		event.Skip();
		return;
	}

	// Not skipped, so no char event follows (and macOS doesn't beep at the key)
	ControlManager::PostButtonEvent(key_id, event.GetEventType() == wxEVT_KEY_DOWN);
}

void EditorViewport::OnMouseButton(wxMouseEvent& event)
{
	const eKey key_id = ConvertWxMouseButtonToKey(event.GetButton());

	if (key_id == eKey::FX_KEY_UNKNOWN) {
		event.Skip();
		return;
	}

	const bool is_down = event.ButtonDown() || event.ButtonDClick();

	if (is_down) {
		SetFocus();
	}

	ControlManager::PostButtonEvent(key_id, is_down);
}

void EditorViewport::OnMouseMove(wxMouseEvent& event)
{
	const wxPoint position = event.GetPosition();

	// Relative mode reads the movement in PollRelativeMouse() instead, since the cursor is held in place
	if (!mbRelativeMouse && mLastMousePos != wxDefaultPosition) {
		const wxPoint delta = position - mLastMousePos;
		ControlManager::PostMouseMotion(Vec2f(static_cast<float32>(delta.x), static_cast<float32>(delta.y)));
	}

	mLastMousePos = position;
}

void EditorViewport::OnSize(wxSizeEvent& event)
{
	mbResizePending = true;
	event.Skip();
}

void EditorViewport::OnKillFocus(wxFocusEvent& event)
{
	// The key up events go to whatever has focus now, so nothing would lift these
	ControlManager::ReleaseAllKeys();
	event.Skip();
}

wxPoint EditorViewport::GetCenter() const
{
	const wxSize size = GetClientSize();
	return wxPoint(size.x / 2, size.y / 2);
}

void EditorViewport::SetRelativeMouseMode(bool enabled)
{
	if (enabled == mbRelativeMouse) {
		return;
	}

	mbRelativeMouse = enabled;

#ifdef FX_PLATFORM_MACOS
	platform::SetRelativeMouseMode(GetHandle(), enabled);
#else
	// TODO: Gotta test this on Windows
	if (enabled) {
		SetCursor(wxCursor(wxCURSOR_BLANK));
		CaptureMouse();
		WarpPointer(GetCenter().x, GetCenter().y);
	}
	else {
		if (HasCapture()) {
			ReleaseMouse();
		}

		SetCursor(wxNullCursor);
	}
#endif

	mLastMousePos = wxDefaultPosition;
}

void EditorViewport::PollRelativeMouse()
{
	if (!mbRelativeMouse) {
		return;
	}

#ifdef FX_PLATFORM_MACOS
	const Vec2f delta = platform::ConsumeRelativeMouseDelta();

	if (delta != Vec2f::sZero) {
		ControlManager::PostMouseMotion(delta);
	}

#else
	// Measure how far the cursor moved from the centre, then put it back
	const wxPoint center = GetCenter();
	const wxPoint delta = ScreenToClient(wxGetMousePosition()) - center;

	if (delta.x != 0 || delta.y != 0) {
		ControlManager::PostMouseMotion(Vec2f(static_cast<float32>(delta.x), static_cast<float32>(delta.y)));
		WarpPointer(center.x, center.y);
	}
#endif
}

bool EditorViewport::ConsumeResize()
{
	const bool resized = mbResizePending;
	mbResizePending = false;

	return resized;
}

} // namespace fx::editor
