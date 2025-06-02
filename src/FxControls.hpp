#pragma once

#include <Core/FxStaticArray.hpp>
#include <Core/Types.hpp>

#include <Math/Vec2.hpp>

#include <Util/FxKey.hpp>

#include <cstring>

// Forward Declarations
union SDL_Event;
class FxControl;

class FxControlManager {
private:
    friend void CheckKeyForContinuedPress(FxControl *key);

public:
    static const int MaxKeys = FxKey::FX_KEY_MAX;

    FxControlManager() = default;

    static void Init();

    static FxControlManager& GetInstance();

    static void CaptureMouse();
    static void ReleaseMouse();
    static bool IsMouseLocked();

    static Vec2f &GetMouseDelta();

    /**
     * Check if a key is currently pressed.
     */
    static bool IsKeyDown(FxKey scancode);

    /**
     * Check if a key is currently released.
     */
    static bool IsKeyUp(FxKey scancode);

    /**
     * Check if a key was pressed once.
     *
     * This returns `true` if the key was pressed for the first time
     * during this frame. It will return `false` if the key was already pressed
     * before this frame or if it was released during this frame.
     *
     * - Any occurances of this function called during the frame will return true.
     *
     * *NOTE:* The current "scope" for currently pressed keys is reset or begun on
     * a call to `FxControlManager::Update`. As well, if this function
     * is not called within the bounds of *every* `::Update` call, then
     * the bit that determines if the key is on the next frame returns false positives.
     * This *ONLY* pertains to if the key is held down.
     */
    static bool IsKeyPressed(FxKey scancode);

    /**
     * Checks if a combination of keys is currently pressed down.
     */
    template <std::same_as<FxKey> ...KeyV>
    static bool IsComboDown(KeyV ...keys)
    {
        return (IsKeyDown(keys) && ...);
    }

    /**
     * Checks if a combination of keys is no longer pressed.
     */
    template <std::same_as<FxKey> ...KeyV>
    static bool IsComboUp(KeyV ...keys)
    {
        return (IsKeyUp(keys) && ...);
    }

    /**
     * Checks if a combination of keys have just been pressed down.
     *
     * This function will only return true if at least one of the keys satisifies `IsKeyPressed()`,
     * and all of the keys are pressed down.
     */
    template <std::same_as<FxKey> ...KeyV>
    static bool IsComboPressed(KeyV ...keys)
    {
        // If all keys are down and at least one key has just been pressed, then return true
        return (IsKeyDown(keys) && ...) && (IsKeyPressed(keys) || ...);
    }

    /**
     * Get a pointer to the control associated with the given keycode.
     * Returns `nullptr` if the keycode does not exist or is out of range.
     */
    static FxControl *GetKey(FxKey scancode);

    ////////////////////////////////
    // Update functions
    ////////////////////////////////

    static void Update();

private:
    static void UpdateFromKeyboardEvent(SDL_Event *event);
    static void UpdateFromMouseButtonEvent(SDL_Event *event);
    static void UpdateFromMouseMoveEvent(SDL_Event *event);

    // General, used by UpdateFromKeyboardEvent and UpdateFromMouseButtonEvent
    static void UpdateButtonFromEvent(FxKey key_id, SDL_Event *event);

public:
    using WindowEventFunc = void (*)();

    WindowEventFunc OnQuit;

private:
    FxStaticArray<FxControl> mKeyMap;
    bool mMouseCaptured = false;

    Vec2f mMouseDelta = Vec2f::Zero;
    Vec2f mCapturedMousePos = Vec2f::Zero;

    uint8 mThisTick : 1 = 0;
};

class FxControl
{
private:
    friend void CheckKeyForContinuedPress(FxControl *control);
    friend class FxControlManager;

public:
    // enum StateFlag : uint8 {
    //     NoState = (0),
    //     ContinuedPress = (1 << 0),
    //     KeyDown = (1 << 1),
    //     FrameBit = (1 << 2), /* Flag for if this is the current frame or the previous frame. */
    // };

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
    uint8 mTickBit : 1 = 0;
};
