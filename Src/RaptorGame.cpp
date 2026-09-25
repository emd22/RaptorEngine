#include "RaptorGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/WorldFile.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Core/Assert.hpp>
#include <Core/Defer.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <Renderer/TextRenderer.hpp>
#include <Script/ScriptManager.hpp>
#include <Texture/TextureManager.hpp>
#include <csignal>

#ifdef FX_IS_EDITOR
#include <Editor/EditorFrame.hpp>
#include <Editor/EditorTool.hpp>
#include <Editor/RaptorEditor.hpp>
#endif


FX_SET_MODULE_NAME("RaptorGame");

namespace fx {

using namespace renderer;

static constexpr float scMouseSensitivity = 0.25;

static constexpr uint32 scFramesForAvg = 10;

/// Target frame time while the window doesn't have OS focus, so an unfocused/backgrounded window doesn't burn a full
/// core rendering frames nobody's looking at
static constexpr double scUnfocusedFrameTime = 1.0 / 10.0;


static double sClockFreq = 1.0;

static bool sbRunning = true;

static bool sbShowShadowCam = false;

static constexpr const char* scCrosshairPath = "Textures/crosshair.png";

/// Crosshair width and height in window pixels, set `$i_crosshair_size` in the console to change it (0 hides it)
static constexpr int64 scCrosshairSize = 3;

RaptorGame::RaptorGame()
{
	InitEngine();
	CreateGame();
}


void RaptorGame::InitEngine()
{
#ifdef FX_LOG_OUTPUT_TO_FILE
	LogCreateFile("FoxtrotLog.log");
#endif

	Config.Load("Config/Main.conf");

#ifndef FX_IS_EDITOR
	// The editor's window and input come from wxWidgets (see editor::Init), and SDL's video subsystem would fight it
	// over the native application object
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		ModulePanic("Could not initialize SDL! (SDL err: {})\n", SDL_GetError());
	}
#endif

	// Create the global engine variables
	fx::Globals::Init();

	gWorld->Create();

	ControlManager::Init();
	ControlManager::GetInstance().OnQuit = [] { sbRunning = false; };

	signal(SIGABRT,
		   [](int signum)
		   {
			   LogError("Aborted!");
			   exit(1);
		   });

	ConfigEntry* window_entry = Config.GetEntry(HashStr32("Window"));

	uint32 window_width = 800;
	uint32 window_height = 800;

	const char* window_title = "Raptor Engine";

	if (window_entry != nullptr) {
		window_width = window_entry->GetMember(HashStr32("Width"))->Get<uint32>();
		window_height = window_entry->GetMember(HashStr32("Height"))->Get<uint32>();
		window_title = window_entry->GetMember(HashStr32("Title"))->Get<const char*>();
	}

	Ref<Window> window = Window::New(window_title, Vec2u(window_width, window_height));

	ConfigEntry* bob_entry = Config.GetEntry(HashStr32("HeadBob"));

	if (bob_entry != nullptr) {
		gCVars->Set("b_headbob_enabled", static_cast<bool>(bob_entry->GetMemberValue(HashStr32("Enabled"), 1)));

		gWorld->Player.HeadBobStrength.X = bob_entry->GetMemberValue(HashStr32("ScaleX"), 0.011);
		gWorld->Player.HeadBobStrength.Y = bob_entry->GetMemberValue(HashStr32("ScaleY"), 0.018);
	}

	gGraphics->SelectWindow(window);
	gGraphics->Init(window->GetSize());

	gPhysics->Create();
	gAssetManager->Start(3);
	gWorldGrid->Create(Vec2u(20, 20));

	sClockFreq = static_cast<double>(SDL_GetPerformanceFrequency());

	gWorld->pBlockout = new Blockout;
	gWorld->pBlockout->Create(gWorld);

	ConfigEntry* blockout_entry = Config.GetEntry(HashStr32("blockout"));
	if (blockout_entry) {
		gWorld->BlockoutPath = String(blockout_entry->Get<const char*>());
		gWorld->pBlockout->Load(gWorld->BlockoutPath);
	}
}


void RaptorGame::CreateLights()
{
	// Ref<LightPoint> pl = Ref<LightPoint>::New();
	// pl->Color = Color::FromRGBA(50, 250, 100, 8);
	// pl->MoveBy(Vec3f(0, 1, 0));
	// pl->SetRadius(3.0);
	// // pl->SetScale(15);

	// mMainScene.Attach(pl);

	// Ref<LightPoint> pl2 = Ref<LightPoint>::New();
	// pl2->Color = Color::FromRGBA(200, 80, 100, 8);
	// pl2->MoveBy(Vec3f(1, 0.5, 0));
	// pl2->SetRadius(3.0);

	// mMainScene.Attach(pl2);
}


