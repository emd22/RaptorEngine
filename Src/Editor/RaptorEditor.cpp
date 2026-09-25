#include "RaptorEditor.hpp"

#ifdef FX_IS_EDITOR

#include "EditorFrame.hpp"
#include "EditorPlatform.hpp"
#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/image.h>
#include <wx/init.h>

#include <Blockout.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Core/Log.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Script/Script.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <algorithm>


namespace fx::editor {

/// Raptor controls the frame loop, so the app has nothing to do on init
class EditorApp : public wxApp
{
public:
	bool OnInit() override
	{
		SetAppearance(wxApp::Appearance::Dark);
		return true;
	}
};


/// Upper bound on native events dispatched per frame, so a flood of them can't stall rendering
static constexpr uint32 scMaxEventsPerFrame = 256;

/// How far away objects can be picked
static constexpr float32 scPickRange = 4.0f;

/// How far away the crosshair can place a new object
static constexpr float32 scCreateRange = 20.0f;

/// Size of a snap step for each snap level. Mirrors tool_get_snap_multiplier() in `tool_common.strata`.
static constexpr float32 scSnapSteps[] = { 0.05f, 0.10f, 0.25f, 0.50f };
static constexpr int32 scSnapLevelCount = static_cast<int32>(std::size(scSnapSteps));

/// Where the transform marker is kept while it isn't in use, out of view
static const Vec3f scHiddenMarkerPosition = Vec3f(200.0f);

static const char* const scCommandScriptPath = "./Scripts/editor/editor_cmd.strata";


/////////////////////////////////////
// GUI
/////////////////////////////////////

bool RaptorEditor::InitGUI(int argc, char** argv)
{
	wxApp::SetInstance(new EditorApp);

	if (!wxEntryStart(argc, argv)) {
		LogError(LC_CORE, "Could not initialize the wxWidgets backend for the editor");
		return false;
	}

	// For the tool icons
	wxInitAllImageHandlers();

#ifdef FX_PLATFORM_MACOS
	platform::DisableWindowTabbing();
#endif

	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return false;
	}

	mpEventLoop = new wxGUIEventLoop;
	wxEventLoopBase::SetActive(mpEventLoop);

	AddTools();

	mpCommandScript = gScriptManager->LoadScript(scCommandScriptPath);

	return true;
}

EditorFrame* RaptorEditor::CreateMainFrame(const char* title, const Vec2u& viewport_size)
{
	mpMainFrame = new EditorFrame(wxString::FromAscii(title),
								  wxSize(static_cast<int>(viewport_size.X), static_cast<int>(viewport_size.Y)));

	mpMainFrame->Show();
	mpMainFrame->Raise();

#ifdef FX_PLATFORM_MACOS
	// Bring window to front
	platform::ActivateApp();
#endif

	mpMainFrame->GetViewport()->SetFocus();

	// Get the frame on screen and laid out before the renderer creates its surface on the viewport
	PumpEvents();

	return mpMainFrame;
}

bool RaptorEditor::PumpEvents()
{
	if (mpEventLoop == nullptr || mpMainFrame == nullptr) {
		return false;
	}

	for (uint32 i = 0; i < scMaxEventsPerFrame; i++) {
		if (mpEventLoop->DispatchTimeout(0) != 1) {
			break;
		}
	}

	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();

	EditorViewport* viewport = mpMainFrame->GetViewport();

	viewport->PollRelativeMouse();

	// Rebuild once for however many size events arrived
	if (viewport->ConsumeResize() && renderer::gGraphics != nullptr && renderer::gGraphics->GetWindow() != nullptr) {
		const wxSize size = viewport->GetClientSize();

		if (size.x > 0 && size.y > 0) {
			renderer::gGraphics->RebuildToResizedWindow();
		}
	}

	return !mpMainFrame->IsCloseRequested();
}

void RaptorEditor::RefreshPanels()
{
	if (mpMainFrame == nullptr) {
		return;
	}

	mpMainFrame->GetWorldPropertiesPanel()->Update();
	mpMainFrame->GetObjectPropertiesPanel()->ShowObject(mSelection.GetLast());
}

void RaptorEditor::SetReloadHandler(eReloadTarget target, std::function<void()> handler)
{
	mpReloadHandlers[static_cast<uint32>(target)] = std::move(handler);
}

