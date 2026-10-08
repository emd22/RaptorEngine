#include "RaptorEditor.hpp"

#ifdef FX_IS_EDITOR

#include "EditorFrame.hpp"
#include "EditorPlatform.hpp"
#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "ToolSettingsPanel.hpp"
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
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ObjectScripts.hpp>
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

static constexpr float32 scAngleSnapSteps[] = { 5.0f, 15.0f, 45.0f, 90.0f };
static_assert(std::size(scAngleSnapSteps) == std::size(scSnapSteps));

/// Where the transform marker is kept while it isn't in use, out of view
static const Vec3f scHiddenMarkerPosition = Vec3f(200.0f);

static constexpr float32 scDataPickRange = 32.0f;

static const Vec3f scDefaultDataBrushHalfExtent(2.0f, 1.5f, 2.0f);

static constexpr uint32 ToolBit(eEditorTool tool) { return 1u << static_cast<uint32>(tool); }

static constexpr uint32 scVisTools = ToolBit(eEditorTool::Translate) | ToolBit(eEditorTool::Face) |
									 ToolBit(eEditorTool::Rotate) | ToolBit(eEditorTool::Create) |
									 ToolBit(eEditorTool::Clip) | ToolBit(eEditorTool::Bounds) |
									 ToolBit(eEditorTool::Grab) | ToolBit(eEditorTool::Subtract);

static constexpr uint32 scDataTools[static_cast<uint32>(eDataFilter::Count)] = {
	ToolBit(eEditorTool::Translate) | ToolBit(eEditorTool::Face) | ToolBit(eEditorTool::Light),
	ToolBit(eEditorTool::Translate) | ToolBit(eEditorTool::Face) | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Translate) | ToolBit(eEditorTool::Face) | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Translate),
	ToolBit(eEditorTool::Translate) | ToolBit(eEditorTool::Face) | ToolBit(eEditorTool::Rotate) |
		ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Light),
};

static constexpr eEditorTool scFallbackTools[] = {
	eEditorTool::Translate, eEditorTool::Face, eEditorTool::Create, eEditorTool::Light,
};

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

	mpMainFrame->ShowMode(mMode, mDataFilter);
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

	if (ToolSettingsBasePanel* tool_settings = mpMainFrame->GetToolSettingsPanel(); tool_settings != nullptr) {
		tool_settings->Refresh();
	}
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

	if (!mbDragging) {
		PruneSelection();
	}

	if (mpCurrentTool->UsesSelection() && ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		PickObject();
	}

	const bool mouse_down = ControlManager::IsKeyDown(eKey::FX_MOUSE_LEFT);

	if (mbDragging && !mouse_down) {
		EndDrag();
	}

	// The selection went away (deleted, or deselected) partway through, so there is nothing left to finish
	if (mbDragging && !mbDraggingSpawn && mpCurrentTool->UsesSelection() && mSelection.IsEmpty()) {
		CancelDrag();
	}

	if (!mbDragging) {
		if (mbSpawnSelected) {
			mSpawnEditor.Controls();
		}
		else {
			mpCurrentTool->Controls();
		}

		const bool has_target = mbSpawnSelected || (!mpCurrentTool->UsesSelection() || !mSelection.IsEmpty());

		if (mouse_down && has_target) {
			BeginDrag();
		}
	}

	if (mbDragging) {
		if (mbDraggingSpawn) {
			mSpawnEditor.Update(delta_time);
		}
		else {
			mpCurrentTool->Update(delta_time);
		}
	}

	if (!mbSpawnSelected && IsSpawnPointShown()) {
		mSpawnEditor.DrawMarker(false);
	}

	DrawModelSelection();
}

static void DrawModelBounds(Object* object, const Color& color)
{
	if (object->pMesh.IsValid()) {
		const Vec3f half_extent = (object->Bounds.Max - object->Bounds.Min) * 0.5f;
		const Vec3f center = (object->Bounds.Max + object->Bounds.Min) * 0.5f;

		renderer::gDebugDraw->WireBox(
			Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * object->GetWorldMatrix(), color);
	}

	for (ObjectID attached_id : object->AttachedNodes) {
		Object* attached = gObjectManager->GetObject(attached_id);

		if (attached != nullptr) {
			DrawModelBounds(attached, color);
		}
	}
}

