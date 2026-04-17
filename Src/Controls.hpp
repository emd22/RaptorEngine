#pragma once

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Util/Key.hpp>
#include <cstring>

union SDL_Event;

namespace fx {

// Forward Declarations
class Control;

class ControlManager
{
private:
    friend void CheckKeyForContinuedPress(Control* key);

public:
    static const uint32 scMaxKeys = static_cast<uint32>(eKey::FX_KEY_MAX);

    ControlManager() = default;

    static void Init();

    static ControlManager& GetInstance();

    static void CaptureMouse();
    static void ReleaseMouse();
    static bool IsMouseLocked();

    static Vec2f& GetMouseDelta();

    /**
     * Check if a key is currently pressed.
     */
    static bool IsKeyDown(eKey scancode);

    /**
     * Check if a key is currently released.
     */
    static bool IsKeyUp(eKey scancode);

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
     * a call to `ControlManager::Update`. As well, if this function
     * is not called within the bounds of *every* `::Update` call, then
     * the bit that determines if the key is on the next frame returns false positives.
     * This *ONLY* pertains to if the key is held down.
     */
    static bool IsKeyPressed(eKey scancode);

    /**
     * Checks if a combination of keys is currently pressed down.
     */
    template <std::same_as<eKey>... KeyV>
    static bool IsComboDown(KeyV... keys)
    {
        return (IsKeyDown(keys) && ...);
    }

    /**
     * Checks if a combination of keys is no longer pressed.
     */
    template <std::same_as<eKey>... KeyV>
    static bool IsComboUp(KeyV... keys)
    {
        return (IsKeyUp(keys) && ...);
    }

    /**
     * Checks if a combination of keys have just been pressed down.
     *
     * This function will only return true if at least one of the keys satisifies `IsKeyPressed()`,
     * and all of the keys are pressed down.
     */
    template <std::same_as<eKey>... KeyV>
    static bool IsComboPressed(KeyV... keys)
    {
        // If all keys are down and at least one key has just been pressed, then return true
        return (IsKeyDown(keys) && ...) && (IsKeyPressed(keys) || ...);
    }

    /**
     * Get a pointer to the control associated with the given keycode.
     * Returns `nullptr` if the keycode does not exist or is out of range.
     */
    static Control* GetKey(eKey scancode);

    static void ResetKey(eKey scancode);

    ////////////////////////////////
    // Update functions
    ////////////////////////////////

    static void Update();

private:
    static void UpdateFromKeyboardEvent(SDL_Event* event);
    static void UpdateFromMouseButtonEvent(SDL_Event* event);
    static void UpdateFromMouseMoveEvent(SDL_Event* event);

    // General, used by UpdateFromKeyboardEvent and UpdateFromMouseButtonEvent
    static void UpdateButtonFromEvent(eKey key_id, bool is_now_down);

public:
    using WindowEventFunc = void (*)();

    WindowEventFunc OnQuit;

private:
    SizedArray<Control> mKeyMap;
    bool mMouseCaptured = false;

    Vec2f mMouseDelta = Vec2f::sZero;
    Vec2f mCapturedMousePos = Vec2f::sZero;

    uint8 mThisTick : 1 = 0;
};

class Control
{
private:
    friend void CheckKeyForContinuedPress(Control* control);
    friend class ControlManager;

public:
public:
    inline bool IsKeyDown() const { return mbKeyDown; }
    inline bool IsKeyUp() const { return !mbKeyDown; }

    inline bool IsContinuedPress() const { return mbContinuedPress; }

    inline void Reset()
    {
        mbContinuedPress = false;
        mbKeyDown = false;
    }

    bool IsKeyPressed() const { return (IsKeyDown() && !IsContinuedPress()); }

private:
    uint8 mbContinuedPress : 1 = 0;
    uint8 mbKeyDown : 1 = 0;
    uint8 mbTickBit : 1 = 0;
};

} // namespace fx