void RaptorEditor::InvokeReloadHandler(eReloadTarget target)
{
	const std::function<void()>& handler = mpReloadHandlers[static_cast<uint32>(target)];

	if (handler != nullptr) {
		handler();
	}
}


/////////////////////////////////////
// Frame update
/////////////////////////////////////

void RaptorEditor::Update(float32 delta_time)
{
	if (IsSimulationMode()) {
		return;
	}

	HandleHotkeys();

	if (mpCurrentTool->UsesSelection() && ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		PickObject();
	}

	const bool mouse_down = ControlManager::IsKeyDown(eKey::FX_MOUSE_LEFT);

	if (mbDragging && !mouse_down) {
		EndDrag();
	}

	// The selection went away (deleted, or deselected) partway through, so there is nothing left to finish
	if (mbDragging && mpCurrentTool->UsesSelection() && mSelection.IsEmpty()) {
		CancelDrag();
	}

	if (!mbDragging) {
		mpCurrentTool->Controls();

		const bool has_target = (!mpCurrentTool->UsesSelection() || !mSelection.IsEmpty());

		if (mouse_down && has_target) {
			BeginDrag();
		}
	}

	if (mbDragging) {
		mpCurrentTool->Update(delta_time);
	}
}

void RaptorEditor::HandleHotkeys()
{
	// Snap
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_U)) {
		mToolState.ToolSnapEnabled = !mToolState.ToolSnapEnabled;
		SyncCurrentTool();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_MINUS)) {
		AdjustSnapLevel(-1);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_EQUALS)) {
		AdjustSnapLevel(1);
	}

	// Nothing below can happen partway through a drag
	if (mbDragging) {
		return;
	}

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LCTRL, eKey::FX_KEY_Z)) {
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
			Redo();
		}
		else {
			Undo();
		}
	}

	// Tools. Shift+R is left alone as it reloads the blockout.
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_T)) {
		SetTool(eEditorTool::Translate);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_F)) {
		SetTool(eEditorTool::Face);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_R) && !ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		SetTool(eEditorTool::Rotate);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_B)) {
		SetTool(eEditorTool::Create);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_C)) {
		SetTool(eEditorTool::Clip);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_V)) {
		SetTool(eEditorTool::SetMaterial);
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_K)) {
		CreateObjectAtCrosshair();
	}

	if (mSelection.IsEmpty()) {
		return;
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_TAB)) {
		ClearSelection();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_BACKSPACE)) {
		DeleteSelection();
	}

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LCTRL, eKey::FX_KEY_D)) {
		DupeSelection();
	}
}


/////////////////////////////////////
// Object picking
/////////////////////////////////////

/// Picks the probe volume that the crosshair is on, if there is nothing solid in front of it
static Object* PickProbeVolume(const PerspectiveCamera& camera, const Vec3f& pick_direction)
{
	float32 volume_distance = 0.0f;
	Object* volume = gWorld->RaycastProbeVolumes(camera.Position, pick_direction, scPickRange, volume_distance);

	if (volume == nullptr) {
		return nullptr;
	}

	// Geometry in front of the volume wins, so looking at a wall inside a volume still selects the wall. The
	// volume's box is hollow, so standing inside one doesn't put it in front of everything it contains.
	const physics::RayResult solid = gPhysics->pBackend->Raycast(camera.Position, pick_direction * scPickRange);

	if (solid.bHit && (solid.Point - camera.Position).Length() <= volume_distance) {
		return nullptr;
	}

	return volume;
}

void RaptorEditor::PickObject()
{
	const bool append_selection = ControlManager::IsKeyDown(eKey::FX_KEY_LALT);

	// Clicking away from the selection keeps it. Alt adds to it, Tab clears it.
	if (!mSelection.IsEmpty() && !append_selection) {
		return;
	}

	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;
	const Vec3f pick_direction = camera->GetForwardVector();

	Object* volume = PickProbeVolume(*camera, pick_direction);

	if (volume != nullptr && SelectObject(volume, append_selection)) {
		return;
	}

	SizedArray<JPH::BodyID> hits = gPhysics->pBackend->RaycastObjects(camera->Position, pick_direction * scPickRange);

	for (uint32 i = 0; i < hits.Size; i++) {
		physics::Body* body = gPhysics->FindBody(hits[i]);

		if (body == nullptr) {
			continue;
		}

		if (SelectObject(gObjectManager->GetObject(body->GetObjectID()), append_selection)) {
			return;
		}
	}

	ClearSelection();
}