void RaptorEditor::DrawModelSelection()
{
	static const Color scSelectedColor = Color::FromRGBA(255, 220, 60, 255);

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (!object->HasTags(eObjectTag::Blockout)) {
			DrawModelBounds(object, scSelectedColor);
		}
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
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_I)) {
		SetTool(eEditorTool::Light);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_O)) {
		SetTool(eEditorTool::Bounds);
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_Y)) {
		SetTool(eEditorTool::Grab);
	}
	// if (ControlManager::IsKeyPressed(eKey::FX_KEY_N)) {
	// 	SetTool(eEditorTool::Subtract);
	// }

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_K)) {
		if (mCurrentToolType == eEditorTool::Light) {
			mLightEditor.CreateAtCrosshair();
		}
		else if (IsDataMode() && (mbSpawnSelected || mDataFilter == eDataFilter::SpawnPoints)) {
			mSpawnEditor.PlaceAtCrosshair();
		}
		else if (IsDataMode()) {
			CreateDataBrushAtCrosshair();
		}
		else {
			CreateObjectAtCrosshair();
		}
	}

	if (mbSpawnSelected && ControlManager::IsKeyPressed(eKey::FX_KEY_TAB)) {
		DeselectSpawn();
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

static Object* FindModelRoot(Object* object)
{
	while (!object->ParentID.IsInvalid()) {
		Object* parent = gObjectManager->GetObject(object->ParentID);

		if (parent == nullptr) {
			break;
		}

		object = parent;
	}

	return object;
}

static Object* PickModel(const PerspectiveCamera& camera, const Vec3f pick_direction, float32& out_distance)
{
	Object* nearest = nullptr;
	float32 nearest_distance = scPickRange;

	for (Object& object : gObjectManager->GetCache()) {
		if (object.HasTags(eObjectTag::Blockout) || !object.pMesh.IsValid() ||
			object.GetObjectLayer() == eObjectLayer::PlayerLayer) {
			continue;
		}

		Vec3f face;
		const float32 distance = object.RaycastBounds(camera.Position, pick_direction, face);

		if (distance < 0.0f || distance >= nearest_distance) {
			continue;
		}

		if (object.ContainsPoint(camera.Position) || !FindModelRoot(&object)->bIsAddedToWorld.load()) {
			continue;
		}

		nearest = &object;
		nearest_distance = distance;
	}

	out_distance = nearest_distance;

	return (nearest != nullptr) ? FindModelRoot(nearest) : nullptr;
}

void RaptorEditor::PickObject()
{
	const bool append_selection = ControlManager::IsKeyDown(eKey::FX_KEY_LALT);

	// Clicking away from the selection keeps it. Alt adds to it, Tab clears it.
	if ((!mSelection.IsEmpty() || mbSpawnSelected) && !append_selection) {
		return;
	}

	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;
	const Vec3f pick_direction = camera->GetForwardVector();

	if (IsDataMode()) {
		float32 volume_distance = 0.0f;
		Object* volume = gWorld->RaycastProbeVolumes(camera->Position, pick_direction, scDataPickRange,
													 volume_distance,
													 [this](const Object& object) { return IsObjectSelectable(&object); });

		float32 spawn_distance = 0.0f;
		const bool spawn_hit = IsSpawnPointShown() &&
							   mSpawnEditor.Raycast(camera->Position, pick_direction, scDataPickRange, spawn_distance);

		if (spawn_hit && (volume == nullptr || spawn_distance <= volume_distance)) {
			SelectSpawn();
			return;
		}

		if (volume != nullptr && SelectObject(volume, append_selection)) {
			return;
		}

		ClearSelection();
		return;
	}

	if (mpCurrentTool->UsesModels()) {
		float32 model_distance = 0.0f;
		Object* model = PickModel(*camera, pick_direction, model_distance);

		if (model != nullptr) {
			const physics::RayResult solid = gPhysics->pBackend->Raycast(camera->Position,
																		 pick_direction * scPickRange);

			if (!solid.bHit || model_distance <= (solid.Point - camera->Position).Length()) {
				if (SelectObject(model, append_selection)) {
					return;
				}
			}
		}
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

	if (!HasFlag(mpCurrentTool->Flags, eEditorToolFlags::KeepBob)) {
		SetMovementDamped(true);
	}

	mbDragging = true;
	mbDraggingSpawn = mbSpawnSelected;

	if (mbDraggingSpawn) {
		mSpawnEditor.Begin();
	}
	else {
		mpCurrentTool->Begin();
	}
}

void RaptorEditor::EndDrag()
{
	if (!mbDragging) {
		return;
	}

	mbDragging = false;

	if (mbDraggingSpawn) {
		mSpawnEditor.Finalize();
	}
	else {
		mpCurrentTool->Finalize();
	}

	mbDraggingSpawn = false;

	if (!HasFlag(mpCurrentTool->Flags, eEditorToolFlags::KeepBob)) {
		SetMovementDamped(false);
	}
}

void RaptorEditor::CancelDrag()
{
	if (!mbDragging) {
		return;
	}

	mbDragging = false;

	if (mbDraggingSpawn) {
		mSpawnEditor.Cancel();
	}
	else {
		mpCurrentTool->Cancel();
	}

	mbDraggingSpawn = false;
	HideToolMarkers();

	if (!HasFlag(mpCurrentTool->Flags, eEditorToolFlags::KeepBob)) {
		SetMovementDamped(false);
	}
}

void RaptorEditor::SetMovementDamped(bool damped)
{
	if (damped) {
		mbHadHeadbob = gCVars->Get("b_headbob_enabled", false);

		// gWorld->Player.SpeedMultiplier = 0.5f;
		gCVars->Set("b_headbob_enabled", false);
	}
	else {
		// gWorld->Player.SpeedMultiplier = 1.0f;
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
	AddTool(eEditorTool::Translate, "./Scripts/editor/tools/tool_translate.strata",
			eEditorToolFlags::UsesSelection | eEditorToolFlags::UsesModels);
	AddTool(eEditorTool::Face, "./Scripts/editor/tools/tool_face.strata", eEditorToolFlags::UsesSelection);
	AddTool(eEditorTool::Rotate, "./Scripts/editor/tools/tool_rotate.strata",
			eEditorToolFlags::UsesSelection | eEditorToolFlags::UsesModels);
	AddTool(eEditorTool::Create, "./Scripts/editor/tools/tool_create.strata", eEditorToolFlags::None);
	AddTool(eEditorTool::Clip, "./Scripts/editor/tools/tool_clip.strata", eEditorToolFlags::UsesSelection);

	// The lights can't be reached from scripts, so this tool is written in C++
	AddTool(eEditorTool::Light, nullptr, eEditorToolFlags::ClearsSelection);
	GetTool(eEditorTool::Light)->SetNative(&mLightEditor);
	GetTool(eEditorTool::Light)
		->SetSettingsPanel([]() -> ToolSettingsBasePanel* { return new LightToolSettingsPanel(); });

	AddTool(eEditorTool::Bounds, nullptr, eEditorToolFlags::UsesSelection | eEditorToolFlags::UsesModels);
	GetTool(eEditorTool::Bounds)->SetNative(&mBoundsEditor);

	AddTool(eEditorTool::Grab, nullptr, eEditorToolFlags::KeepBob);
	GetTool(eEditorTool::Grab)->SetNative(&mGrabEditor);

	AddTool(eEditorTool::Subtract, "./Scripts/editor/tools/tool_subtract.strata", eEditorToolFlags::UsesSelection);

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

	if (new_tool == nullptr || (tool != eEditorTool::None && !IsToolAvailable(tool))) {
		return;
	}

	if (tool != mCurrentToolType) {
		// Switching tools keeps what the current drag has done so far
		EndDrag();

		mpCurrentTool->Leave();
		HideToolMarkers();

		mCurrentToolType = tool;
		mpCurrentTool = new_tool;

		if (tool != eEditorTool::Translate) {
			DeselectSpawn();
		}

		// Nothing stays selected while simulating, and the Light tool works on lights instead of objects
		if (IsSimulationMode() || HasFlag(new_tool->Flags, eEditorToolFlags::ClearsSelection)) {
			mSelection.Clear();
		}
		else if (!new_tool->UsesModels()) {
			DeselectModels();
		}

		SyncSelection();
		mpCurrentTool->Enter();

		if (mpMainFrame != nullptr) {
			mpMainFrame->SetToolSettingsPanel(mpCurrentTool->CreateSettingsPanel());
		}
	}

	// Also run when the tool is unchanged, as clicking the selected tool's button toggles it off
	if (mpMainFrame != nullptr) {
		mpMainFrame->ShowSelectedTool(mCurrentToolType);
	}
}

bool RaptorEditor::IsToolAvailable(eEditorTool tool) const
{
	if (tool == eEditorTool::None) {
		return true;
	}

	const uint32 available = IsDataMode() ? scDataTools[static_cast<uint32>(mDataFilter)] : scVisTools;

	return (available & ToolBit(tool)) != 0;
}

void RaptorEditor::EnsureToolAvailable()
{
	if (IsToolAvailable(mCurrentToolType)) {
		return;
	}

	for (eEditorTool fallback : scFallbackTools) {
		if (IsToolAvailable(fallback)) {
			SetTool(fallback);
			return;
		}
	}

	SetTool(eEditorTool::None);
}

void RaptorEditor::ShowMode()
{
	if (mpMainFrame != nullptr) {
		mpMainFrame->ShowMode(mMode, mDataFilter);
	}
}

void RaptorEditor::SetMode(eEditorMode mode)
{
	if (mode != mMode) {
		EndDrag();
		ClearSelection();

		mMode = mode;

		EnsureToolAvailable();
	}

	ShowMode();
}

void RaptorEditor::SetDataFilter(eDataFilter filter)
{
	if (filter != mDataFilter) {
		EndDrag();

		mDataFilter = filter;

		PruneSelection();
		EnsureToolAvailable();
	}

	ShowMode();
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

float32 RaptorEditor::GetAngleSnapStep() const
{
	if (!mToolState.ToolSnapEnabled) {
		return 0.0f;
	}

	return scAngleSnapSteps[std::clamp(mToolState.ToolSnapLevel, 0, scSnapLevelCount - 1)];
}

Vec3f RaptorEditor::SnapToGrid(const Vec3f position) const
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

static bool MatchesDataFilter(const Object& object, eDataFilter filter)
{
	switch (filter) {
	case eDataFilter::All:
		return true;
	case eDataFilter::ProbeVolumes:
		return object.IsProbeVolume() && !object.IsReflectionProbe();
	case eDataFilter::ReflectionProbes:
		return object.IsReflectionProbe();
	case eDataFilter::Volumes:
		return object.IsTrigger() && !object.IsProbeVolume();
	default:
		return false;
	}
}

bool RaptorEditor::IsObjectSelectable(const Object* object) const
{
	if (object == nullptr) {
		return false;
	}

	if (!IsDataMode()) {
		return !object->IsDataBrush();
	}

	return object->IsDataBrush() && MatchesDataFilter(*object, mDataFilter);
}

void RaptorEditor::PruneSelection()
{
	if (mbSpawnSelected && !IsSpawnPointShown()) {
		DeselectSpawn();
	}

	Object* stale[scMaxSelectedObjects];
	uint32 stale_count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (!IsObjectSelectable(object)) {
			stale[stale_count++] = object;
		}
	}

	if (stale_count == 0) {
		return;
	}

	for (uint32 i = 0; i < stale_count; i++) {
		mSelection.Remove(stale[i]);
	}

	SyncSelection();
}

bool RaptorEditor::SelectObject(Object* object, bool append_selection)
{
	if (object == nullptr) {
		ClearSelection();
		return false;
	}

	if (!IsObjectSelectable(object)) {
		return false;
	}

	DeselectSpawn();

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

void RaptorEditor::DeselectModels()
{
	Object* models[scMaxSelectedObjects];
	uint32 model_count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (!object->HasTags(eObjectTag::Blockout)) {
			models[model_count++] = object;
		}
	}

	for (uint32 i = 0; i < model_count; i++) {
		mSelection.Remove(models[i]);
	}
}

bool RaptorEditor::IsSpawnPointShown() const
{
	return IsDataMode() && (mDataFilter == eDataFilter::All || mDataFilter == eDataFilter::SpawnPoints);
}

void RaptorEditor::SelectSpawn()
{
	if (mbSpawnSelected) {
		return;
	}

	ClearSelection();

	mbSpawnSelected = true;
	mSpawnEditor.Enter();
}

void RaptorEditor::DeselectSpawn()
{
	if (!mbSpawnSelected) {
		return;
	}

	if (mbDragging && mbDraggingSpawn) {
		CancelDrag();
	}

	mbSpawnSelected = false;
	mSpawnEditor.Leave();
}

void RaptorEditor::ClearSelection()
{
	DeselectSpawn();

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
		.GroupSize = group_size,
	};

	const Brush* brush = gWorld->pBlockout->GetBrush(object);
	op.PlanesBefore = (brush != nullptr) ? brush->Planes
										 : Brush::FromBox(object->Bounds.Min, object->Bounds.Max).Planes;

	op.ObjectSnapshot = EditOperation::Snapshot::Capture(*object, mSelection.GetStoredMaterial(object));

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
		.ValueA = EditOperationValue(origin),
	});

	SelectObject(created.pObject, false);
}

