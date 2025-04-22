#include "FxControls.hpp"

static FxControlManager ControlManager;

FxControlManager &FxControlManager::GetInstance()
{
    return ControlManager;
}

static inline bool IsKeyInRange(int32 scancode)
{
    bool in_range = (scancode >= 0 && scancode < FxControlManager::MaxKeys);

    if (!in_range) {
        Log::Warning("Invalid Control(scancode) index! (0 <= %d < %d)", scancode, FxControlManager::MaxKeys);
    }

    return in_range;
}



FxControl *FxControlManager::GetKey(int32 scancode)
{
    if (!IsKeyInRange(scancode)) {
        return nullptr;
    }

    FxControl *key = &ControlManager.mKeyMap[scancode];

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
    // When a key is pressed, we set the `FrameBit` in `mCurrentState` (inside the FxControl)
    // to the current value of `mThisTick`. When a function is called that checks  the
    // key is pressed, such as`IsKeyDown`, `IsKeyPressed`, etc., then we call this function to check
    // if we are on the same frame that the event occurred. If not, then we know that this
    // is a continued keypress.

    // This is already a continued press, ignore.
    if (!key->IsKeyDown() || key->mContinuedPress) {
        return;
    }

    // Get the value of the "off" frame.
    const bool key_next_frame = !key->mFrameBit;

    // If we are currently on the "off" frame, then we know this is continued.
    if (key_next_frame == ControlManager.mThisTick) {
        key->mContinuedPress = true;
    }
}

bool FxControlManager::IsKeyDown(int32 scancode)
{
    GET_SCANCODE_IN_BOUNDS(key);
    CheckKeyForContinuedPress(key);

    return key->IsKeyDown();
}

bool FxControlManager::IsKeyPressed(int32 scancode)
{
    GET_SCANCODE_IN_BOUNDS(key);
    CheckKeyForContinuedPress(key);

    return key->IsKeyPressed();
}

bool FxControlManager::IsKeyUp(int32 scancode)
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

void FxControlManager::UpdateFromKeyboardEvent(SDL_Event *event)
{
    FxControl *key = ControlManager.GetKey(event->key.scancode);
    if (!key) {
        return;
    }

    if (event->key.down && !key->IsKeyDown()) {
        // key->CurrentState |= (FxControl::StateFlag::KeyDown);
        // key->CurrentState = (key->CurrentState & ~((uint8)1 << 2)) | (((uint8)ControlManager.mThisTick) << 2);
        key->mFrameBit = ControlManager.mThisTick;
        key->mKeyDown = true;
    }
    else if (!event->key.down && key->IsKeyDown()) {
        // key->CurrentState &= ~(FxControl::StateFlag::KeyDown | FxControl::StateFlag::ContinuedPress);
        key->mKeyDown = false;
        key->mContinuedPress = false;
    }
}
