#include "Controls.hpp"

#include "Util/Key.hpp"

#include <SDL3/SDL.h>

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

/** Converts an SDL scancode to its corresponding Key ID. */
static inline eKey ConvertScancodeToKey(int32 sdl_scancode)
{
    if (sdl_scancode <= static_cast<uint32>(eKey::FX_KEYBOARD_STANDARD_END)) {
        // Key is the direct scancode
        return static_cast<eKey>(sdl_scancode);
    }
    // Check for ctrl, alt, meta keys
    else if (sdl_scancode >= static_cast<uint32>(eKey::FX_KEYBOARD_STANDARD_END) &&
             sdl_scancode <= static_cast<uint32>(eKey::FX_KEYBOARD_MODIFIERS_END) + FX_KEYBOARD_MODIFIERS_OFFSET) {
        return static_cast<eKey>(sdl_scancode - FX_KEYBOARD_MODIFIERS_OFFSET);
    }

    return eKey::FX_KEY_UNKNOWN;
}

/** Converts an SDL mouse button index to its corresponding Key ID. */
static inline eKey ConvertMouseButtonToKey(int32 mouse_button)
{
    constexpr uint32 mouse_buttons_start = static_cast<uint32>(eKey::FX_MOUSE_BUTTONS_START);
    constexpr uint32 mouse_buttons_end = static_cast<uint32>(eKey::FX_MOUSE_BUTTONS_END);

    // If the button is not defined in Key, return FX_KEY_UNKNOWN.
    if (mouse_buttons_start + mouse_button > mouse_buttons_end) {
        return eKey::FX_KEY_UNKNOWN;
    }

    // If it exists, get the mouse button Key ID.
    return static_cast<eKey>(mouse_buttons_start + mouse_button);
}

void ControlManager::Init()
{
    // MemPool::GetGlobalPool().PrintAllocations();

    ControlManager& inst = GetInstance();

    inst.mKeyMap.InitSize(scMaxKeys);
    memset(inst.mKeyMap.pData, 0, inst.mKeyMap.GetSizeInBytes());
}

ControlManager& ControlManager::GetInstance()
{
    static ControlManager ControlManager;

    return ControlManager;
}

void ControlManager::CaptureMouse()
{
    ControlManager& inst = GetInstance();

    // Get the position of the mouse on capture
    float32 x, y;
    SDL_GetMouseState(&x, &y);
    inst.mCapturedMousePos.Set(x, y);

    SDL_SetWindowRelativeMouseMode(renderer::gRenderer->GetWindow()->GetWindow(), (inst.mMouseCaptured = true));
}

bool ControlManager::IsMouseLocked() { return GetInstance().mMouseCaptured; }

void ControlManager::ReleaseMouse()
{
    ControlManager& inst = GetInstance();

    Vec2f* pos = &inst.mCapturedMousePos;
    SDL_Window* window = renderer::gRenderer->GetWindow()->GetWindow();

    // Warp back to the original position
    SDL_WarpMouseInWindow(window, pos->GetX(), pos->GetY());

    SDL_SetWindowRelativeMouseMode(window, (inst.mMouseCaptured = false));
}

Vec2f& ControlManager::GetMouseDelta() { return GetInstance().mMouseDelta; }

static inline bool IsKeyInRange(eKey key_id)
{
    bool in_range = (static_cast<int32>(key_id) >= 0 && static_cast<int32>(key_id) < ControlManager::scMaxKeys);

    if (!in_range) {
        int max_keys = ControlManager::scMaxKeys;
        LogWarning("Invalid Control Key ID! (0 <= {:d} < {:d})", static_cast<int>(key_id), max_keys);
    }

    return in_range;
}


Control* ControlManager::GetKey(eKey key_id)
{
    if (!IsKeyInRange(key_id)) {
        return nullptr;
    }

    Control* key = &GetInstance().mKeyMap[static_cast<uint32>(key_id)];

    return key;
}

///////////////////////////////
// Key State functions
///////////////////////////////

inline void CheckKeyForContinuedPress(Control* key)
{
    // When a key is pressed, we set the `mTickBit` inside the Control
    // to the current value of `mThisTick`. When a function is called that checks  the
    // key is pressed, such as`IsKeyDown`, `IsKeyPressed`, etc., then we call this function to check
    // if we are on the same frame that the event occurred. If not, then we know that this
    // is a continued keypress.

    // This is already a continued press, ignore.
    if (!key->IsKeyDown() || key->mbContinuedPress) {
        return;
    }

    // Get the value of the "off" frame.
    const bool key_next_frame = !key->mbTickBit;

    // If we are currently on the "off" frame, then we know this is continued.
    if (key_next_frame == ControlManager::GetInstance().mThisTick) {
        key->mbContinuedPress = true;
    }
}

bool ControlManager::IsKeyDown(eKey scancode)
{
    Control* key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    CheckKeyForContinuedPress(key);

    return key->IsKeyDown();
}

bool ControlManager::IsKeyPressed(eKey scancode)
{
    Control* key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    CheckKeyForContinuedPress(key);

    return key->IsKeyPressed();
}

bool ControlManager::IsKeyUp(eKey scancode)
{
    Control* key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    return key->IsKeyUp();
}

void ControlManager::ResetKey(eKey scancode) { GetInstance().GetKey(scancode)->Reset(); }

////////////////////////////////
// Update functions
////////////////////////////////

#include <SDL3/SDL.h>

void ControlManager::Update()
{
    ControlManager& inst = GetInstance();

    inst.mMouseDelta = Vec2f::sZero;
    inst.mThisTick++;

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            inst.OnQuit();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            renderer::gRenderer->RebuildToResizedWindow();
            break;

        // Keyboard events
        case SDL_EVENT_KEY_DOWN: // fallthrough
        case SDL_EVENT_KEY_UP:
            inst.UpdateFromKeyboardEvent(&event);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: // fallthrough
        case SDL_EVENT_MOUSE_BUTTON_UP:
            inst.UpdateFromMouseButtonEvent(&event);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            inst.UpdateFromMouseMoveEvent(&event);
            break;

        default:;
        }
    }
}

void ControlManager::UpdateButtonFromEvent(eKey key_id, bool is_now_down)
{
    Control* button = GetInstance().GetKey(key_id);
    if (!button) {
        return;
    }

    if (is_now_down && !button->IsKeyDown()) {
        button->mbTickBit = GetInstance().mThisTick;
        button->mbKeyDown = true;
    }
    else if (!is_now_down && button->IsKeyDown()) {
        button->mbKeyDown = false;
        button->mbContinuedPress = false;
    }
}

void ControlManager::UpdateFromKeyboardEvent(SDL_Event* event)
{
    const eKey key_id = ConvertScancodeToKey(event->key.scancode);

    if (key_id == eKey::FX_KEY_UNKNOWN) {
        LogWarning("Unknown scancode {:d}!", static_cast<int>(event->key.scancode));
        return;
    }

    UpdateButtonFromEvent(key_id, event->key.down);
}

void ControlManager::UpdateFromMouseMoveEvent(SDL_Event* event)
{
    GetInstance().mMouseDelta += Vec2f(event->motion.xrel, event->motion.yrel);
}

void ControlManager::UpdateFromMouseButtonEvent(SDL_Event* event)
{
    const eKey key_id = ConvertMouseButtonToKey(event->button.button);
    if (key_id == eKey::FX_KEY_UNKNOWN) {
        LogWarning("Unknown mouse button {:d}!", static_cast<int>(event->button.button));
        return;
    }

    UpdateButtonFromEvent(key_id, event->button.down);
}

} // namespace fx
