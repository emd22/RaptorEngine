#pragma once

#include "EditorTool.hpp"

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <functional>

namespace fx {

class Object;

namespace editor {

enum class eEditorTool : uint32;

};

} // namespace fx

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


class RaptorEditor
{
public:
	RaptorEditor() = default;

	bool InitGUI(int argc, char** argv);
	EditorFrame* CreateMainWindow(const char* title, const Vec2u& viewport_size);

	void Destroy();

	~RaptorEditor() = default;

private:
public:
private:
};


/**
 * @brief Creates the editor frame
 */

EditorFrame* GetMainFrame();

/**
 * @brief Dispatches the pending wxWidgets events for this frame. Key and mouse input on the viewport reaches the
 * ControlManager from in here.
 *
 * @returns false once the editor window has been closed.
 */
bool PumpEvents();

void UpdatePropertiesPanelForObject(Object* object);
void UpdateWorldPropertiesPanel();

bool IsSimulationMode();

/**
 * @brief Returns the tool selected in the editor's tool bar
 */
eEditorTool GetEditorTool();

editor::EditorTool* GetEditorTool2(eEditorTool tool_type);

editor::EditorToolState* GetEditorToolState();
editor::EditorToolSelection* GetEditorToolSelection();

void ReloadAllTools();

void SubmitToolConfig(const editor::EditorToolState* config);

/**
 * @brief Sets up a handler used by the File > Reload X options
 */
void SetReloadHandler(eReloadTarget target, std::function<void()> handler);

/// Called by EditorFrame's File > Reload menu. Does nothing if no handler is registered for `target` yet.
void InvokeReloadHandler(eReloadTarget target);


} // namespace fx::editor