Vec2f PixelsToUV(const Vec2i& pos, const Vec2f& size) { return Vec2f(pos.X / size.X, pos.Y / size.Y); }

void RaptorGame::CreateGame()
{
	gWorld->Player.Create();
	gWorld->Player.pCamera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	// Move the player up and behind the other objects
	gWorld->Player.TeleportTo(Vec3f(0.0f, -0.2f, -2.0f));
	gWorld->Player.SetFlyMode(false);


	gWorld->SelectCamera(gWorld->Player.pCamera);

	gCVars->Set("i_crosshair_size", scCrosshairSize);

	// Metres between probes in a volume built from an editor brush. Set `$r_probe_spacing` in the console
	gCVars->Set("r_probe_spacing", 2.5f);

	mCrosshairTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, scCrosshairPath,
												eImageCreateFlags::None);

	// Runs on the asset thread, RenderCrosshair() starts drawing once it's set
	mCrosshairTicket.OnLoaded([this](void* data) { mpCrosshair.store(static_cast<Image*>(data)); });

	WorldFile scene_file;

	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();

	scene_file.Load(std::format("Data/{}", scene_to_load));
	gPhysics->pBackend->OptimizeBroadPhase();

	// Baked probes if the scene has them, procedural gradient otherwise.
	gProbeManager->LoadProbes();

	pSun = gWorld->GetDirectionalLight();


	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
	gShadowRenderer->ShadowCamera.SetFarPlane(400.0f);
	gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
	gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
	gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

	CreateLights();

#ifdef FX_IS_EDITOR
	gEditor->SetReloadHandler(editor::eReloadTarget::World, [this] { ReloadWorldFile(); });
	gEditor->SetReloadHandler(editor::eReloadTarget::Prototype, [this] { ReloadBlockout(); });
	gEditor->SetReloadHandler(editor::eReloadTarget::Scripts, [this] { ReloadScripts(); });

	// Start out editing
	gEditor->SetTool(editor::eEditorTool::Translate);
#endif

	// Start the frame timer now, otherwise the first Tick() measures from counter zero (time since boot) and steps
	// physics by that much in one go.
	mLastTick = SDL_GetPerformanceCounter();

	while (sbRunning) {
		const uint64 frame_start = SDL_GetPerformanceCounter();

		Tick();

		if (!gGraphics->GetWindow()->IsFocused()) {
			const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - frame_start) / sClockFreq;
			const double remaining = scUnfocusedFrameTime - elapsed;

			if (remaining > 0.0) {
				SDL_Delay(static_cast<uint32>(remaining * 1000.0));
			}
		}
	}
}


static FX_FORCE_INLINE Vec3f GetMovementVector()
{
	Vec3f movement = Vec3f::sZero;

	if (ControlManager::IsKeyDown(eKey::FX_KEY_W)) {
		movement.Z += 1.0f;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_S)) {
		movement.Z += -1.0f;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_A)) {
		movement.X += -1.0f;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_D)) {
		movement.X += 1.0f;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_E)) {
		movement.Y += 1.0f;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_Q)) {
		movement.Y += -1.0f;
	}

	return movement;
}

static FX_FORCE_INLINE Vec3f GetEditorMovementVector()
{
	Vec3f movement = Vec3f::sZero;

	const float speed = 0.25f;

	if (ControlManager::IsKeyDown(eKey::FX_KEY_UP)) {
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
			movement.Y += speed;
		}
		else {
			movement.Z += speed;
		}
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_DOWN)) {
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
			movement.Y += -speed;
		}
		else {
			movement.Z += -speed;
		}
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_LEFT)) {
		movement.X += -speed;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_RIGHT)) {
		movement.X += speed;
	}

	return movement;
}


void RaptorGame::ToggleEditorMode()
{
#ifdef FX_IS_EDITOR
	gEditor->SetTool(gEditor->IsSimulationMode() ? editor::eEditorTool::Translate : editor::eEditorTool::None);
#endif
}

/// How far the player's shots reach
static constexpr float32 scShotRange = 100.0f;

