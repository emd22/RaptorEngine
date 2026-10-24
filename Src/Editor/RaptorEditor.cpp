#include "RaptorEditor.hpp"

#ifdef FX_IS_EDITOR

#include "EditorFrame.hpp"
#include "EditorPlatform.hpp"
#include "EditorThread.hpp"
#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "ToolSettingsPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/image.h>
#include <wx/init.h>
#include <wx/timer.h>
#include <wx/toplevel.h>

#include <Asset/ConfigFile.hpp>
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
#include <Renderer/Exposure.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ObjectScripts.hpp>
#include <Script/Script.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <vector>


/////////////////////////////////////
// Operation includes
/////////////////////////////////////

#include "Operation/History.hpp"
#include "Operation/LightOperation.hpp"
#include "Operation/ObjectOperation.hpp"
#include "Operation/SubviewOperation.hpp"
#include "Operation/WorldOperation.hpp"


namespace fx::editor {

class EditorApp : public wxApp
{
public:
	bool OnInit() override
	{
		SetAppearance(wxApp::Appearance::Dark);
		return true;
	}
};


static constexpr uint32 scMaxEventsPerPump = 256;

static constexpr unsigned long scUILoopWaitMs = 10;

static constexpr std::chrono::milliseconds scPanelSyncInterval(100);

/// How far away a subview is placed when nothing is under the crosshair
static constexpr float32 scPlaceDistance = 4.0f;

/// How far away the crosshair can place a new subview
static constexpr float32 scCreateRange = 20.0f;

/// Size of a snap step for each snap level. Mirrors tool_get_snap_multiplier() in `tool_common.strata`.
static constexpr float32 scSnapSteps[] = { 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f };
static constexpr int32 scSnapLevelCount = static_cast<int32>(std::size(scSnapSteps));

static constexpr float32 scAngleSnapSteps[] = { 5.0f, 15.0f, 45.0f, 90.0f, 90.0f };
static_assert(std::size(scAngleSnapSteps) == std::size(scSnapSteps));

static const Vec3f scDefaultDataBrushHalfExtent(2.0f, 1.5f, 2.0f);

static constexpr uint32 scMaxSubtractOperations = 200;

static constexpr uint32 ToolBit(eEditorTool tool) { return 1u << static_cast<uint32>(tool); }

static constexpr uint32 scBrushEditTools = ToolBit(eEditorTool::Face) | ToolBit(eEditorTool::Vertex);

static constexpr uint32 scVisTools = ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Rotate) |
									 ToolBit(eEditorTool::Create) | ToolBit(eEditorTool::Arch) | ToolBit(eEditorTool::Room) |
									 ToolBit(eEditorTool::Curve) | ToolBit(eEditorTool::Decal) |
									 ToolBit(eEditorTool::Clip);

static constexpr uint32 scDataTools[static_cast<uint32>(eDataFilter::Count)] = {
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Light),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Select),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Rotate) | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Select) | ToolBit(eEditorTool::Light),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Rotate) | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Rotate) | ToolBit(eEditorTool::Create),
	ToolBit(eEditorTool::Select) | scBrushEditTools | ToolBit(eEditorTool::Rotate) | ToolBit(eEditorTool::Create),
};

static const char* const scCommandScriptPath = "./Scripts/editor/editor_cmd.strata";


/////////////////////////////////////
// GUI
/////////////////////////////////////

bool RaptorEditor::InitGUI(int argc, char** argv)
{
	thread::InitUIThread();

	wxApp::SetInstance(new EditorApp);

	if (!wxEntryStart(argc, argv)) {
		LogError(LC_CORE, "Could not initialize the wxWidgets backend for the editor");
		return false;
	}

	// For the tool icons
	wxInitAllImageHandlers();

#ifdef FX_PLATFORM_MACOS
	platform::DisableWindowTabbing();
	platform::InitRawMouse();
#endif

	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return false;
	}

	mpEventLoop = new wxGUIEventLoop;
	wxEventLoopBase::SetActive(mpEventLoop);

	thread::SetUIWakeHandler([this] { mpEventLoop->WakeUp(); });

	return true;
}

void RaptorEditor::InitTools() { mpCommandScript = gScriptManager->LoadScript(scCommandScriptPath); }

EditorFrame* RaptorEditor::CreateMainFrame(const char* title, const Vec2u& viewport_size)
{
	mpMainFrame = new EditorFrame(wxString::FromAscii(title),
								  wxSize(static_cast<int>(viewport_size.X), static_cast<int>(viewport_size.Y)));

	mpMainFrame->SetInteractive(false);

	mpMainFrame->Show();
	mpMainFrame->Raise();

#ifdef FX_PLATFORM_MACOS
	// Bring window to front
	platform::ActivateApp();
#endif

	mpMainFrame->ShowMode(mMode, mDataFilter, GetAvailableToolMask());
	mpMainFrame->GetViewport()->SetFocus();

	// Get the frame on screen and laid out before the renderer creates its surface on the viewport
	DispatchEvents(0);

	EditorViewport* viewport = mpMainFrame->GetViewport();
	viewport->UpdateCachedSize();
	viewport->ConsumeResize();

	return mpMainFrame;
}

bool RaptorEditor::IsCloseRequested() const { return mpMainFrame != nullptr && mpMainFrame->IsCloseRequested(); }


void RaptorEditor::DispatchEvents(unsigned long wait_ms)
{
	if (mpEventLoop == nullptr) {
		return;
	}

	int dispatched = mpEventLoop->DispatchTimeout(wait_ms);

	for (uint32 i = 1; i < scMaxEventsPerPump && dispatched == 1; i++) {
		dispatched = mpEventLoop->DispatchTimeout(0);
	}

	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();
}


void RaptorEditor::PumpUI(unsigned long wait_ms)
{
	DispatchEvents(wait_ms);

	if (mpMainFrame != nullptr) {
		mpMainFrame->GetViewport()->PollRelativeMouse();

#ifdef FX_PLATFORM_MACOS
		mbAppActive = platform::IsAppActive();
#else
		mbAppActive = mpMainFrame->IsActive();
#endif
	}

	thread::RunUITasks();
}

void RaptorEditor::RunUILoop()
{
	while (!mbGameFinished) {
		PumpUI(scUILoopWaitMs);
	}
}

