#pragma once

#ifdef FX_IS_EDITOR

#include "EditOperation.hpp"
#include "EditorSelection.hpp"
#include "EditorTool.hpp"

#include <wx/evtloop.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Vec2.hpp>
#include <functional>

namespace fx {

class Object;
class String;

namespace script {
class Script;
}

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
	EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size);

	FX_FORCE_INLINE EditorFrame* GetMainFrame() { return mpMainFrame; }
	FX_FORCE_INLINE const EditorFrame* GetMainFrame() const { return mpMainFrame; }

	void SetReloadHandler(eReloadTarget target, std::function<void()> handler);
	void InvokeReloadHandler(eReloadTarget target);

	bool PumpEvents();

	/**
	 * @brief Runs the editor for a frame: its hotkeys, picking objects, and the selected tool. Does nothing while
	 * simulating.
	 */
	void Update(float32 delta_time);

	/// Updates the property panels to show the world and the last selected object
	void RefreshPanels();

	/////////////////////////////////////
	// Tools
	/////////////////////////////////////

	/// Selects a tool, or `eEditorTool::None` to leave the editor and simulate the game
	void SetTool(eEditorTool tool);

	FX_FORCE_INLINE eEditorTool GetCurrentToolType() const { return mCurrentToolType; }
	FX_FORCE_INLINE EditorTool* GetCurrentTool() { return mpCurrentTool; }
	EditorTool* GetTool(const eEditorTool tool_type);

	FX_FORCE_INLINE bool IsSimulationMode() const { return (mCurrentToolType == eEditorTool::None); }

	/// Reloads every script, then picks the tools' functions back up
	void ReloadScripts();

	FX_FORCE_INLINE const EditorToolState& GetToolState() const { return mToolState; }

	/// Takes the snap settings from a tool script
	void SubmitToolState(const EditorToolState& state);

	/// The size of a snap step, in metres
	float32 GetSnapStep() const;

	/////////////////////////////////////
	// Selection
	/////////////////////////////////////

	/**
	 * @brief Selects an object, adding it to the selection if `append_selection` is set or replacing the selection
	 * otherwise. Passing nullptr clears the selection. Returns true if the object is now selected.
	 */
	bool SelectObject(Object* object, bool append_selection);
	void ClearSelection();

	FX_FORCE_INLINE const EditorSelection& GetSelection() const { return mSelection; }

	/// Changes an object's material, and the material it goes back to once it is deselected
	void SetStoredMaterial(Object* object, MaterialID material);

	/////////////////////////////////////
	// Edit operations
	/////////////////////////////////////

	/// Applies an operation and records it so it can be undone
	EditOperationValue PushEditOperation(const EditOperation& op);

	/// Deletes an object as an undoable operation. `group_size` is the number of objects deleted together.
	void DeleteObject(Object* object, int32 group_size);

	void Undo();
	void Redo();

	/// Drops the selection and edit history. Call before the objects in them are destroyed, such as on a reload.
	void ForgetObjects();

	/////////////////////////////////////
	// Commands
	/////////////////////////////////////

	/// Runs `CMD_<name>` from the editor command script. Returns false if there is no such command.
	bool RunCommand(const String& name);

	void Destroy();

	~RaptorEditor() = default;

private:
	void AddTools();
	void AddTool(const eEditorTool tool_type, const char* path, eEditorToolFlags flags);

	void HandleHotkeys();

	/// Selects the object under the crosshair
	void PickObject();

	void BeginDrag();
	/// Ends the drag in progress, letting the tool finish its operation
	void EndDrag();
	/// Ends the drag in progress without letting the tool finish it
	void CancelDrag();

	/// Slows the player down and turns off head bob, so a dragged object can be placed precisely
	void SetMovementDamped(bool damped);

	void HideToolMarkers();

	/// Copies the selection over to the scripts, capturing where each object is now
	void SyncSelection();
	void SyncCurrentTool();

	void CreateObjectAtCrosshair();
	void DeleteSelection();
	void DupeSelection();

	void AdjustSnapLevel(int32 direction);
	Vec3f SnapToGrid(const Vec3f& position) const;

private:
	EditorFrame* mpMainFrame = nullptr;

	StackArray<EditorTool, static_cast<uint32>(eEditorTool::Count)> mTools;

	eEditorTool mCurrentToolType = eEditorTool::None;
	EditorTool* mpCurrentTool = nullptr;

	EditorToolState mToolState {};
	EditorToolSelection mToolSelection {};

	EditorSelection mSelection;
	EditHistory mHistory { mSelection };

	/// Provides the `CMD_*` console commands
	script::Script* mpCommandScript = nullptr;

	bool mbDragging = false;
	bool mbHadHeadbob = false;

	wxGUIEventLoop* mpEventLoop = nullptr;

	std::function<void()> mpReloadHandlers[static_cast<uint32>(eReloadTarget::Count)];
};


} // namespace fx::editor

#endif
