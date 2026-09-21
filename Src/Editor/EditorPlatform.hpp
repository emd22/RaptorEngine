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
 * @brief Retrieve the mouse movement delta
 */
Vec2f ConsumeRelativeMouseDelta();

/**
 * @brief Brings the editor to the front. When starting from the terminal, the application is not normally activated on
 * launch. This is to make that consistent
 */
void ActivateApp();

#endif

} // namespace fx::editor::platform
