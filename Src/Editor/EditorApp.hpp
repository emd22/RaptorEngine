#pragma once

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <functional>

namespace fx {
class Object;
}

namespace fx::editor {

class EditorFrame;

/// The File > Reload menu items
enum class eReloadTarget : uint32
{
	World,
	Prototype,
	Scripts,

	Count,
};

/**
 * @brief Initialze wxWidgets gubbins
 */
bool Init(int argc, char** argv);

/**
 * @brief Creates the editor frame
 */
EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size);

EditorFrame* GetMainFrame();

/**
 * @brief Dispatches the pending wxWidgets events for this frame. Key and mouse input on the viewport reaches the
 * ControlManager from in here.
 *
 * @returns false once the editor window has been closed.
 */
bool PumpEvents();

void UpdatePropertiesPanelForObject(const Object* object);
void UpdateWorldPropertiesPanel();

bool IsSimulationMode();

/**
 * @brief Sets up a handler used by the File > Reload X options
 */
void SetReloadHandler(eReloadTarget target, std::function<void()> handler);

/// Called by EditorFrame's File > Reload menu. Does nothing if no handler is registered for `target` yet.
void InvokeReloadHandler(eReloadTarget target);

/**
 * @brief Destroys the editor frame and shuts wxWidgets down. Call after the renderer has destroyed its surface.
 */
void Shutdown();

} // namespace fx::editor