static bool IsCreatableDataKind(eDataFilter filter)
{
	return filter == eDataFilter::ProbeVolumes || filter == eDataFilter::ReflectionProbes ||
		   filter == eDataFilter::Volumes;
}

Object* RaptorEditor::CreateDataBrush(const Brush::PlaneList& planes, const Vec3f position)
{
	if (gWorld->pBlockout == nullptr || !IsCreatableDataKind(mDataFilter)) {
		return nullptr;
	}

	EditOperation op {
		.Type = EditOperation::eType::CreateBrush,
	};

	op.PlanesAfter = planes;
	op.ObjectSnapshot.Position = position;
	op.ObjectSnapshot.Material = gWorld->pBlockout->GetDefaultMaterial();
	op.ObjectSnapshot.bIsProbeVolume = (mDataFilter != eDataFilter::Volumes);
	op.ObjectSnapshot.bIsReflectionProbe = (mDataFilter == eDataFilter::ReflectionProbes);
	op.ObjectSnapshot.bIsTrigger = (mDataFilter == eDataFilter::Volumes);

	Object* created = PushEditOperation(op).pObject;

	if (created != nullptr) {
		SelectObject(created, false);

		if (mDataFilter == eDataFilter::ReflectionProbes) {
			gProbeManager->RebuildReflectionProbesFromWorld();
		}
	}

	return created;
}

