#pragma once

#ifdef FX_IS_EDITOR

#include "BoundsEditor.hpp"
#include "EditOperation.hpp"
#include "EditorPanelState.hpp"
#include "EditorSelection.hpp"
#include "EditorTool.hpp"
#include "GrabEditor.hpp"
#include "LightEditor.hpp"
#include "SpawnEditor.hpp"

#include <wx/evtloop.h>

// wxWidgets pulls in windows.h on Windows, which #defines LoadImage to LoadImageW and breaks AssetManager::LoadImage
#ifdef LoadImage
#undef LoadImage
#endif

#include <Brush.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Vec2.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace fx {

class Object;
class String;

namespace script {
class Script;
}

} // namespace fx

namespace fx::editor {

class EditorFrame;

/// The World menu items
enum class eReloadTarget : uint32
{
	World,
	Prototype,
	Scripts,
	Clean,

	Count,
};


class RaptorEditor
{
public:
	RaptorEditor() = default;


	bool InitGUI(int argc, char** argv);

	void RunUILoop();

	void Destroy();


	void InitTools();

	EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size);

	void BeginGameLoop();

	void EndGameLoop();

	void ProcessGUIOperations();

	void SyncPanels();

	void NotifyGameFinished();

	void ServiceViewportResize();

	FX_FORCE_INLINE EditorFrame* GetMainFrame() { return mpMainFrame; }
	FX_FORCE_INLINE const EditorFrame* GetMainFrame() const { return mpMainFrame; }

	bool IsCloseRequested() const;

	FX_FORCE_INLINE bool IsAppActive() const { return mbAppActive; }

	void SetReloadHandler(eReloadTarget target, std::function<void()> handler);
	void InvokeReloadHandler(eReloadTarget target);

	/**
	 * @brief Runs the editor for a frame: its hotkeys, picking objects, and the selected tool. Does nothing while
	 * simulating.
	 */
	void Update(float32 delta_time);

	/////////////////////////////////////
	// Tools
	/////////////////////////////////////

	/// Selects a tool, or `eEditorTool::None` to leave the editor and simulate the game
	void SetTool(eEditorTool tool);

	FX_FORCE_INLINE eEditorTool GetCurrentToolType() const { return mCurrentToolType; }
	FX_FORCE_INLINE EditorTool* GetCurrentTool() { return mpCurrentTool; }
	EditorTool* GetTool(const eEditorTool tool_type);

	FX_FORCE_INLINE bool IsSimulationMode() const { return (mCurrentToolType == eEditorTool::None); }

	/////////////////////////////////////
	// Modes
	/////////////////////////////////////

	void SetMode(eEditorMode mode);
	void SetDataFilter(eDataFilter filter);

	FX_FORCE_INLINE eEditorMode GetMode() const { return mMode; }
	FX_FORCE_INLINE eDataFilter GetDataFilter() const { return mDataFilter; }
	FX_FORCE_INLINE bool IsDataMode() const { return (mMode == eEditorMode::Data); }

	bool IsToolAvailable(eEditorTool tool) const;

	/////////////////////////////////////
	// Views
	/////////////////////////////////////

	void SetView(eEditorView view);

	FX_FORCE_INLINE eEditorView GetView() const { return mView; }
	FX_FORCE_INLINE bool IsTopViewActive() const { return mView != eEditorView::Perspective; }

	FX_FORCE_INLINE void NoteTopViewCommand() { ++mTopViewCommandSerial; }

	void TopViewSelect(const std::vector<uint32>& object_ids, eTopViewSelectMode mode);
	void TopViewMove(const Vec3f offset);
	void TopViewRotate(const Vec3f axis, float32 angle, const Vec3f pivot);
	void TopViewMoveFace(const Vec3f world_normal, float32 distance);
	void TopViewMoveVertices(uint32 axis_u, uint32 axis_v, const std::vector<TopViewVertexHandle>& handles,
							 const Vec3f offset);
	void TopViewCreate(const Vec3f min, const Vec3f max);
	void TopViewDelete();
	void TopViewDuplicate();
	void TopViewToggleSnap();
	void TopViewAdjustSnap(int32 direction);

	Object* CreateWorldBox(const Vec3f min, const Vec3f max);

	FX_FORCE_INLINE bool IsSpawnSelected() const { return mbSpawnSelected; }

	/// Vis mode selects everything but data brushes, Data mode only the data brushes the filter lets through
	bool IsObjectSelectable(const Object* object) const;

	/// Reloads every script, then picks the tools' functions back up
	void ReloadScripts();

	FX_FORCE_INLINE const EditorToolState& GetToolState() const { return mToolState; }

	/// Takes the snap settings from a tool script
	void SubmitToolState(const EditorToolState& state);

	/// The size of a snap step, in metres
	float32 GetSnapStep() const;

	/// Rounds a position (or a distance) to the snap step, if snapping is on
	Vec3f SnapToGrid(const Vec3f position) const;

	float32 GetAngleSnapStep() const;

