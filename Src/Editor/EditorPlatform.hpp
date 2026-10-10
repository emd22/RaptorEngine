#pragma once

#include <Math/Vec2.hpp>

/// Native helpers for things that aren't covered by wxWidgets
namespace fx::editor::platform {

#ifdef FX_PLATFORM_MACOS

/**
 * @brief Get the Metal layer so we can create a Vulkan surface on there
 */
void* AttachMetalLayer(void* ns_view);

/**
 * @brief If enabled, hide the cursor and only take raw mouse movement (movement delta)
 */
void SetRelativeMouseMode(void* ns_view, bool enabled);

/**
 * @brief Retrieve the mouse movement since the last call. Safe to call from any thread
 */
Vec2f ConsumeRelativeMouseDelta();

/**
 * @brief Brings the editor to the front. When starting from the terminal, the application is not normally activated on
 * launch. This is to make that consistent
 */
void ActivateApp();

/**
 * @brief Starts listening for raw mouse movement on a background queue. Mice can take a moment to show up, so this is
 * called at startup rather than when relative mode is first entered
 */
void InitRawMouse();

void DisableWindowTabbing();
bool IsAppActive();

/**
 * @brief True while the event being handled is a trackpad or Magic Mouse scroll with pixel deltas
 */
bool IsPreciseScrollEvent();

#else

inline bool IsPreciseScrollEvent() { return false; }

#endif

} // namespace fx::editor::platform