void RaptorEditor::CreateDataBrushAtCrosshair()
{
	if (!IsCreatableDataKind(mDataFilter)) {
		LogWarning(LC_CORE, "Pick Probe Volumes, Reflection Probes or Volumes in the Data filter to create one");
		return;
	}

	const Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera->Position,
															   camera->GetForwardVector() * scCreateRange);

	const Vec3f base = SnapToGrid(hit.bHit ? hit.Point : camera->Position);
	const Vec3f center = base + Vec3f(0.0f, scDefaultDataBrushHalfExtent.Y, 0.0f);

	Vec3f position;
	const Brush brush = gWorld->pBlockout->MakeWorldBox(center - scDefaultDataBrushHalfExtent,
														center + scDefaultDataBrushHalfExtent, position);

	if (brush.IsValid()) {
		CreateDataBrush(brush.Planes, position);
	}
}

Object* RaptorEditor::CreateReflectionProbeAtPlayer()
{
	if (gWorld->pBlockout == nullptr) {
		return nullptr;
	}

	const Vec3f center = SnapToGrid(gWorld->Player.pCamera->Position);

	Vec3f position;
	const Brush brush = gWorld->pBlockout->MakeWorldBox(center - scDefaultDataBrushHalfExtent,
														center + scDefaultDataBrushHalfExtent, position);

	if (!brush.IsValid()) {
		return nullptr;
	}

	EditOperation op {
		.Type = EditOperation::eType::CreateBrush,
	};

	op.PlanesAfter = brush.Planes;
	op.ObjectSnapshot.Position = position;
	op.ObjectSnapshot.Material = gWorld->pBlockout->GetDefaultMaterial();
	op.ObjectSnapshot.bIsProbeVolume = true;
	op.ObjectSnapshot.bIsReflectionProbe = true;

	Object* created = PushEditOperation(op).pObject;

	if (created != nullptr) {
		SelectObject(created, false);
		gProbeManager->RebuildReflectionProbesFromWorld();
	}

	return created;
}