	/////////////////////////////////////
	// Selection
	/////////////////////////////////////

	/**
	 * @brief Selects an object, adding it to the selection if `append_selection` is set or replacing the selection
	 * otherwise. Passing nullptr clears the selection. Returns true if the object is now selected.
	 */
	bool SelectObject(Object* object, bool append_selection);
	void ClearSelection();

	void DeselectModels();

	bool IsSpawnPointShown() const;
	void SelectSpawn();
	void DeselectSpawn();

	FX_FORCE_INLINE const EditorSelection& GetSelection() const { return mSelection; }

	FX_FORCE_INLINE LightEditor& GetLightEditor() { return mLightEditor; }

	/// Changes an object's material, and the material it goes back to once it is deselected
	void SetStoredMaterial(Object* object, MaterialID material);

	/// The material new brushes are made with: the last one chosen for an object, or the default one
	MaterialID GetNewBrushMaterial() const;

	Object* CreateReflectionProbeAtPlayer();

	/// Creates a data brush of the kind the filter is set to, filling a world space box
	Object* CreateDataBrush(const Brush::PlaneList& planes, const Vec3f position);
	uint32 SetSelectionReflectionProbe(bool enabled);

	uint32 SetSelectionObjectBit(bool is_tag, uint32 bit, bool enabled);

	uint32 SetSelectionScript(const String& path);

	uint32 SetSelectionEnterDirection(const Vec3f local_direction);

	/////////////////////////////////////
	// Edit operations
	/////////////////////////////////////

	/// Applies an operation and records it so it can be undone
	EditOperation& PushEditOperation(std::unique_ptr<EditOperation> op);

	template <typename TOp, typename... TArgs>
	TOp& EmplaceEditOperation(TArgs&&... args)
	{
		std::unique_ptr<TOp> op = std::make_unique<TOp>(std::forward<TArgs>(args)...);
		TOp& pushed = *op;

		PushEditOperation(std::move(op));

		return pushed;
	}

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

	~RaptorEditor() = default;

private:
	enum class eGamePhase : uint8
	{
		Starting,
		Running,
		Stopped,
	};

	void DispatchEvents(unsigned long wait_ms);

	void PumpUI(unsigned long wait_ms);

	void LockdownGUI();

	EditorPanelState BuildPanelState();

	TopViewState BuildTopViewState();

	Object* FindTopViewObject(uint32 object_id);
	uint32 CollectTopViewTargets(Object** out_targets);

	uint32 GetAvailableToolMask() const;

	void AddTools();
	void AddTool(const eEditorTool tool_type, const char* path, eEditorToolFlags flags);

	void HandleHotkeys();

	/// Selects the object under the crosshair
	void PickObject();

	void DrawModelSelection();

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
	void CreateDataBrushAtCrosshair();
	void DeleteSelection();
	void DupeSelection();

	void AdjustSnapLevel(int32 direction);

	/// Drops anything from the selection that the mode and filter don't allow, such as after its tags changed
	void PruneSelection();
	/// Moves off a tool that the mode and filter don't allow
	void EnsureToolAvailable();
	void ShowMode();

private:
	EditorFrame* mpMainFrame = nullptr;

	StackArray<EditorTool, static_cast<uint32>(eEditorTool::Count)> mTools;

	eEditorMode mMode = eEditorMode::Vis;
	eDataFilter mDataFilter = eDataFilter::All;

	eEditorView mView = eEditorView::Perspective;
	uint32 mTopViewCommandSerial = 0;

	eEditorTool mCurrentToolType = eEditorTool::None;
	EditorTool* mpCurrentTool = nullptr;

	EditorToolState mToolState {};
	EditorToolSelection mToolSelection {};

	MaterialID mLastUsedMaterial = MaterialID::scNull;

	/// Runs the Light tool
	LightEditor mLightEditor;

	BoundsEditor mBoundsEditor;

	GrabEditor mGrabEditor;

	SpawnEditor mSpawnEditor;

	EditorSelection mSelection;
	EditHistory mHistory { mSelection };

	/// Provides the `CMD_*` console commands
	script::Script* mpCommandScript = nullptr;

	bool mbDragging = false;
	bool mbDraggingSpawn = false;
	bool mbSpawnSelected = false;
	bool mbHadHeadbob = false;

	wxGUIEventLoop* mpEventLoop = nullptr;

	std::chrono::steady_clock::time_point mLastPanelSync = std::chrono::steady_clock::now();
	EditorPanelState mLastSentState;
	bool mbForcePanelSync = true;

	std::mutex mPendingStateMutex;
	EditorPanelState mPendingState;
	bool mbStatePosted = false;

	std::atomic<eGamePhase> mGamePhase = eGamePhase::Starting;
	std::atomic<bool> mbGameFinished = false;
	std::atomic<bool> mbAppActive = true;

	std::function<void()> mpReloadHandlers[static_cast<uint32>(eReloadTarget::Count)];
};


} // namespace fx::editor

#endif