/// Casts a shot from the camera and leaves a bullet hole where it lands
static void FireShot()
{
	Ref<PerspectiveCamera>& cam = gWorld->Player.pCamera;

	physics::RayResult hit = gPhysics->pBackend->Raycast(cam->Position, cam->GetForwardVector() * scShotRange);

	if (!hit.bHit) {
		return;
	}

	// Decals stay where they are put, so only geometry that can't move gets them
	if (gPhysics->pBackend->GetBodyInterface().GetMotionType(hit.Body) != JPH::EMotionType::Static) {
		return;
	}

	gDecalManager->AddBulletHole(hit.Point, hit.Normal);
}


void RaptorGame::ProcessControls()
{
#ifdef FX_IS_EDITOR
	const bool is_simulation_mode = gEditor->IsSimulationMode();
#else
	const bool is_simulation_mode = true;
#endif

	// The click that captures the mouse shouldn't also fire
	const bool was_mouse_locked = ControlManager::IsMouseLocked();

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_GRAVE)) {
		// Release the mouse before quitting the game incase there is a crash.
		ControlManager::ReleaseMouse();
		sbRunning = false;
	}

	// Click to lock mouse
	if (ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT) && !ControlManager::IsMouseLocked()) {
		ControlManager::CaptureMouse();
	}
	// Escape to unlock mouse
	else if (ControlManager::IsKeyPressed(eKey::FX_KEY_ESCAPE) && ControlManager::IsMouseLocked()) {
		ControlManager::ReleaseMouse();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_L)) {
		gWorld->bRenderProbes = !gWorld->bRenderProbes;
		LogInfo("Probe debug render {}", gWorld->bRenderProbes ? "enabled" : "disabled");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_2)) {
		gGraphics->bOnlyRenderProbes = !gGraphics->bOnlyRenderProbes;
		LogInfo("Probe irradiance debug view {}", gGraphics->bOnlyRenderProbes ? "enabled" : "disabled");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_3)) {
		gGraphics->bRenderProbeVisibility = !gGraphics->bRenderProbeVisibility;
		LogInfo("Probe visibility debug view {}", gGraphics->bRenderProbeVisibility ? "enabled" : "disabled");
	}

	if (ControlManager::IsMouseLocked()) {
		Vec2f mouse_delta = ControlManager::GetMouseDelta();
		mouse_delta.X = static_cast<float32>(DeltaTime * static_cast<double>(mouse_delta.X) *
											 static_cast<double>(scMouseSensitivity));
		mouse_delta.Y = static_cast<float32>(DeltaTime * static_cast<double>(mouse_delta.Y) *
											 -static_cast<double>(scMouseSensitivity));

		gWorld->Player.RotateHead(mouse_delta);
	}


	if (ControlManager::IsKeyDown(eKey::FX_KEY_SPACE)) {
		if (!gWorld->Player.IsFlyMode()) {
			gWorld->Player.Jump();
		}
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_X)) {
		ToggleEditorMode();
	}


	if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		gWorld->Player.bIsSprinting = true;
	}
	else {
		gWorld->Player.bIsSprinting = false;
	}

	if (is_simulation_mode && was_mouse_locked && ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		FireShot();
		gWorld->Player.DoFireAnimation();
	}

	if (is_simulation_mode && ControlManager::IsKeyPressed(eKey::FX_KEY_R) &&
		!ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		gWorld->Player.DoReloadAnimation();
	}


	if (ControlManager::IsComboPressed(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_R)) {
		ReloadBlockout();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_0)) {
		ReloadScripts();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_H)) {
		const SizedArray<ObjectID>& nearby_objects = gWorldGrid->GetNearbyObjects();

		LogInfo("=== Nearby Objects ===");

		for (ObjectID id : nearby_objects) {
			LogInfo("{}", id);
		}

		LogInfo("");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_0)) {
		TileIndex tile_index = gWorldGrid->WorldToTile(gWorld->Player.Position);

		Vec2u tile_xy = gWorldGrid->TileToTileXY(tile_index);

		LogInfo("Tile index: {}, {}", tile_xy.X, tile_xy.Y);
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_N)) {
		gWorld->Player.SetFlyMode(!gWorld->Player.IsFlyMode());
		gWorld->Player.Physics.SetCollisionEnabled(!gWorld->Player.IsFlyMode());
	}


	// `G` rebuilds the volumes from the level (a coarse volume over all of it, plus one per probe volume brush
	// the editor has tagged) and bakes them.
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_G)) {
		gProbeManager->RebuildVolumesFromWorld();
		gProbeManager->BeginBake();
	}


	// `P` saves the volumes + probes for the current scene (auto-loaded next run).
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_P)) {
		gProbeManager->SaveProbes();
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_SLASH)) {
		bInCommandMode = !bInCommandMode;
	}

	// Save the blockout to a file
	if (ControlManager::IsComboPressed(eKey::FX_KEY_LMETA, eKey::FX_KEY_S)) {
		LogInfo("Saving blockout...");
		gWorld->pBlockout->Save("Data/blockouts/btemp.prx");
	}
}

