#include "FxControls.hpp"
#include "Util/FxKey.hpp"

#include <Renderer/Renderer.hpp>

#include <SDL3/SDL.h>

static FxControlManager ControlManager;

/** Converts an SDL scancode to its corresponding FxKey ID. */
inline FxKey ConvertScancodeToFxKey(int32 sdl_scancode)
{
    if (sdl_scancode <= FX_KEYBOARD_STANDARD_END) {
        // FxKey is the direct scancode
        return static_cast<FxKey>(sdl_scancode);
    }
    // Check for ctrl, alt, meta keys
    else if (sdl_scancode >= FX_KEYBOARD_STANDARD_END &&
        sdl_scancode <= FX_KEYBOARD_SPECIALS_END + FX_KEYBOARD_SPECIALS_OFFSET)
    {
        return static_cast<FxKey>(sdl_scancode - FX_KEYBOARD_SPECIALS_OFFSET);
    }

    return FxKey::FX_KEY_UNKNOWN;
}

/** Converts an SDL mouse button index to its corresponding FxKey ID. */
inline FxKey ConvertMouseButtonToFxKey(int32 mouse_button)
{
    // If the button is not defined in FxKey, return FX_KEY_UNKNOWN.
    if (FxKey::FX_MOUSE_BUTTONS_START + mouse_button > FxKey::FX_MOUSE_BUTTONS_END) {
        return FxKey::FX_KEY_UNKNOWN;
    }

    // If it exists, get the mouse button Key ID.
    return static_cast<FxKey>(FxKey::FX_MOUSE_BUTTONS_START + mouse_button);
}

FxControlManager &FxControlManager::GetInstance()
{
    return ControlManager;
}

void FxControlManager::CaptureMouse()
{
    SDL_SetWindowRelativeMouseMode(
        Renderer->GetWindow()->GetWindow(),
        (ControlManager.mMouseCaptured = true)
    );
}

void FxControlManager::ReleaseMouse()
{
    SDL_SetWindowRelativeMouseMode(
        Renderer->GetWindow()->GetWindow(),
        (ControlManager.mMouseCaptured = false)
    );
}

static inline bool IsKeyInRange(FxKey key_id)
{
    bool in_range = (key_id >= 0 && key_id < FxControlManager::MaxKeys);

    if (!in_range) {
        Log::Warning("Invalid Control Key ID! (0 <= %d < %d)", key_id, FxControlManager::MaxKeys);
    }

    return in_range;
}



FxControl *FxControlManager::GetKey(FxKey key_id)
{
    if (!IsKeyInRange(key_id)) {
        return nullptr;
    }

    FxControl *key = &ControlManager.mKeyMap[key_id];

    return key;
}

///////////////////////////////
// Key State functions
///////////////////////////////

#define GET_SCANCODE_IN_BOUNDS(keyname_) \
    FxControl *keyname_ = ControlManager.GetKey(scancode); \
    if (!keyname_) { \
        return false; \
    }


inline void CheckKeyForContinuedPress(FxControl *key)
{
    // When a key is pressed, we set the `mTickBit` inside the FxControl
    // to the current value of `mThisTick`. When a function is called that checks  the
    // key is pressed, such as`IsKeyDown`, `IsKeyPressed`, etc., then we call this function to check
    // if we are on the same frame that the event occurred. If not, then we know that this
    // is a continued keypress.

    // This is already a continued press, ignore.
    if (!key->IsKeyDown() || key->mContinuedPress) {
        return;
    }

    // Get the value of the "off" frame.
    const bool key_next_frame = !key->mTickBit;

    // If we are currently on the "off" frame, then we know this is continued.
    if (key_next_frame == ControlManager.mThisTick) {
        key->mContinuedPress = true;
    }
}

bool FxControlManager::IsKeyDown(FxKey scancode)
{
    GET_SCANCODE_IN_BOUNDS(key);
    CheckKeyForContinuedPress(key);

    return key->IsKeyDown();
}

bool FxControlManager::IsKeyPressed(FxKey scancode)
{
    GET_SCANCODE_IN_BOUNDS(key);
    CheckKeyForContinuedPress(key);

    return key->IsKeyPressed();
}

bool FxControlManager::IsKeyUp(FxKey scancode)
{
    GET_SCANCODE_IN_BOUNDS(key);
    return key->IsKeyUp();
}

////////////////////////////////
// Update functions
////////////////////////////////

#include <SDL3/SDL.h>

void FxControlManager::UpdateControlManager()
{
    ControlManager.mThisTick++;
}

void FxControlManager::UpdateButtonFromEvent(FxKey key_id, SDL_Event *event)
{
    FxControl *button = ControlManager.GetKey(key_id);
    if (!button) {
        return;
    }

    if (event->key.down && !button->IsKeyDown()) {
        // key->CurrentState |= (FxControl::StateFlag::KeyDown);
        // key->CurrentState = (key->CurrentState & ~((uint8)1 << 2)) | (((uint8)ControlManager.mThisTick) << 2);
        button->mTickBit = ControlManager.mThisTick;
        button->mKeyDown = true;
    }
    else if (!event->key.down && button->IsKeyDown()) {
        // key->CurrentState &= ~(FxControl::StateFlag::KeyDown | FxControl::StateFlag::ContinuedPress);
        button->mKeyDown = false;
        button->mContinuedPress = false;
    }
}

void FxControlManager::UpdateFromKeyboardEvent(SDL_Event *event)
{
    const FxKey key_id = ConvertScancodeToFxKey(event->key.scancode);
    if (key_id == FxKey::FX_KEY_UNKNOWN) {
        Log::Warning("Unknown scancode %d. Could not convert to Key ID\n", event->key.scancode);
        return;
    }

    UpdateButtonFromEvent(key_id, event);
}

void FxControlManager::UpdateFromMouseButtonEvent(SDL_Event *event)
{
    const FxKey key_id = ConvertMouseButtonToFxKey(event->button.button);
    if (key_id == FxKey::FX_KEY_UNKNOWN) {
        Log::Warning("Unknown mouse button %d. Could not convert to Key ID\n", event->button.button);
        return;
    }

    UpdateButtonFromEvent(key_id, event);
}