uint32 RaptorEditor::SetSelectionReflectionProbe(bool enabled)
{
	uint32 changed = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || object->IsReflectionProbe() == enabled) {
			continue;
		}

		object->SetReflectionProbe(enabled);
		changed++;
	}

	if (changed > 0) {
		gProbeManager->RebuildReflectionProbesFromWorld();
	}

	return changed;
}

uint32 RaptorEditor::SetSelectionObjectBit(bool is_tag, uint32 bit, bool enabled)
{
	Object* targets[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr) {
			continue;
		}

		const bool can_edit = is_tag ? CanEditObjectTag(object, bit) : CanEditObjectFlag(object, bit);
		const uint32 current = is_tag ? static_cast<uint32>(object->Tags) : static_cast<uint32>(object->GetFlags());

		if (can_edit && ((current & bit) != 0) != enabled) {
			targets[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		EditOperation op {
			.Type = EditOperation::eType::ObjectStateEdit,
			.pObject = targets[i],
			.GroupSize = static_cast<int32>(count),
		};

		op.StateEdit.Bit = bit;
		op.StateEdit.bIsTag = is_tag;
		op.StateEdit.bEnabled = enabled;
		op.StateEdit.CaptureBefore(*targets[i]);

		PushEditOperation(op);
	}

	return count;
}

uint32 RaptorEditor::SetSelectionScript(const String& path)
{
	Object* targets[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object != nullptr && CanAttachScript(object) && !(gObjectScripts->GetPath(object->ID) == path)) {
			targets[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		EditOperation op {
			.Type = EditOperation::eType::ScriptEdit,
			.pObject = targets[i],
			.GroupSize = static_cast<int32>(count),
		};

		op.ScriptEdit.Before = gObjectScripts->GetPath(targets[i]->ID);
		op.ScriptEdit.After = path;

		PushEditOperation(op);
	}

	return count;
}

void RaptorEditor::DeleteSelection()
{
	// Each delete takes its object out of the selection, so work from a copy
	uint32 count = 0;

	Object* objects[scMaxSelectedObjects];

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object->HasTags(eObjectTag::Blockout)) {
			objects[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		DeleteObject(objects[i], static_cast<int32>(count));
	}
}

void RaptorEditor::DupeSelection()
{
	uint32 count = 0;

	Object* originals[scMaxSelectedObjects];

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object->HasTags(eObjectTag::Blockout)) {
			originals[count++] = object;
		}
	}

	if (count == 0) {
		return;
	}

	Object* dupes[scMaxSelectedObjects];
	uint32 dupe_count = 0;

	// The dupes are made while the originals are still selected, so they are given the originals' own materials
	// instead of the selection material
	for (uint32 i = 0; i < count; i++) {
		Object* original = originals[i];

		const EditOperationValue dupe = PushEditOperation(EditOperation {
			.Type = EditOperation::eType::Dupe,
			.pObject = original,
			.ValueA = EditOperationValue(original->GetPosition()),
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
	DeselectSpawn();

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