/////////////////////////////////////
// Dragging
/////////////////////////////////////

void RaptorEditor::BeginDrag()
{
	// Captures where everything starts from
	SyncSelection();

	SetMovementDamped(true);
	mbDragging = true;

	mpCurrentTool->Begin();
}

void RaptorEditor::EndDrag()
{
	if (!mbDragging) {
		return;
	}

	mbDragging = false;
	mpCurrentTool->Finalize();

	SetMovementDamped(false);
}

void RaptorEditor::CancelDrag()
{
	if (!mbDragging) {
		return;
	}

	mbDragging = false;
	HideToolMarkers();

	SetMovementDamped(false);
}

void RaptorEditor::SetMovementDamped(bool damped)
{
	if (damped) {
		mbHadHeadbob = gCVars->Get("b_headbob_enabled", false);

		gWorld->Player.SpeedMultiplier = 0.5f;
		gCVars->Set("b_headbob_enabled", false);
	}
	else {
		gWorld->Player.SpeedMultiplier = 1.0f;
		gCVars->Set("b_headbob_enabled", mbHadHeadbob);
	}
}

void RaptorEditor::HideToolMarkers()
{
	Blockout* blockout = gWorld->pBlockout;

	if (blockout == nullptr) {
		return;
	}

	blockout->HidePreview();

	if (blockout->pXFormObject != nullptr) {
		blockout->pXFormObject->SetPosition(scHiddenMarkerPosition);
	}
}


/////////////////////////////////////
// Tools
/////////////////////////////////////

void RaptorEditor::AddTools()
{
	AddTool(eEditorTool::None, nullptr, eEditorToolFlags::None);
	AddTool(eEditorTool::Translate, "./Scripts/editor/tools/tool_translate.strata", eEditorToolFlags::UsesSelection);
	AddTool(eEditorTool::Face, "./Scripts/editor/tools/tool_face.strata", eEditorToolFlags::UsesSelection);
	AddTool(eEditorTool::Rotate, "./Scripts/editor/tools/tool_rotate.strata", eEditorToolFlags::UsesSelection);
	AddTool(eEditorTool::Create, "./Scripts/editor/tools/tool_create.strata", eEditorToolFlags::None);
	AddTool(eEditorTool::Clip, "./Scripts/editor/tools/tool_clip.strata", eEditorToolFlags::UsesSelection);
	AddTool(eEditorTool::SetMaterial, "./Scripts/editor/tools/tool_set_material.strata", eEditorToolFlags::None);

	mpCurrentTool = GetTool(mCurrentToolType);
}

void RaptorEditor::AddTool(const eEditorTool tool_type, const char* path, eEditorToolFlags flags)
{
	// Tools are looked up by their index, so they have to be added in order
	Assert(static_cast<uint32>(tool_type) == mTools.Size);

	mTools.Insert(EditorTool(tool_type, path, flags));
}

EditorTool* RaptorEditor::GetTool(const eEditorTool tool_type)
{
	const uint32 tool_index = static_cast<uint32>(tool_type);

	if (tool_index >= mTools.Size) {
		LogError(LC_CORE, "Tool {} has not been registered", tool_index);
		return nullptr;
	}

	return &mTools[tool_index];
}

void RaptorEditor::SetTool(eEditorTool tool)
{
	EditorTool* new_tool = GetTool(tool);

	if (new_tool == nullptr) {
		return;
	}

	if (tool != mCurrentToolType) {
		// Switching tools keeps what the current drag has done so far
		EndDrag();

		mpCurrentTool->Leave();
		HideToolMarkers();

		mCurrentToolType = tool;
		mpCurrentTool = new_tool;

		// Nothing stays selected while simulating
		if (IsSimulationMode()) {
			mSelection.Clear();
		}

		SyncSelection();
		mpCurrentTool->Enter();
	}

	// Also run when the tool is unchanged, as clicking the selected tool's button toggles it off
	if (mpMainFrame != nullptr) {
		mpMainFrame->ShowSelectedTool(mCurrentToolType);
	}
}