void RaptorEditor::NotifyGameFinished()
{
	mbGameFinished = true;

	if (mpEventLoop != nullptr) {
		mpEventLoop->WakeUp();
	}
}

void RaptorEditor::LockdownGUI()
{
	if (mpMainFrame == nullptr) {
		return;
	}

	for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node != nullptr;
		 node = node->GetNext()) {
		wxWindow* window = node->GetData();

		if (window != mpMainFrame) {
			window->Hide();
		}
	}

	mpMainFrame->SetInteractive(false);
}


void RaptorEditor::BeginGameLoop()
{
	mGamePhase = eGamePhase::Running;

	thread::PostToUI([this] { mpMainFrame->SetInteractive(true); });
}

void RaptorEditor::EndGameLoop()
{
	thread::RunOnUIAndWait([this] { LockdownGUI(); });

	TopViewSetPickedFaces({});

	mGamePhase = eGamePhase::Stopped;
}

void RaptorEditor::ProcessGUIOperations()
{
	if (thread::RunGameTasks() > 0) {
		mbForcePanelSync = true;
	}
}

void RaptorEditor::ServiceViewportResize()
{
	if (mpMainFrame == nullptr) {
		return;
	}

	EditorViewport* viewport = mpMainFrame->GetViewport();

	// Rebuild once for however many size events arrived
	if (!viewport->ConsumeResize() || renderer::gGraphics == nullptr || renderer::gGraphics->GetWindow() == nullptr) {
		return;
	}

	const Vec2u size = viewport->GetCachedSize();

	if (size.X > 0 && size.Y > 0) {
		renderer::gGraphics->RebuildToResizedWindow();
	}
}

uint32 RaptorEditor::GetAvailableToolMask() const
{
	uint32 mask = 0;

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		if (IsToolAvailable(static_cast<eEditorTool>(i))) {
			mask |= (1u << i);
		}
	}

	return mask;
}

static Vec2f GetSunAngles(const Vec3f position)
{
	const float32 length = position.Length();
	if (length < 1e-6f) {
		return Vec2f::scZero;
	}

	const Vec3f direction = position / length;

	return Vec2f(MathUtil::RadiansToDegrees(std::atan2(direction.X, direction.Z)),
				 MathUtil::RadiansToDegrees(std::asin(std::clamp(direction.Y, -1.0f, 1.0f))));
}

EditorPanelState RaptorEditor::BuildPanelState()
{
	EditorPanelState state;

	if (gWorld->pPrototype != nullptr) {
		const MaterialLibrary& library = gWorld->pPrototype->GetMaterialLibrary();

		for (uint32 id = 0; id < library.GetCount(); id++) {
			state.Materials.Names.Insert(library.GetName(MaterialLibraryID(id)));
			state.Materials.DiffusePaths.Insert(library.GetDiffusePath(MaterialLibraryID(id)));
		}
	}

	state.BlockoutPath = gWorld->PrototypePath;
	state.bUnsavedChanges = HasUnsavedChanges();
	state.bIsCompiledMap = IsCompiledMapOpen();

	if (gWorld->pPrototype != nullptr) {
		state.ActiveSubview = gWorld->pPrototype->GetSubviewName(gWorld->pPrototype->GetActiveSubview());
		state.SubviewNames = gWorld->pPrototype->GetSubviewNames();

		state.Object.ActiveSubview = state.ActiveSubview;
	}

	if (Object* object = mSelection.GetLast(); object != nullptr) {
		ObjectPanelState& panel = state.Object;

		panel.bHasObject = true;
		panel.bIsBlockout = object->HasTags(eObjectTag::Blockout);
		panel.Name = object->Name.Get();
		panel.SelectedCount = mSelection.GetCount();
		panel.Tags = object->Tags;
		panel.Flags = object->GetFlags();
		panel.EditableTags = GetEditableTags(object);
		panel.EditableFlags = GetEditableFlags(object);

		if (!CanEditObject(object) && gWorld->pPrototype != nullptr) {
			panel.SubviewName = gWorld->pPrototype->GetSubviewName(object->SubviewInstance);
			panel.SubviewBrushCount = gWorld->pPrototype->GetSubviewBrushCount(object->SubviewInstance);
		}

		if (gWorld->pPrototype != nullptr) {
			panel.MaterialSlot = gWorld->pPrototype->GetIDForMaterial(object->GetMaterialID()).ID;
		}

		panel.bCanRename = CanRenameObject(object);
		panel.bCanAttachScript = CanAttachScript(object);

		if (panel.bCanAttachScript) {
			panel.bScriptErrors = gObjectScripts->HasErrors(object->ID);
			panel.ScriptPath = gObjectScripts->GetPath(object->ID);
		}

		panel.bIsTrigger = object->IsTrigger();

		Vec3f enter_direction;

		if (panel.bIsTrigger && gObjectScripts->TryGetRequiredEnterDirection(object->ID, enter_direction)) {
			panel.bHasEnterDirection = true;
			panel.EnterDirection = enter_direction;
		}
	}

	WorldPanelState& world = state.World;

	ExposureSettings exposure;

	world.Aperture = gCVars->Get("r_aperture", exposure.Aperture);
	world.ShutterTime = gCVars->Get("r_shutter", exposure.ShutterTime);
	world.ISO = gCVars->Get("r_iso", exposure.ISO);
	world.Compensation = gCVars->Get("r_exposure_ev", exposure.Compensation);

	world.DebugMask = gCVars->Get("r_debug_bounds", int64 { 0 });
	world.bReflectionsEnabled = gCVars->Get("r_reflection_probes", int64 { 1 }) != 0;
	world.bReflectionFallback = gCVars->Get("r_reflection_level_probe", int64 { 0 }) != 0;
	world.ReflectionDebugView = std::clamp<int64>(gCVars->Get("r_reflection_debug", int64 { 0 }), 0, 2);

	if (LightDirectional* sun = gLightManager->GetDirectionalLight(); sun != nullptr) {
		const Vec3f position = sun->GetPosition();
		const Vec2f angles = GetSunAngles(position);

		world.bHasSun = true;
		world.bSunEnabled = gCVars->Get("b_sun_enabled", true);
		world.SunColor = sun->Color;
		world.SunIntensity = sun->Intensity;
		world.SunAzimuthDegrees = angles.X;
		world.SunElevationDegrees = angles.Y;

		world.bSunBakeStale = gWorld->pPrototype != nullptr && gWorld->pPrototype->HasBakedSun() &&
							  !gWorld->pPrototype->IsBakedSunCurrent(position);
	}

	const uint32 probe_count = gProbeManager->GetReflectionProbeCount();

	if (gProbeManager->IsBakingReflections()) {
		world.ReflectionStatus = String::Fmt("Baking {} probe(s)...", probe_count);
	}
	else if (gProbeManager->IsBaking()) {
		world.ReflectionStatus = "Baking irradiance...";
	}
	else if (probe_count == 0) {
		world.ReflectionStatus = "No reflection probes";
	}
	else if (gProbeManager->AreReflectionsBaked()) {
		world.ReflectionStatus = String::Fmt("{} probe(s), baked", probe_count);
	}
	else {
		world.ReflectionStatus = String::Fmt("{} probe(s), needs a bake", probe_count);
	}

	if (LightSpot* light = mLightEditor.GetSelected(); light != nullptr) {
		LightPanelState& panel = state.Light;

		panel.bHasLight = true;
		panel.Name = light->Name.Get();
		panel.Color = light->Color;
		panel.Position = light->GetPosition();
		panel.Radius = light->GetRadius();
		panel.Intensity = light->Intensity;
		panel.Lumens = light->GetLumens();
		panel.OuterAngleDegrees = MathUtil::RadiansToDegrees(light->GetOuterAngle());
		panel.InnerAngleDegrees = MathUtil::RadiansToDegrees(light->GetInnerAngle());
	}

	if (IsTopViewActive()) {
		state.TopView = BuildTopViewState();
	}

	return state;
}

