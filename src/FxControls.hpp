#pragma once

#include <Core/StaticArray.hpp>
#include <Core/Types.hpp>

#include <cstring>

union SDL_Event;

class FxControl;

class FxControlManager {
private:
    friend void CheckKeyForContinuedPress(FxControl *key);

public:
    static const int MaxKeys = 300;

    FxControlManager()
    {
        mKeyMap.InitSize(MaxKeys);
        memset(mKeyMap.Data, 0, mKeyMap.GetSizeInBytes());
    }

    static FxControlManager &GetInstance();

    /**
     * Check if a key is currently pressed.
     */
    static bool IsKeyDown(int32 scancode);

    /**
     * Check if a key is currently released.
     */
    static bool IsKeyUp(int32 scancode);

    /**
     * Check if a key was pressed once.
     *
     * This returns `true` if the key was pressed for the first time
     * during this frame. It will return `false` if the key was already pressed
     * before this frame or if it was released during this frame.
     *
     * *NOTE:* When not calling this function every tick, make sure that
     * `UpdateControlManager()` is called before using this function. This function
     * relies on the current tick of the control manager.
     */
    static bool IsKeyPressed(int32 scancode);

    /**
     * Get a pointer to the control associated with the given keycode.
     * Returns `nullptr` if the keycode does not exist or is out of range.
     */
    static FxControl *GetKey(int32 scancode);

    ////////////////////////////////
    // Update functions
    ////////////////////////////////

    static void UpdateFromKeyboardEvent(SDL_Event *event);
    static void UpdateControlManager();

private:
    StaticArray<FxControl> mKeyMap;

    uint8 mThisTick : 1 = 0;
};

class FxControl
{
private:
    friend void CheckKeyForContinuedPress(FxControl *control);
    friend void FxControlManager::UpdateFromKeyboardEvent(SDL_Event *control);

public:
    enum StateFlag : uint8 {
        NoState = (0),
        ContinuedPress = (1 << 0),
        KeyDown = (1 << 1),
        FrameBit = (1 << 2), /* Flag for if this is the current frame or the previous frame. */
    };

    enum Flag : uint8 {
        NoFlag = (0),
    };

public:
    inline bool IsKeyDown() const { return mKeyDown; }
    inline bool IsKeyUp() const { return !mKeyDown; }

    inline bool IsContinuedPress() const { return mContinuedPress; }

    bool IsKeyPressed() const{ return (IsKeyDown() && !IsContinuedPress()); }

    /**
     * Set the flags of the control. These are defined by `Flag`.
     * For example, to set a control to be a "toggle",
     *
     * ```cpp
     * control->SetFlags(control->GetFlags() | FxControl::Flag::Toggle);
     * ```
     */
    inline void SetFlags(uint8 flags) { mFlags = flags; }
    inline void AddFlag(uint8 flag) { mFlags |= flag; }
    inline void RemoveFlag(uint8 flag) { mFlags &= ~flag; }
    inline bool HasFlag(uint8 flag) const { return (mFlags & flag); }
    inline uint8 GetFlags() const { return mFlags; }

    /**
     * The current state of the control.
     * The state is documented with a set of flags defined by `StateFlag`.
     *
     * These flags are normally accessed using helper functions such as
     * `IsKeyDown`, `IsKeyUp`, and `IsToggled`.
     */
    // uint8 CurrentState = StateFlag::NoState;

private:
    uint8 mFlags = Flag::NoFlag;

    uint8 mContinuedPress : 1 = 0;
    uint8 mKeyDown : 1 = 0;
    uint8 mFrameBit : 1 = 0;
};