void RaptorGame::ReloadWorldFile()
{
	LogInfo("Reloading world...");

	WorldFile scene_file;
	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();
	scene_file.Load(std::format("Data/{}", scene_to_load));
}

void RaptorGame::ReloadBlockout()
{
	LogInfo("Reloading blockout...");
	gWorld->pBlockout->Load(gWorld->BlockoutPath);
}

void RaptorGame::ReloadScripts()
{
	LogInfo("Reloading all scripts...");

#ifdef FX_IS_EDITOR
	// The editor picks its tools' functions back up afterwards
	gEditor->ReloadScripts();
#else
	gScriptManager->ReloadAllScripts();
#endif
}

void RaptorGame::RenderText()
{
	static const uint32 scWhite = Color::FromRGBA(255, 255, 255, 255).AsUInt();
	static const uint32 scGreen = Color::FromRGBA(100, 255, 0, 255).AsUInt();

	if (bInCommandMode) {
		gTextRenderer->DrawText(String::Fmt(":{}", mCommandConsole.GetString()).CStr(), 2.0f, scGreen);
		gTextRenderer->DrawText(String::Fmt("={}", mCommandConsole.Output).CStr(), 2.0f, scWhite);
		return;
	}


	gTextRenderer->DrawText(String::Fmt("Vis={}", gWorld->mRenderList.GetItemCount()).CStr(), 2.0f, scWhite);

#ifdef FX_IS_EDITOR
	gTextRenderer->DrawText(
		String::Fmt("Q={}, QE={}", gEditor->GetSnapStep(), gEditor->GetToolState().ToolSnapEnabled).CStr(), 2.0,
		scGreen);

	const editor::EditorSelection& selection = gEditor->GetSelection();

	if (!selection.IsEmpty()) {
		gTextRenderer->DrawText(
			String::Fmt("Last={}, Sel={}", selection.GetLast()->Name.Get(), selection.GetCount()).CStr(), 2.0, scGreen);
	}
#endif
}

void RaptorGame::RenderCrosshair()
{
	static const uint32 scWhite = Color::FromRGBA(255, 255, 255, 255).AsUInt();

	const Vec2u window_size = gGraphics->GetWindow()->GetSize();

	// Whole pixels keep the image's texels lined up with the screen's, so it stays sharp
	const Vec2f position(static_cast<float32>((static_cast<int64>(window_size.X) - scCrosshairSize) / 2),
						 static_cast<float32>((static_cast<int64>(window_size.Y) - scCrosshairSize) / 2));

	gTextRenderer->DrawImage(mpCrosshair.load(), position, Vec2f(static_cast<float32>(scCrosshairSize)), scWhite);
}