void RaptorEditor::SyncPanels()
{
	if (mpMainFrame == nullptr || mGamePhase != eGamePhase::Running) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();

	if (!mbForcePanelSync && (now - mLastPanelSync) < scPanelSyncInterval) {
		return;
	}

	mLastPanelSync = now;

	EditorPanelState state = BuildPanelState();

	if (!mbForcePanelSync && state == mLastSentState) {
		return;
	}

	mbForcePanelSync = false;
	mLastSentState = state;

	bool needs_post = false;

	{
		std::lock_guard lock(mPendingStateMutex);

		mPendingState = std::move(state);
		needs_post = !mbStatePosted;
		mbStatePosted = true;
	}

	if (!needs_post) {
		return;
	}

	thread::PostToUI(
		[this]
		{
			EditorPanelState latest;

			{
				std::lock_guard lock(mPendingStateMutex);

				latest = std::move(mPendingState);
				mbStatePosted = false;
			}

			if (mpMainFrame != nullptr) {
				mpMainFrame->ApplyState(latest);
			}
		});
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
	UpdateCompiledMapViewLock();

	if (gWorld->pPrototype != nullptr) {
		gWorld->pPrototype->SyncSubviewInstances();

		const uint64 revision = mHistory.GetRevision();

		if (revision != mDecalRevision || mUntrackedEditCount != mDecalUntrackedEditCount) {
			mDecalRevision = revision;
			mDecalUntrackedEditCount = mUntrackedEditCount;

			gWorld->pPrototype->RefreshDecals();
			gWorld->pPrototype->RefreshCappedPatches();
		}
	}

	if (mbSimulating) {
		mGrabEditor.Update(delta_time);
		return;
	}

	PruneSelection();

	if (mCurrentTool == eEditorTool::Light) {
		mLightEditor.DrawMarkers();
	}

	if (IsSpawnPointShown()) {
		mSpawnEditor.DrawMarker(mbSpawnSelected);
	}

	DrawSelectionOverlay();
	DrawPickedFaces();
	DrawModelSelection();
	DrawSubviews();
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

static void DrawOverlay(Object* object, const Color& color)
{
	if (object->IsHidden()) {
		return;
	}

	if (object->pMesh.IsValid()) {
		renderer::gDebugDraw->Overlay(object->pMesh, object->GetWorldMatrix(), color);
	}

	for (ObjectID attached_id : object->AttachedNodes) {
		Object* attached = gObjectManager->GetObject(attached_id);

		if (attached != nullptr) {
			DrawOverlay(attached, color);
		}
	}
}

void RaptorEditor::DrawSelectionOverlay()
{
	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		DrawOverlay(mSelection.GetObject(i), mTheme.Viewport.Selection);
	}
}

void RaptorEditor::DrawModelSelection()
{
	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (!object->HasTags(eObjectTag::Blockout)) {
			DrawModelBounds(object, mTheme.Viewport.ModelSelected);
		}
	}
}

/////////////////////////////////////
// Tools
/////////////////////////////////////

void RaptorEditor::SetTool(eEditorTool tool)
{
	if (tool >= eEditorTool::Count || !IsToolAvailable(tool)) {
		ShowTool();
		return;
	}

	if (tool != mCurrentTool) {
		const bool was_light = (mCurrentTool == eEditorTool::Light);
		const bool is_light = (tool == eEditorTool::Light);

		mCurrentTool = tool;

		if (was_light) {
			mLightEditor.Deselect();
		}

		if (tool != eEditorTool::Select) {
			DeselectSpawn();
		}

		PruneSelection();

		if (was_light != is_light && mpMainFrame != nullptr) {
			thread::PostToUI(
				[this, is_light]
				{
					ToolSettingsBasePanel* panel = nullptr;

					if (is_light) {
						panel = new LightToolSettingsPanel();
					}

					mpMainFrame->SetToolSettingsPanel(panel);
				});
		}

		mbForcePanelSync = true;
	}

	ShowTool();
}

void RaptorEditor::ShowTool()
{
	if (mpMainFrame != nullptr) {
		thread::PostToUI([this, tool = mCurrentTool] { mpMainFrame->ShowSelectedTool(tool); });
	}
}

