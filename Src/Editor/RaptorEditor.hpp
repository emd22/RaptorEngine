#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "EditorSelection.hpp"
#include "EditorTheme.hpp"
#include "EditorTool.hpp"
#include "GrabEditor.hpp"
#include "LightEditor.hpp"
#include "Operation/BaseOperation.hpp"
#include "Operation/History.hpp"
#include "SpawnEditor.hpp"

#include <wx/evtloop.h>

// wxWidgets pulls in windows.h on Windows, which #defines LoadImage to LoadImageW and breaks AssetManager::LoadImage
#ifdef LoadImage
#undef LoadImage
#endif

#include <Brush.hpp>
#include <Core/DynArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/PrimitiveMesh.hpp>
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
	static constexpr std::chrono::seconds scWarningDuration { 6 };

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
	 * @brief Runs the editor for a frame: draws the selection and markers in the 3D view while editing, and the grab
	 * helper while simulating.
	 */
	void Update(float32 delta_time);

	void UpdateCompiledMapViewLock();

	/////////////////////////////////////
	// Tools
	/////////////////////////////////////

	void SetTool(eEditorTool tool);

	FX_FORCE_INLINE eEditorTool GetCurrentTool() const { return mCurrentTool; }

	/////////////////////////////////////
	// Simulation
	/////////////////////////////////////

	void SetSimulating(bool simulating);

	FX_FORCE_INLINE bool IsSimulationMode() const { return mbSimulating; }
	FX_FORCE_INLINE bool IsGrabbing() const { return mGrabEditor.IsHolding(); }

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
	FX_FORCE_INLINE bool IsTopViewActive() const { return mView != eEditorView::Viewport; }

	bool IsCompiledMapOpen() const;

	FX_FORCE_INLINE void NoteTopViewCommand() { ++mTopViewCommandSerial; }

	void TopViewSelect(const DynArray<ObjectID>& object_ids, eTopViewSelectMode mode);
	void TopViewMove(const Vec3f offset);
	void TopViewRotate(const Vec3f axis, float32 angle, const Vec3f pivot, const Vec3f offset);
	void TopViewMoveFace(const Vec3f world_normal, float32 distance, const DynArray<ObjectID>& object_ids);
	void TopViewMoveVertices(eViewPlane plane, const DynArray<TopViewVertexHandle>& handles, const Vec3f offset);
	void TopViewCreate(eViewPlane plane, Vec3f min, Vec3f max, bool drop_to_ground);
	void TopViewCreateRoom(eViewPlane plane, Vec3f min, Vec3f max, bool drop_to_ground, const RoomSettings& settings);
	void TopViewHollowSelection(const RoomSettings& settings);
	void TopViewCreateArch(eViewPlane plane, DynArray<Vec3f> points, bool drop_to_ground);
	void TopViewCreateDecal(eViewPlane plane, Vec3f min, Vec3f max, bool drop_to_ground);
	void TopViewCreatePatch(eViewPlane plane, ePatchPrimitive primitive, Vec3f min, Vec3f max, bool drop_to_ground,
							const PatchCommand& settings);
	void TopViewPatchCommand(eViewPlane plane, const PatchCommand& command,
							 const DynArray<TopViewVertexHandle>& handles);
	void TopViewClip(const Vec3f point_a, const Vec3f point_b, const Vec3f view_axis);
	void TopViewSubtract(bool keep_cutters);
	void TopViewEditTextures(const DynArray<TopViewFaceRef>& faces, const TextureCommand& command);
	void TopViewSetFaceMaterial(const DynArray<TopViewFaceRef>& faces, MaterialID material);
	void TopViewSetPickedFaces(const DynArray<TopViewFaceRef>& faces);
	void TopViewSelectLight(uint32 light_id);
	void TopViewMoveLight(const Vec3f offset);
	void TopViewAimLight(const Vec3f direction);
	void TopViewCreateLight(const Vec3f position);
	void TopViewSelectSpawn();
	void TopViewMoveSpawn(const Vec3f offset);
	void TopViewAimSpawn(const Vec3f direction);
	void TopViewRespawn();
	void TopViewToggleSubview();
	void TopViewDelete();
	void TopViewDuplicate();
	/// Hides the selection from the 2D views. It stays in the level and in the 3D view.
	void TopViewHideSelection();
	void TopViewShowAll();
	void TopViewToggleSnap();
	void TopViewAdjustSnap(int32 direction);

	uint32 SnapSelectionToGrid();

	Object* CreateWorldBox(const Vec3f min, const Vec3f max);
	Object* CreateWorldPatch(Patch&& world_patch, int32 group_size = 1, MaterialID material = MaterialID::scNull);
	void FitDBoundsToLevel();

	FX_FORCE_INLINE bool IsSpawnSelected() const { return mbSpawnSelected; }

	bool ClipObject(Object* object, const Vec3f point_a, const Vec3f point_b, const Vec3f direction);

	uint32 SubtractSelection(bool keep_cutters);

	/// Vis mode selects everything but data brushes, Data mode only the data brushes the filter lets through
	bool IsObjectSelectable(const Object* object) const;

	void ReloadScripts();

	FX_FORCE_INLINE const EditorToolState& GetToolState() const { return mToolState; }

	/// Takes the snap settings from the command script
	void SubmitToolState(const EditorToolState& state);

	/// The size of a snap step, in metres
	float32 GetSnapStep() const;

	/// Rounds a position (or a distance) to the snap step, if snapping is on
	Vec3f SnapToGrid(const Vec3f position) const;

	Vec3f RoundToGrid(const Vec3f position) const;

	float32 GetAngleSnapStep() const;

	const EditorTheme& GetTheme() const { return mTheme; }
	void SetTheme(const EditorTheme& theme) { mTheme = theme; }

	/////////////////////////////////////
	// Selection
	/////////////////////////////////////

	/**
	 * @brief Selects an object, adding it to the selection if `append_selection` is set or replacing the selection
	 * otherwise. Passing nullptr clears the selection. Returns true if the object is now selected.
	 */
	bool SelectObject(Object* object, bool append_selection);
	/// Takes an object out of the selection, along with the rest of the subview it was placed with
	void DeselectObject(Object* object);
	void ClearSelection();

	void DeselectModels();

	bool IsSpawnPointShown() const;
	void SelectSpawn();
	void DeselectSpawn();

	FX_FORCE_INLINE const EditorSelection& GetSelection() const { return mSelection; }

	FX_FORCE_INLINE LightEditor& GetLightEditor() { return mLightEditor; }

	/// Changes a brush's material, and makes it the one new brushes are made with
	void SetObjectMaterial(Object* object, MaterialID material);

	/// The material new brushes are made with: the last one chosen for an object, or the default one
	MaterialID GetNewBrushMaterial() const;
	MaterialID GetNewDecalMaterial() const;

	Object* CreateReflectionProbeAtPlayer();

	void SetSunEnabled(bool enabled);
	void SetSunColor(Color color);
	void SetSunIntensity(float32 intensity);
	void SetSunAngles(float32 azimuth_degrees, float32 elevation_degrees);
	void SetSunAzimuth(float32 azimuth_degrees);
	void SetSunElevation(float32 elevation_degrees);

	void SetFog(const renderer::FogSettings& fog);

	/// Creates a data brush of the kind the filter is set to, filling a world space box
	Object* CreateDataBrush(const Brush::PlaneList& planes, const Vec3f position);
	uint32 SetSelectionReflectionProbe(bool enabled);

	uint32 SetSelectionObjectState(eObjectTag tag, bool enabled);
	uint32 SetSelectionObjectState(eObjectFlags flag, bool enabled);

	uint32 SetSelectionScript(const String& path);

	uint32 SetSelectionEnterDirection(const Vec3f local_direction);

	bool CanRenameObject(const Object* object) const;
	bool RenameObject(Object* object, const String& name);
	/// Names the selection, numbering the names after the first so they stay unique
	void RenameSelection(const String& name);

	/////////////////////////////////////
	// Subviews
	/////////////////////////////////////

	bool IsEditingSubview() const;

	/**
	 * @brief True if the object can be changed on its own right now. While a subview is being edited that is only
	 * its brushes, and otherwise it is everything but the subviews placed in the level.
	 */
	bool CanEditObject(const Object* object) const;

	/// Turns the selected brushes into a new subview, placed where they are
	bool CreateSubviewFromSelection(const String& name);

	/// Starts editing the selected subview
	bool IsSubviewSelected() const;
	bool EnterSubview();
	void LeaveSubview();

	/// Places another copy of a subview at the crosshair
	bool PlaceSubview(const String& name);

	/// Turns the selected subviews back into brushes of the level
	uint32 DissolveSelectedSubviews();

	/// The subview instance that a dupe of `original` becomes, if it is a placed subview
	uint32 AllocateDupeSubview(const Object* original);

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

	bool HasUnsavedChanges() const;
	void MarkSaved();
	void MarkModified();

	template <typename... TTypes>
	void Warn(const char* fmt, TTypes&&... args)
	{
		const String message = String::Fmt(fmt, args...);

		LogWarning(LC_CORE, "{}", message.CStr());
		ShowStatusMessage(message, scWarningDuration);
	}

	uint32 ShowStatusMessage(const String& text,
							 std::chrono::milliseconds duration = std::chrono::milliseconds::zero());
	void ClearStatusMessage(uint32 id);

	/////////////////////////////////////
	// Commands
	/////////////////////////////////////

	/// Runs `CMD_<name>` from the editor command script. Returns false if there is no such command.
	bool RunCommand(const String& name);

	~RaptorEditor() = default;