void RaptorGame::Tick()
{
	const uint64 current_tick = SDL_GetPerformanceCounter();

	DeltaTime = static_cast<double>(current_tick - mLastTick) / sClockFreq;
	gGraphics->DeltaTime = static_cast<float32>(DeltaTime);

	FrameTimeAvg += DeltaTime;

	if (!(gGraphics->GetFrameNumber() % scFramesForAvg)) {
		double frametime = FrameTimeAvg / scFramesForAvg;
		double fps = 1.0 / frametime;

		// LogInfo("FrameTime={}, FPS={}", frametime, fps);

		FrameTimeAvg = 0;
	}


	ControlManager::Update();

	if (bInCommandMode) {
		if (ControlManager::IsKeyPressed(eKey::FX_KEY_ESCAPE)) {
			bInCommandMode = false;
		}

		mCommandConsole.HandleKeyboard();
	}
	else {
		ProcessControls();
	}

	if (!bInCommandMode) {
		gWorld->Player.Move(DeltaTime, GetMovementVector());

#ifdef FX_IS_EDITOR
		gEditor->Update(static_cast<float32>(DeltaTime));
#endif
	}

#ifdef FX_IS_EDITOR
	gEditor->RefreshPanels();
#endif

	gWorld->Player.Update(DeltaTime);


	Ref<PerspectiveCamera> camera = gWorld->Player.pCamera;

	// Set from the scene file (see WorldFile::Load), or from the console
	pSun->bEnabled = gCVars->Get("b_sun_enabled", true);

	if (pSun->bEnabled) {
		gShadowRenderer->PlaceCamera(gShadowRenderer->ShadowCamera, gWorld->Player.Position,
									 pSun->GetPosition().Normalize());
	}

	if (gGraphics->BeginFrame() != eFrameResult::Success) {
		mLastTick = current_tick;
		return;
	}

	gPhysics->pBackend->Update();

	FrameData* frame = gGraphics->GetFrame();

	frame->CmdBuffer.Reset();
	frame->CmdBuffer.Record();

	// mMainScene.RenderShadows(&gShadowRenderer->ShadowCamera);
	gWorld->Render(&gShadowRenderer->ShadowCamera);

	if (gGraphics->DidResize()) {
		LogInfo("Setting aspect ratio");
		camera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	}

	RenderText();
	RenderCrosshair();

	gGraphics->DoComposition(*gWorld->GetCurrentCamera());

	// Progressive probe bakes (single captures finish in one call, grid bakes
	// advance one probe per frame).
	gProbeManager->ServiceCaptureBake();

	mLastTick = current_tick;
}

void RaptorGame::DestroyGame()
{
	gGraphics->GetDevice()->WaitForIdle();

	delete gShadowRenderer;
	gShadowRenderer = nullptr;

	delete gShadowAtlas;
	gShadowAtlas = nullptr;

	gMaterialManager->Destroy();
	gAssetManager->Shutdown();

	delete gGraphics->pRenderer;
	gGraphics->pRenderer = nullptr;
}

RaptorGame::~RaptorGame()
{
	DestroyGame();

	delete gTextureManager;
	gTextureManager = nullptr;

	delete gObjectManager;
	gObjectManager = nullptr;

	gWorld->Destroy();

	// empty_images_list.Destroy();
}


/////////////////////////////////////
// Editor modes
/////////////////////////////////////


// void EditorModeMoveCollider::Update(const World& scene, const Vec3f& movement_vector)
// {
// 	physics::BodyID phys_id = scene.GetSelectedPhysicsObject();
// 	if (phys_id != physics::BodyID::scNull) {
// 		physics::Body* phys = scene.GetPhysicsObject(phys_id);
// 		phys->Teleport(phys->GetPosition() + (movement_vector * Vec3f(0.05)), phys->GetRotation());

// 		Vec3f target = phys->GetPosition();
// 		pCamera->MoveTo(target + Vec3f(0, 10, -10));
// 		pCamera->Target = target;
// 		pCamera->bLookatTarget = true;

// 		pCamera->Update();
// 	}
// }

// void EditorModeMoveCollider::OnLeave(const World& scene) {}

// void EditorModeScaleCollider::Update(const World& scene, const Vec3f& movement_vector)
// {
// 	physics::BodyID phys_id = scene.GetSelectedPhysicsObject();
// 	if (phys_id != physics::BodyID::scNull) {
// 		physics::Body* phys = scene.GetPhysicsObject(phys_id);
// 		phys->Dimensions = phys->Dimensions + (movement_vector * Vec3f(0.05));
// 		if (phys->Dimensions.X < 0.01f) {
// 			phys->Dimensions.X = 0.01f;
// 		}
// 		if (phys->Dimensions.Y < 0.01f) {
// 			phys->Dimensions.Y = 0.01f;
// 		}
// 		if (phys->Dimensions.Z < 0.01f) {
// 			phys->Dimensions.Z = 0.01f;
// 		}

// 		Vec3f target = phys->GetPosition();
// 		pCamera->MoveTo(target + Vec3f(0, 10, -10));
// 		pCamera->Target = target;
// 		pCamera->bLookatTarget = true;

// 		pCamera->Update();
// 	}
// }

// void EditorModeScaleCollider::OnLeave(const World& scene)
// {
// 	physics::BodyID phys_id = scene.GetSelectedPhysicsObject();
// 	if (phys_id != physics::BodyID::scNull) {
// 		physics::Body* phys = scene.GetPhysicsObject(phys_id);
// 		phys->CreatePrimitiveBody(phys->PrimitiveType, phys->Dimensions, phys->mMotionType, {});
// 	}
// }


} // namespace fx