void RaptorEditor::SetSimulating(bool simulating)
{
	if (simulating != mbSimulating) {
		mbSimulating = simulating;

		if (simulating) {
			ClearSelection();
			mLightEditor.Deselect();

			if (mView == eEditorView::Top) {
				SetView(eEditorView::Perspective);
			}
		}
		else {
			mGrabEditor.Release();
		}

		mbForcePanelSync = true;
	}

	if (mpMainFrame != nullptr) {
		thread::PostToUI([this, shown = mbSimulating] { mpMainFrame->ShowSimulating(shown); });
	}
}

bool RaptorEditor::IsToolAvailable(eEditorTool tool) const
{
	const uint32 available = IsDataMode() ? scDataTools[static_cast<uint32>(mDataFilter)] : scVisTools;

	return (available & ToolBit(tool)) != 0;
}

void RaptorEditor::EnsureToolAvailable()
{
	if (!IsToolAvailable(mCurrentTool)) {
		SetTool(eEditorTool::Select);
	}
}

void RaptorEditor::ShowMode()
{
	if (mpMainFrame != nullptr) {
		thread::PostToUI([this, mode = mMode, filter = mDataFilter, mask = GetAvailableToolMask()]
						 { mpMainFrame->ShowMode(mode, filter, mask); });
	}
}

void RaptorEditor::SetMode(eEditorMode mode)
{
	if (mode != mMode) {
		ClearSelection();

		mMode = mode;

		EnsureToolAvailable();
	}

	ShowMode();
}

void RaptorEditor::SetDataFilter(eDataFilter filter)
{
	if (filter != mDataFilter) {
		mDataFilter = filter;

		PruneSelection();
		EnsureToolAvailable();
	}

	ShowMode();
}

void RaptorEditor::ReloadScripts() { gScriptManager->ReloadAllScripts(); }

void RaptorEditor::SubmitToolState(const EditorToolState& state)
{
	// The selected tool and the transform marker belong to the editor, so only the snap settings are taken
	mToolState.ToolSnapLevel = std::clamp(state.ToolSnapLevel, 0, scSnapLevelCount - 1);
	mToolState.ToolSnapEnabled = state.ToolSnapEnabled;
}

float32 RaptorEditor::GetSnapStep() const
{
	return scSnapSteps[std::clamp(mToolState.ToolSnapLevel, 0, scSnapLevelCount - 1)];
}

void RaptorEditor::AdjustSnapLevel(int32 direction)
{
	mToolState.ToolSnapLevel = std::clamp(mToolState.ToolSnapLevel + direction, 0, scSnapLevelCount - 1);
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

	return RoundToGrid(position);
}

Vec3f RaptorEditor::RoundToGrid(const Vec3f position) const
{
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
	case eDataFilter::NvBrushes:
		return object.GetSpecialMaterial() == eSpecialMaterial::NvBrush;
	case eDataFilter::DBounds:
		return object.GetSpecialMaterial() == eSpecialMaterial::DBound;
	case eDataFilter::Portals:
		return object.GetSpecialMaterial() == eSpecialMaterial::Portal;
	default:
		return false;
	}
}