void RaptorEditor::ReloadScripts()
{
	// The drag's state is in the scripts that are about to be reloaded
	EndDrag();

	gScriptManager->ReloadAllScripts();

	for (EditorTool& tool : mTools) {
		tool.ReloadHotFunctions();
	}

	// The scripts start over with fresh globals
	SyncSelection();
	mpCurrentTool->Enter();
}

void RaptorEditor::SubmitToolState(const EditorToolState& state)
{
	// The selected tool and the transform marker belong to the editor, so only the snap settings are taken
	mToolState.ToolSnapLevel = std::clamp(state.ToolSnapLevel, 0, scSnapLevelCount - 1);
	mToolState.ToolSnapEnabled = state.ToolSnapEnabled;

	SyncCurrentTool();
}

float32 RaptorEditor::GetSnapStep() const
{
	return scSnapSteps[std::clamp(mToolState.ToolSnapLevel, 0, scSnapLevelCount - 1)];
}

void RaptorEditor::AdjustSnapLevel(int32 direction)
{
	mToolState.ToolSnapLevel = std::clamp(mToolState.ToolSnapLevel + direction, 0, scSnapLevelCount - 1);

	SyncCurrentTool();
}

Vec3f RaptorEditor::SnapToGrid(const Vec3f& position) const
{
	if (!mToolState.ToolSnapEnabled) {
		return position;
	}

	const float32 step = GetSnapStep();

	return Vec3f(simd::Round((position / step).mIntrin)) * step;
}

void RaptorEditor::SyncSelection()
{
	const uint32 count = mSelection.GetCount();

	for (uint32 i = 0; i < count; i++) {
		Object* object = mSelection.GetObject(i);

		mToolSelection.pSelection[i] = object;
		mToolSelection.pInitialObjectPositions[i] = object->GetPosition().mIntrin;
		mToolSelection.pInitialObjectRotations[i] = object->mRotation.GetEulerAngles().mIntrin;
	}

	mToolSelection.SelectionSize = static_cast<int32>(count);

	SyncCurrentTool();
}

void RaptorEditor::SyncCurrentTool()
{
	mToolState.SelectedTool = mCurrentToolType;
	mToolState.pTransformMarkerObject = (gWorld->pBlockout != nullptr) ? gWorld->pBlockout->pXFormObject : nullptr;

	mpCurrentTool->Sync(mToolState, mToolSelection);
}


/////////////////////////////////////
// Selection
/////////////////////////////////////

bool RaptorEditor::SelectObject(Object* object, bool append_selection)
{
	if (object == nullptr) {
		ClearSelection();
		return false;
	}

	if (object->HasTags(eObjectTag::LockTransform)) {
		return false;
	}

	if (mSelection.Contains(object)) {
		return true;
	}

	if (!append_selection) {
		mSelection.Clear();
	}

	const bool selected = mSelection.Add(object);

	if (!selected) {
		LogWarning(LC_CORE, "Cannot select '{}', the selection is full", object->Name.Get());
	}

	SyncSelection();

	return selected;
}

void RaptorEditor::ClearSelection()
{
	if (mSelection.IsEmpty()) {
		return;
	}

	mSelection.Clear();
	SyncSelection();
}

void RaptorEditor::SetStoredMaterial(Object* object, MaterialID material)
{
	mSelection.SetStoredMaterial(object, material);
}


/////////////////////////////////////
// Edit operations
/////////////////////////////////////

EditOperationValue RaptorEditor::PushEditOperation(const EditOperation& op)
{
	const EditOperationValue result = mHistory.Push(op);

	// Deleting an object takes it out of the selection
	if (op.ChangesObjects()) {
		SyncSelection();
	}

	return result;
}

