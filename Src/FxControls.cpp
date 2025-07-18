#include "FxControls.hpp"
#include "Util/FxKey.hpp"

#include <Renderer/Renderer.hpp>

#include <SDL3/SDL.h>

/** Converts an SDL scancode to its corresponding FxKey ID. */
static inline FxKey ConvertScancodeToFxKey(int32 sdl_scancode)
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
static inline FxKey ConvertMouseButtonToFxKey(int32 mouse_button)
{
    // If the button is not defined in FxKey, return FX_KEY_UNKNOWN.
    if (FxKey::FX_MOUSE_BUTTONS_START + mouse_button > FxKey::FX_MOUSE_BUTTONS_END) {
        return FxKey::FX_KEY_UNKNOWN;
    }

    // If it exists, get the mouse button Key ID.
    return static_cast<FxKey>(FxKey::FX_MOUSE_BUTTONS_START + mouse_button);
}

void FxControlManager::Init()
{
    // FxMemPool::GetGlobalPool().PrintAllocations();

    FxControlManager& inst = GetInstance();

    inst.mKeyMap.InitSize(MaxKeys);
    memset(inst.mKeyMap.Data, 0, inst.mKeyMap.GetSizeInBytes());
}

FxControlManager& FxControlManager::GetInstance()
{
    static FxControlManager ControlManager;

    return ControlManager;
}

void FxControlManager::CaptureMouse()
{
    FxControlManager& inst = GetInstance();

    // Get the position of the mouse on capture
    float32 x, y;
    SDL_GetMouseState(&x, &y);
    inst.mCapturedMousePos.Set(x, y);

    SDL_SetWindowRelativeMouseMode(
        Renderer->GetWindow()->GetWindow(),
        (inst.mMouseCaptured = true)
    );
}

bool FxControlManager::IsMouseLocked()
{
    return GetInstance().mMouseCaptured;
}

void FxControlManager::ReleaseMouse()
{
    FxControlManager& inst = GetInstance();

    FxVec2f *pos = &inst.mCapturedMousePos;
    SDL_Window *window = Renderer->GetWindow()->GetWindow();

    // Warp back to the original position
    SDL_WarpMouseInWindow(window, pos->GetX(), pos->GetY());

    SDL_SetWindowRelativeMouseMode(
        window,
        (inst.mMouseCaptured = false)
    );
}

FxVec2f &FxControlManager::GetMouseDelta()
{
    return GetInstance().mMouseDelta;
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

    FxControl *key = &GetInstance().mKeyMap[key_id];

    return key;
}

///////////////////////////////
// Key State functions
///////////////////////////////

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
    if (key_next_frame == FxControlManager::GetInstance().mThisTick) {
        key->mContinuedPress = true;
    }
}

bool FxControlManager::IsKeyDown(FxKey scancode)
{
    FxControl *key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    CheckKeyForContinuedPress(key);

    return key->IsKeyDown();
}

bool FxControlManager::IsKeyPressed(FxKey scancode)
{
    FxControl *key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    CheckKeyForContinuedPress(key);

    return key->IsKeyPressed();
}

bool FxControlManager::IsKeyUp(FxKey scancode)
{
    FxControl *key = GetInstance().GetKey(scancode);
    if (!key) {
        return false;
    }

    return key->IsKeyUp();
}

////////////////////////////////
// Update functions
////////////////////////////////

#include <SDL3/SDL.h>

void FxControlManager::Update()
{
    FxControlManager& inst = GetInstance();

    inst.mMouseDelta = FxVec2f::Zero;
    inst.mThisTick++;

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                inst.OnQuit();
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

void FxControlManager::UpdateButtonFromEvent(FxKey key_id, SDL_Event *event)
{
    FxControl *button = GetInstance().GetKey(key_id);
    if (!button) {
        return;
    }

    if (event->key.down && !button->IsKeyDown()) {
        button->mTickBit = GetInstance().mThisTick;
        button->mKeyDown = true;
    }
    else if (!event->key.down && button->IsKeyDown()) {
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

void FxControlManager::UpdateFromMouseMoveEvent(SDL_Event *event)
{
    GetInstance().mMouseDelta += { event->motion.xrel, event->motion.yrel };
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