bool RaptorEditor::IsObjectSelectable(const Object* object) const
{
	if (object == nullptr) {
		return false;
	}

	if (mCurrentTool == eEditorTool::Light) {
		return false;
	}

	if (!CanEditObject(object)) {
		// A placed subview is one object, which only the Select and Rotate tools can work with. Nothing outside
		// of the subview being edited can be selected.
		return gWorld->pPrototype != nullptr && gWorld->pPrototype->IsPlacedInActiveSubview(object->SubviewInstance) &&
			   (mCurrentTool == eEditorTool::Select || mCurrentTool == eEditorTool::Rotate);
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

	if (!CanEditObject(object)) {
		return SelectSubview(object->SubviewInstance, append_selection);
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
		Warn("Cannot select '{}', the selection is full", object->Name.Get());
	}

	SyncSelection();

	return selected;
}

bool RaptorEditor::SelectSubview(uint32 instance, bool append_selection)
{
	Object* root = gWorld->pPrototype->GetSubviewObject(instance);

	if (root == nullptr) {
		return false;
	}

	if (mSelection.Contains(root)) {
		return true;
	}

	if (!append_selection) {
		mSelection.Clear();
	}

	const bool selected = mSelection.Add(root);

	if (!selected) {
		Warn("Cannot select '{}', the selection is full", root->Name.Get());
	}

	SyncSelection();

	return selected;
}

void RaptorEditor::DeselectObject(Object* object)
{
	if (object == nullptr) {
		return;
	}

	if (CanEditObject(object)) {
		mSelection.Remove(object);
	}
	else {
		mSelection.Remove(gWorld->pPrototype->GetSubviewObject(object->SubviewInstance));
	}

	SyncSelection();
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
	if (mbSpawnSelected || !IsSpawnPointShown() || mCurrentTool != eEditorTool::Select) {
		return;
	}

	ClearSelection();

	mbSpawnSelected = true;
}

void RaptorEditor::DeselectSpawn() { mbSpawnSelected = false; }

void RaptorEditor::ClearSelection()
{
	DeselectSpawn();

	if (mSelection.IsEmpty()) {
		return;
	}

	mSelection.Clear();
	SyncSelection();
}

void RaptorEditor::SetObjectMaterial(Object* object, MaterialID material)
{
	if (!CanEditObject(object) || !object->HasTags(eObjectTag::Blockout)) {
		return;
	}

	object->SetMaterial(material);

	if (object->IsDecal()) {
		mLastDecalMaterial = material;
	}
	else {
		mLastUsedMaterial = material;
	}
}

MaterialID RaptorEditor::GetNewDecalMaterial() const
{
	if (gWorld->pPrototype == nullptr) {
		return MaterialID::scNull;
	}

	if (!mLastDecalMaterial.IsNull() && gWorld->pPrototype->GetIDForMaterial(mLastDecalMaterial).IsValid()) {
		return mLastDecalMaterial;
	}

	return gWorld->pPrototype->GetDefaultMaterial();
}

MaterialID RaptorEditor::GetNewBrushMaterial() const
{
	if (gWorld->pPrototype == nullptr) {
		return MaterialID::scNull;
	}

	if (!mLastUsedMaterial.IsNull() && gWorld->pPrototype->GetIDForMaterial(mLastUsedMaterial).IsValid()) {
		return mLastUsedMaterial;
	}

	return gWorld->pPrototype->GetDefaultMaterial();
}


/////////////////////////////////////
// Edit operations
/////////////////////////////////////

EditOperation& RaptorEditor::PushEditOperation(std::unique_ptr<EditOperation> op)
{
	EditOperation& pushed = mHistory.Push(std::move(op));

	// Deleting an object takes it out of the selection
	if (pushed.ChangesObjects()) {
		SyncSelection();
	}

	return pushed;
}

void RaptorEditor::DeleteObject(Object* object, int32 group_size)
{
	if (object == nullptr) {
		return;
	}

	// Snapshot everything Undo needs before the Execute step destroys the object.
	const Brush* brush = gWorld->pPrototype->GetBrush(object);
	const Brush::PlaneList planes = (brush != nullptr) ? brush->Planes
													   : Brush::FromBox(object->Bounds.Min, object->Bounds.Max).Planes;

	EmplaceEditOperation<DeleteOperation>(
		object, planes, ObjectSnapshot::Capture(*object, object->GetMaterialID()), group_size);
}

static bool IsCreatableDataKind(eDataFilter filter)
{
	return filter == eDataFilter::ProbeVolumes || filter == eDataFilter::ReflectionProbes ||
		   filter == eDataFilter::Volumes || filter == eDataFilter::NvBrushes || filter == eDataFilter::DBounds ||
		   filter == eDataFilter::Portals;
}

Object* RaptorEditor::CreateDataBrush(const Brush::PlaneList& planes, const Vec3f position)
{
	if (gWorld->pPrototype == nullptr || !IsCreatableDataKind(mDataFilter)) {
		return nullptr;
	}

	ObjectSnapshot snapshot;
	snapshot.Position = position;
	snapshot.Material = gWorld->pPrototype->GetDefaultMaterial();
	snapshot.bIsProbeVolume = (mDataFilter == eDataFilter::ProbeVolumes ||
							   mDataFilter == eDataFilter::ReflectionProbes);
	snapshot.bIsReflectionProbe = (mDataFilter == eDataFilter::ReflectionProbes);
	snapshot.bIsTrigger = (mDataFilter == eDataFilter::Volumes);

	if (mDataFilter == eDataFilter::NvBrushes) {
		snapshot.Material = gWorld->pPrototype->GetSpecialMaterial(eSpecialMaterial::NvBrush);
	}
	else if (mDataFilter == eDataFilter::DBounds) {
		snapshot.Material = gWorld->pPrototype->GetSpecialMaterial(eSpecialMaterial::DBound);
	}
	else if (mDataFilter == eDataFilter::Portals) {
		snapshot.Material = gWorld->pPrototype->GetSpecialMaterial(eSpecialMaterial::Portal);
	}

	Object* created = EmplaceEditOperation<CreateBrushOperation>(planes, snapshot).GetCreated();

	if (created != nullptr) {
		SelectObject(created, false);

		if (mDataFilter == eDataFilter::ReflectionProbes) {
			gProbeManager->RebuildReflectionProbesFromWorld();
		}
	}

	return created;
}

void RaptorEditor::SetSunEnabled(bool enabled)
{
	gCVars->Set("b_sun_enabled", enabled ? 1 : 0);

	if (LightDirectional* sun = gLightManager->GetDirectionalLight(); sun != nullptr) {
		sun->bEnabled = enabled;
	}

	MarkModified();
}

void RaptorEditor::SetSunColor(Color color)
{
	LightDirectional* sun = gLightManager->GetDirectionalLight();
	if (sun == nullptr) {
		return;
	}

	sun->Color = Color::FromRGBA(color.R, color.G, color.B, 255);
	MarkModified();
}

void RaptorEditor::SetSunIntensity(float32 intensity)
{
	LightDirectional* sun = gLightManager->GetDirectionalLight();
	if (sun == nullptr) {
		return;
	}

	sun->Intensity = std::max(intensity, 0.0f);
	MarkModified();
}

void RaptorEditor::SetSunAngles(float32 azimuth_degrees, float32 elevation_degrees)
{
	LightDirectional* sun = gLightManager->GetDirectionalLight();
	if (sun == nullptr) {
		return;
	}

	float32 azimuth_sin, azimuth_cos;
	float32 elevation_sin, elevation_cos;

	MathUtil::SinCos(MathUtil::DegreesToRadians(azimuth_degrees), &azimuth_sin, &azimuth_cos);
	MathUtil::SinCos(MathUtil::DegreesToRadians(std::clamp(elevation_degrees, -90.0f, 90.0f)), &elevation_sin,
					 &elevation_cos);

	float32 length = sun->GetPosition().Length();
	if (length < 1e-6f) {
		length = 1.0f;
	}

	const Vec3f direction(elevation_cos * azimuth_sin, elevation_sin, elevation_cos * azimuth_cos);

	sun->SetPosition(direction * length);
	MarkModified();
}

void RaptorEditor::SetSunAzimuth(float32 azimuth_degrees)
{
	LightDirectional* sun = gLightManager->GetDirectionalLight();
	if (sun == nullptr) {
		return;
	}

	SetSunAngles(azimuth_degrees, GetSunAngles(sun->GetPosition()).Y);
}

void RaptorEditor::SetSunElevation(float32 elevation_degrees)
{
	LightDirectional* sun = gLightManager->GetDirectionalLight();
	if (sun == nullptr) {
		return;
	}

	SetSunAngles(GetSunAngles(sun->GetPosition()).X, elevation_degrees);
}

Object* RaptorEditor::CreateReflectionProbeAtPlayer()
{
	if (gWorld->pPrototype == nullptr) {
		return nullptr;
	}

	const Vec3f center = SnapToGrid(gWorld->Player.pCamera->Position);

	Vec3f position;
	const Brush brush = gWorld->pPrototype->MakeWorldBox(center - scDefaultDataBrushHalfExtent,
														 center + scDefaultDataBrushHalfExtent, position);

	if (!brush.IsValid()) {
		return nullptr;
	}

	ObjectSnapshot snapshot;
	snapshot.Position = position;
	snapshot.Material = gWorld->pPrototype->GetDefaultMaterial();
	snapshot.bIsProbeVolume = true;
	snapshot.bIsReflectionProbe = true;

	Object* created = EmplaceEditOperation<CreateBrushOperation>(brush.Planes, snapshot).GetCreated();

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

		if (object == nullptr || !CanEditObject(object) || object->IsReflectionProbe() == enabled) {
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

static bool CanEditObjectState(const Object* object, eObjectTag tag) { return CanEditObjectTag(object, tag); }
static bool CanEditObjectState(const Object* object, eObjectFlags flag) { return CanEditObjectFlag(object, flag); }

static bool HasObjectState(const Object* object, eObjectTag tag) { return object->HasTags(tag); }
static bool HasObjectState(const Object* object, eObjectFlags flag) { return HasFlag(object->GetFlags(), flag); }

template <typename TState>
uint32 RaptorEditor::SetSelectionObjectStateImpl(TState state, bool enabled)
{
	Object* targets[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || !CanEditObject(object)) {
			continue;
		}

		if (CanEditObjectState(object, state) && HasObjectState(object, state) != enabled) {
			targets[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		EmplaceEditOperation<ObjectStateEditOperation>(targets[i], state, enabled, static_cast<int32>(count));
	}

	return count;
}

uint32 RaptorEditor::SetSelectionObjectState(eObjectTag tag, bool enabled)
{
	return SetSelectionObjectStateImpl(tag, enabled);
}

uint32 RaptorEditor::SetSelectionObjectState(eObjectFlags flag, bool enabled)
{
	return SetSelectionObjectStateImpl(flag, enabled);
}

uint32 RaptorEditor::SetSelectionScript(const String& path)
{
	Object* targets[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object != nullptr && CanEditObject(object) && CanAttachScript(object) &&
			!(gObjectScripts->GetPath(object->ID) == path)) {
			targets[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		EmplaceEditOperation<ScriptEditOperation>(targets[i], gObjectScripts->GetPath(targets[i]->ID), path,
												  static_cast<int32>(count));
	}

	return count;
}

uint32 RaptorEditor::SetSelectionEnterDirection(const Vec3f local_direction)
{
	static constexpr float32 scMinDirectionLength = 1e-4f;
	static constexpr float32 scSameDirectionDistance = 1e-5f;

	const Vec3f wanted = (local_direction.Length() > scMinDirectionLength) ? local_direction.Normalize() : Vec3f::sZero;

	Object* targets[scMaxSelectedObjects];
	Vec3f befores[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || !CanEditObject(object) || !object->IsTrigger()) {
			continue;
		}

		Vec3f current = Vec3f::sZero;
		gObjectScripts->TryGetRequiredEnterDirection(object->ID, current);

		if ((current - wanted).Length() < scSameDirectionDistance) {
			continue;
		}

		befores[count] = current;
		targets[count++] = object;
	}

	for (uint32 i = 0; i < count; i++) {
		EmplaceEditOperation<TriggerDirectionEditOperation>(targets[i], befores[i], wanted, static_cast<int32>(count));
	}

	return count;
}

bool RaptorEditor::CanRenameObject(const Object* object) const
{
	return CanEditObject(object) && object->SubviewInstance == Blockout::scNoSubview;
}

bool RaptorEditor::RenameObject(Object* object, const String& name)
{
	if (!CanRenameObject(object)) {
		return false;
	}

	if (name.IsEmpty()) {
		Warn("Objects need a name");
		return false;
	}

	const String valid_name = ConfigFile::MakeValidName(name);

	if (valid_name == object->Name.Get()) {
		return false;
	}

	const Object* existing = gObjectManager->FindObject(HashStr32(valid_name.CStr()));

	if (existing != nullptr && existing != object) {
		Warn("There is already an object named '{}'", valid_name);
		return false;
	}

	EmplaceEditOperation<RenameEditOperation>(object, object->Name.Get(), valid_name);

	return true;
}

void RaptorEditor::RenameSelection(const String& name)
{
	if (IsSimulationMode()) {
		return;
	}

	const String base_name = ConfigFile::MakeValidName(name);

	if (base_name.IsEmpty()) {
		Warn("Objects need a name");
		return;
	}

	struct Rename
	{
		Object* pObject = nullptr;
		String Before;
		String After;
	};

	DynArray<Rename> renames;
	uint32 suffix = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (!CanRenameObject(object)) {
			continue;
		}

		String candidate;
		bool is_taken = true;

		while (is_taken) {
			candidate = base_name;

			if (suffix > 0) {
				candidate = String::Fmt("{}_{}", base_name, suffix);
			}

			suffix++;

			const Object* existing = gObjectManager->FindObject(HashStr32(candidate.CStr()));
			is_taken = (existing != nullptr && existing != object);
		}

		if (!(candidate == object->Name.Get())) {
			renames.Insert(Rename { object, object->Name.Get(), candidate });
		}
	}

	for (const Rename& rename : renames) {
		EmplaceEditOperation<RenameEditOperation>(rename.pObject, rename.Before, rename.After,
												  static_cast<int32>(renames.Size));
	}
}

void RaptorEditor::DeleteSelection()
{
	// Each delete takes its object out of the selection, so work from a copy
	uint32 count = 0;

	Object* objects[scMaxSelectedObjects];

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object->HasTags(eObjectTag::Blockout) || gWorld->pPrototype->IsSubviewObject(object)) {
			objects[count++] = object;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		if (gWorld->pPrototype->IsSubviewObject(objects[i])) {
			EmplaceEditOperation<SubviewRemoveOperation>(objects[i]->SubviewInstance, static_cast<int32>(count));
		}
		else {
			DeleteObject(objects[i], static_cast<int32>(count));
		}
	}
}

void RaptorEditor::DupeSelection()
{
	uint32 count = 0;

	Object* originals[scMaxSelectedObjects];

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object->HasTags(eObjectTag::Blockout) || gWorld->pPrototype->IsSubviewObject(object)) {
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

		Object* dupe = EmplaceEditOperation<DupeOperation>(original, original->GetPosition(), static_cast<int32>(count),
														   AllocateDupeSubview(original))
						   .GetDupe();

		if (dupe != nullptr) {
			dupes[dupe_count++] = dupe;
		}
	}

	// Carry on working with the dupes
	mSelection.Clear();

	for (uint32 i = 0; i < dupe_count; i++) {
		mSelection.Add(dupes[i]);
	}

	SyncSelection();
}

bool RaptorEditor::ClipObject(Object* object, const Vec3f point_a, const Vec3f point_b, const Vec3f direction)
{
	Blockout* blockout = gWorld->pPrototype;

	if (object == nullptr || blockout == nullptr || IsSimulationMode() || !CanEditObject(object)) {
		return false;
	}

	const Brush* brush = blockout->GetBrush(object);

	if (brush == nullptr) {
		return false;
	}

	const Brush::PlaneList planes_before = brush->Planes;
	Brush::PlaneList kept_planes;
	Brush::PlaneList split_planes;

	ObjectSnapshot split_snapshot = ObjectSnapshot::Capture(*object, object->GetMaterialID());
	split_snapshot.ObjectName = Name();

	if (!blockout->GetClipPieces(object, point_a, point_b, direction, kept_planes, split_planes,
								 split_snapshot.Position)) {
		return false;
	}

	EmplaceEditOperation<BrushEditOperation>(object, planes_before, kept_planes, 2);
	EmplaceEditOperation<CreateBrushOperation>(split_planes, split_snapshot, 2);

	return true;
}

static bool CanBeSubtractedFrom(const Object* object)
{
	return !object->IsVolume() && !object->IsReflectionProbe() && !object->IsSpawn() && !object->IsDecal() &&
		   !gWorld->pPrototype->IsDynamic(object);
}

static uint32 SubtractCutter(Object* cutter, const std::vector<ObjectID>& cutter_ids, bool keep_cutter)
{
	const EditorSelection& selection = gEditor->GetSelection();

	std::vector<std::unique_ptr<ObjectEditOperation>> ops;
	std::vector<ObjectID> op_object_ids;
	uint32 cut_count = 0;

	std::vector<ObjectID> target_ids;

	for (ObjectID id : gWorld->pPrototype->BlockoutObjects) {
		const bool is_cutter = std::any_of(cutter_ids.begin(), cutter_ids.end(),
										   [&](ObjectID other) { return other.GetID() == id.GetID(); });

		if (!is_cutter) {
			target_ids.push_back(id);
		}
	}

	for (ObjectID id : target_ids) {
		Object* target = gObjectManager->GetObject(id);

		if (target == nullptr || !gEditor->CanEditObject(target) || !CanBeSubtractedFrom(target)) {
			continue;
		}

		std::vector<Brush::PlaneList> pieces;
		std::vector<Vec3f> positions;

		if (!gWorld->pPrototype->GetSubtractPieces(target, cutter, pieces, positions)) {
			continue;
		}

		const Brush* brush = gWorld->pPrototype->GetBrush(target);
		const MaterialID material = target->GetMaterialID();

		cut_count++;

		if (pieces.empty()) {
			ops.push_back(
				std::make_unique<DeleteOperation>(target, brush->Planes, ObjectSnapshot::Capture(*target, material)));
			op_object_ids.push_back(id);

			continue;
		}

		ops.push_back(std::make_unique<BrushEditOperation>(target, brush->Planes, pieces[0]));
		op_object_ids.push_back(id);

		for (uint32 i = 1; i < pieces.size(); i++) {
			ObjectSnapshot snapshot = ObjectSnapshot::Capture(*target, material);
			snapshot.ObjectName = Name();
			snapshot.Position = positions[i];

			ops.push_back(std::make_unique<CreateBrushOperation>(pieces[i], snapshot));
			op_object_ids.push_back(ObjectID::scNull);
		}
	}

	if (cut_count == 0) {
		return 0;
	}

	if (!keep_cutter) {
		const Brush* cutter_brush = gWorld->pPrototype->GetBrush(cutter);

		ops.push_back(std::make_unique<DeleteOperation>(
			cutter, cutter_brush->Planes, ObjectSnapshot::Capture(*cutter, cutter->GetMaterialID())));
		op_object_ids.push_back(cutter->ID);
	}

	if (ops.size() > scMaxSubtractOperations) {
		gEditor->Warn("Subtracting '{}' would change too many blockouts at once", cutter->Name.Get());
		return 0;
	}

	for (uint32 i = 0; i < ops.size(); i++) {
		ops[i]->GroupSize = static_cast<int32>(ops.size());

		if (!op_object_ids[i].IsInvalid()) {
			ops[i]->Retarget(gObjectManager->GetObject(op_object_ids[i]));
		}

		gEditor->PushEditOperation(std::move(ops[i]));
	}

	return cut_count;
}

uint32 RaptorEditor::SubtractSelection(bool keep_cutters)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || IsSimulationMode()) {
		return 0;
	}

	std::vector<ObjectID> cutter_ids;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (blockout->GetBrush(object) != nullptr && !object->IsDecal()) {
			cutter_ids.push_back(object->ID);
		}
	}

	uint32 cut_count = 0;

	for (ObjectID id : cutter_ids) {
		Object* cutter = gObjectManager->GetObject(id);

		if (cutter != nullptr && blockout->GetBrush(cutter) != nullptr) {
			cut_count += SubtractCutter(cutter, cutter_ids, keep_cutters);
		}
	}

	return cut_count;
}