private:
	template <typename TState>
	uint32 SetSelectionObjectStateImpl(TState state, bool enabled);

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

	Object* FindTopViewObject(ObjectID object_id);
	uint32 CollectTopViewTargets(Object** out_targets);

	uint32 GetAvailableToolMask() const;

	void DrawModelSelection();
	void DrawSelectionOverlay();
	void DrawPickedFaces();
	void DrawSubviews();

	bool SelectSubview(uint32 instance, bool append_selection);

	/// Copies the selection over to the command script
	void SyncSelection();

	void DeleteSelection();
	void DupeSelection();

	void AdjustSnapLevel(int32 direction);

	/// Drops anything from the selection that the mode and filter don't allow, such as after its tags changed
	void PruneSelection();
	/// Moves off a tool that the mode and filter don't allow
	void EnsureToolAvailable();
	void ShowMode();
	void ShowTool();

private:
	EditorFrame* mpMainFrame = nullptr;

	eEditorMode mMode = eEditorMode::Vis;
	eDataFilter mDataFilter = eDataFilter::All;

	eEditorView mView = eEditorView::Viewport;
	bool mbHasCreateReference = false;
	Vec3f mCreateReferenceMin = Vec3f::sZero;
	Vec3f mCreateReferenceMax = Vec3f::sZero;

	eEditorView mViewBeforeCompiledMap = eEditorView::Viewport;
	bool mbViewLockedByCompiledMap = false;
	uint32 mTopViewCommandSerial = 0;

	eEditorTool mCurrentTool = eEditorTool::Select;
	bool mbSimulating = false;

	EditorToolState mToolState {};
	EditorToolSelection mToolSelection {};

	MaterialID mLastUsedMaterial = MaterialID::scNull;
	MaterialID mLastDecalMaterial = MaterialID::scNull;

	EditorTheme mTheme {};

	LightEditor mLightEditor;

	GrabEditor mGrabEditor;

	SpawnEditor mSpawnEditor;

	EditorSelection mSelection;
	EditHistory mHistory { mSelection };

	struct PickedFaceOverlay
	{
		TopViewFaceRef Face;
		Ref<PrimitiveMesh> pMesh;
		SizedArray<Vec3f> Vertices;
	};

	DynArray<PickedFaceOverlay> mPickedFaces;

	uint64 mSavedRevision = 0;
	bool mbUntrackedChanges = false;
	uint32 mUntrackedEditCount = 0;

	uint64 mDecalRevision = UINT64_MAX;
	uint32 mDecalUntrackedEditCount = 0;

	/// Provides the `CMD_*` console commands
	script::Script* mpCommandScript = nullptr;

	bool mbSpawnSelected = false;

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