void RaptorEditor::DeleteObject(Object* object, int32 group_size)
{
	if (object == nullptr) {
		return;
	}

	// Snapshot everything Undo needs before the Execute step destroys the object.
	EditOperation op {
		.Type = EditOperation::eType::Delete,
		.pObject = object,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
		.GroupSize = group_size,
	};

	op.PushedObjectID = object->ID;
	op.ObjectSnapshot.Position = object->GetPosition();

	const Brush* brush = gWorld->pBlockout->GetBrush(object);
	op.PlanesBefore = (brush != nullptr) ? brush->Planes
										 : Brush::FromBox(object->Bounds.Min, object->Bounds.Max).Planes;

	op.ObjectSnapshot.Material = mSelection.GetStoredMaterial(object);
	op.ObjectSnapshot.Rotation = object->mRotation;
	op.ObjectSnapshot.ObjectName = object->Name;
	op.ObjectSnapshot.bIsProbeVolume = object->IsProbeVolume();

	PushEditOperation(op);
}

void RaptorEditor::CreateObjectAtCrosshair()
{
	const Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera->Position,
															   camera->GetForwardVector() * scCreateRange);

	const Vec3f origin = SnapToGrid(hit.bHit ? hit.Point : Vec3f::sZero);

	const EditOperationValue created = PushEditOperation(EditOperation {
		.Type = EditOperation::eType::Create,
		.pObject = nullptr,
		.ValueA = EditOperationValue(origin),
		.ValueB = EditOperationValue(nullptr),
	});

	SelectObject(created.pObject, false);
}

void RaptorEditor::DeleteSelection()
{
	// Each delete takes its object out of the selection, so work from a copy
	const uint32 count = mSelection.GetCount();

	Object* objects[scMaxSelectedObjects];

	for (uint32 i = 0; i < count; i++) {
		objects[i] = mSelection.GetObject(i);
	}

	for (uint32 i = 0; i < count; i++) {
		DeleteObject(objects[i], static_cast<int32>(count));
	}
}

void RaptorEditor::DupeSelection()
{
	const uint32 count = mSelection.GetCount();

	Object* dupes[scMaxSelectedObjects];
	uint32 dupe_count = 0;

	// The dupes are made while the originals are still selected, so they are given the originals' own materials
	// instead of the selection material
	for (uint32 i = 0; i < count; i++) {
		Object* original = mSelection.GetObject(i);

		const EditOperationValue dupe = PushEditOperation(EditOperation {
			.Type = EditOperation::eType::Dupe,
			.pObject = original,
			.ValueA = EditOperationValue(original->GetPosition()),
			.ValueB = EditOperationValue(nullptr),
			.GroupSize = static_cast<int32>(count),
		});

		if (dupe.pObject != nullptr) {
			dupes[dupe_count++] = dupe.pObject;
		}
	}

	// Carry on working with the dupes
	mSelection.Clear();

	for (uint32 i = 0; i < dupe_count; i++) {
		mSelection.Add(dupes[i]);
	}

	SyncSelection();
}

void RaptorEditor::Undo()
{
	if (mHistory.Undo()) {
		SyncSelection();
	}
}

void RaptorEditor::Redo()
{
	if (mHistory.Redo()) {
		SyncSelection();
	}
}

void RaptorEditor::ForgetObjects()
{
	CancelDrag();

	mSelection.Clear();
	mHistory.Clear();

	SyncSelection();
}


/////////////////////////////////////
// Commands
/////////////////////////////////////

bool RaptorEditor::RunCommand(const String& name)
{
	if (mpCommandScript == nullptr) {
		return false;
	}

	auto command = mpCommandScript->GetFunction<void()>(String::Fmt("CMD_{}", name).CStr());

	if (command == nullptr) {
		return false;
	}

	// Commands work on the selection, so give the script the current one
	mpCommandScript->CallFunction<void(const EditorToolState*)>("tool_state_receive", &mToolState);
	mpCommandScript->CallFunction<void(const EditorToolSelection*)>("tool_selection_receive", &mToolSelection);

	mpCommandScript->CallFunctionPtr<void()>(command);

	return true;
}


void RaptorEditor::Destroy()
{
	if (mpMainFrame != nullptr) {
		mpMainFrame->Destroy();
		mpMainFrame = nullptr;
	}

	// Runs the frame's delayed deletion
	if (wxTheApp != nullptr) {
		wxTheApp->ProcessIdle();
	}

	wxEventLoopBase::SetActive(nullptr);

	delete mpEventLoop;
	mpEventLoop = nullptr;

	wxEntryCleanup();
}

} // namespace fx::editor

#endif