/////////////////////////////////////
// Subviews
/////////////////////////////////////

bool RaptorEditor::IsEditingSubview() const
{
	return gWorld->pPrototype != nullptr && gWorld->pPrototype->IsEditingSubview();
}

bool RaptorEditor::CanEditObject(const Object* object) const
{
	if (object == nullptr || gWorld->pPrototype == nullptr) {
		return false;
	}

	return object->SubviewInstance == gWorld->pPrototype->GetActiveSubview();
}

uint32 RaptorEditor::AllocateDupeSubview(const Object* original)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || !blockout->IsSubviewObject(original)) {
		return Blockout::scNoSubview;
	}

	return blockout->NewSubviewInstance(blockout->GetSubviewName(original->SubviewInstance));
}

bool RaptorEditor::CreateSubviewFromSelection(const String& name)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || IsSimulationMode()) {
		return false;
	}

	if (blockout->IsCompiledMap()) {
		Warn("Open the prototype (.prx) to make subviews, a compiled map can't be edited");
		return false;
	}

	Object* objects[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (blockout->IsSubviewObject(object) && blockout->IsPlacedInActiveSubview(object->SubviewInstance)) {
			objects[count++] = object;
			continue;
		}

		if (!blockout->HasBrush(object) && !blockout->HasPatch(object)) {
			Warn("Only brushes, patches and subviews can be part of a subview, '{}' is none of them",
				 object->Name.Get());
			return false;
		}

		if (!CanEditObject(object)) {
			Warn("'{}' is already part of the subview '{}'", object->Name.Get(),
				 blockout->GetSubviewName(object->SubviewInstance));
			return false;
		}

		objects[count++] = object;
	}

	if (count == 0) {
		Warn("Select the brushes to make a subview from first");
		return false;
	}

	BBox bounds;

	if (!blockout->GetBrushBounds(objects, count, bounds)) {
		return false;
	}

	BrushPlacement placement;
	placement.Position = SnapToGrid(
		Vec3f((bounds.Min.X + bounds.Max.X) * 0.5f, bounds.Min.Y, (bounds.Min.Z + bounds.Max.Z) * 0.5f));

	String wanted_name = name;

	if (wanted_name.IsEmpty()) {
		wanted_name = "subview";
	}

	const uint32 instance = blockout->CreateSubview(wanted_name, objects, count, placement.Position);

	if (instance == Blockout::scNoSubview) {
		return false;
	}

	EmplaceEditOperation<SubviewCreateOperation>(instance, objects, count, placement);

	LogInfo(LC_CORE, "Made the subview '{}' from {} object(s)", blockout->GetSubviewName(instance), count);

	SetTool(eEditorTool::Select);
	SelectSubview(instance, false);

	mbForcePanelSync = true;

	return true;
}

bool RaptorEditor::IsSubviewSelected() const
{
	const Blockout* blockout = gWorld->pPrototype;
	const Object* selected = mSelection.GetLast();

	return blockout != nullptr && selected != nullptr && selected->SubviewInstance != Blockout::scNoSubview &&
		   blockout->IsPlacedInActiveSubview(selected->SubviewInstance);
}

bool RaptorEditor::EnterSubview()
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || IsSimulationMode()) {
		return false;
	}

	if (!IsSubviewSelected()) {
		Warn("Select a placed subview to enter it");
		return false;
	}

	const Object* selected = mSelection.GetLast();

	const uint32 instance = selected->SubviewInstance;

	// Edits made from here on are to the subview, so they can't be mixed with undoing edits to the level
	ForgetObjects();

	if (!blockout->EnterSubview(instance)) {
		Warn("Could not enter the subview '{}'", blockout->GetSubviewName(instance));
		return false;
	}

	LogInfo(LC_CORE, "Editing the subview '{}'", blockout->GetSubviewName(instance));

	mbForcePanelSync = true;

	return true;
}

void RaptorEditor::LeaveSubview()
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || !blockout->IsEditingSubview()) {
		return;
	}

	// The other copies of the subview are rebuilt, which the history can't follow
	ForgetObjects();

	blockout->LeaveSubview();

	MarkModified();

	mbForcePanelSync = true;
}

bool RaptorEditor::PlaceSubview(const String& name)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || IsSimulationMode()) {
		return false;
	}

	const uint32 instance = blockout->NewSubviewInstance(name);

	if (instance == Blockout::scNoSubview) {
		Warn("The subview '{}' could not be placed", name);
		return false;
	}

	const Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera->Position,
															   camera->GetForwardVector() * scCreateRange);

	BrushPlacement placement;

	if (hit.bHit) {
		placement.Position = SnapToGrid(hit.Point);
	}
	else {
		placement.Position = SnapToGrid(camera->Position + camera->GetForwardVector() * scPlaceDistance);
	}

	EmplaceEditOperation<SubviewPlaceOperation>(instance, placement);

	SetTool(eEditorTool::Select);
	SelectSubview(instance, false);

	return true;
}

uint32 RaptorEditor::DissolveSelectedSubviews()
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || IsSimulationMode()) {
		return 0;
	}

	uint32 instances[scMaxSelectedObjects];
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (blockout->IsSubviewObject(object) && blockout->IsPlacedInActiveSubview(object->SubviewInstance)) {
			instances[count++] = object->SubviewInstance;
		}
	}

	for (uint32 i = 0; i < count; i++) {
		EmplaceEditOperation<SubviewDissolveOperation>(instances[i], static_cast<int32>(count));
	}

	mbForcePanelSync = true;

	return count;
}

void RaptorEditor::DrawSubviews()
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr || !blockout->IsEditingSubview()) {
		return;
	}

	BBox bounds;

	if (blockout->GetSubviewBounds(blockout->GetActiveSubview(), bounds)) {
		renderer::gDebugDraw->WireBox(bounds, mTheme.Viewport.EditedBounds);
	}
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
	DeselectSpawn();

	mSelection.Clear();
	mHistory.Clear();

	SyncSelection();
}

void RaptorEditor::ShowNotice(const String& message)
{
	mNotice = message;
	mNoticeSerial++;
	mbForcePanelSync = true;
}

bool RaptorEditor::HasUnsavedChanges() const { return mbUntrackedChanges || mHistory.GetRevision() != mSavedRevision; }

void RaptorEditor::MarkSaved()
{
	mSavedRevision = mHistory.GetRevision();
	mbUntrackedChanges = false;
}

void RaptorEditor::MarkModified()
{
	mbUntrackedChanges = true;
	++mUntrackedEditCount;
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

	mToolState.SelectedTool = mCurrentTool;
	mToolState.pTransformMarkerObject = (gWorld->pPrototype != nullptr) ? gWorld->pPrototype->pXFormObject : nullptr;

	SyncSelection();

	// Commands work on the selection, so give the script the current one
	mpCommandScript->CallFunction<void(const EditorToolState*)>("tool_state_receive", &mToolState);
	mpCommandScript->CallFunction<void(const EditorToolSelection*)>("tool_selection_receive", &mToolSelection);

	mpCommandScript->CallFunctionPtr<void()>(command);

	return true;
}


void RaptorEditor::Destroy()
{
	thread::SetUIWakeHandler(nullptr);

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
