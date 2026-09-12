#include "RaptorGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/MipmapGen.hpp>
#include <Asset/WorldFile.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Core/Assert.hpp>
#include <Core/Defer.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
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


FX_SET_MODULE_NAME("FoxtrotGame");

namespace fx {

using namespace renderer;

static constexpr float scMouseSensitivity = 0.25;

static constexpr uint32 scFramesForAvg = 10;


static double sClockFreq = 1.0;

static bool sbRunning = true;

static bool sbShowShadowCam = false;

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

	Config.Load(FX_BASE_DIR "/Config/Main.conf");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		ModulePanic("Could not initialize SDL! (SDL err: {})\n", SDL_GetError());
	}

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

	const char* window_title = "Foxtrot";

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
	gGraphics->Init(Vec2u(window_width, window_height));

	gPhysics->Create();
	gAssetManager->Start(3);
	gWorldGrid->Create(Vec2u(10, 10));

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

void RaptorGame::LoadOffsetsFile()
{
	ConfigFile info;

	info.Load(FX_BASE_DIR "/Config/Offsets.conf");

	PistolOffset = info.GetEntryValue(HashStr32("PistolOffset"), Vec3f::sZero);
	ArmsOffset = info.GetEntryValue(HashStr32("ArmsOffset"), Vec3f::sZero);
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

	AddEditorModes();

	WorldFile scene_file;

	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();

	scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load));
	gPhysics->pBackend->OptimizeBroadPhase();

	// Baked probes if the scene has them, procedural gradient otherwise.
	gProbeManager->LoadProbes();

	pSun = gWorld->GetDirectionalLight();

	LoadOffsetsFile();

	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
	gShadowRenderer->ShadowCamera.SetFarPlane(400.0f);
	gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
	gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
	gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

	CreateLights();

	while (sbRunning) {
		Tick();
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


void RaptorGame::SwitchEditorMode(eEditorMode mode)
{
	if (gSelectedEditorMode != nullptr) {
		gSelectedEditorMode->Unload();
	}


	EditorModeType = mode;

	if (static_cast<int32>(EditorModeType) > static_cast<int32>(eEditorMode::Simulate)) {
		EditorModeType = static_cast<eEditorMode>(0);
	}

	if (static_cast<int32>(EditorModeType) < 0) {
		EditorModeType = eEditorMode::Simulate;
	}

	if (EditorModeType != eEditorMode::Simulate) {
		gSelectedEditorMode = EditorModes[static_cast<uint32>(EditorModeType)];
		gSelectedEditorMode->Load();
	}
	else {
		gSelectedEditorMode = nullptr;
	}
}

Vec3f RaptorGame::GetCameraForwardDominantAxis() const
{
	Vec3f fwd = gWorld->Player.pCamera->GetForwardVector();
	Vec3f fa = fwd.Abs();

	if (fa.X > fa.Z) {
		return Vec3f(MathUtil::GetSign(fwd.X), 0.0f, 0.0f);
	}

	return Vec3f(0.0f, 0.0, MathUtil::GetSign(fwd.Z));
}


void RaptorGame::ProcessControls()
{
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
		// If ESCAPE is pressed while there is an object selected in editor mode, deselect the object.
		if (gSelectedEditorMode != nullptr && gSelectedEditorMode->IsObjectSelected()) {
			gSelectedEditorMode->SelectObject(nullptr);
		}
		else {
			ControlManager::ReleaseMouse();
		}
	}

	if (ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		// physics::RayResult hit_point = gPhysics->pBackend->Raycast(Player.pCamera->Position,
		// 														   Player.pCamera->GetForwardVector() * 10.0f);

		Ref<PerspectiveCamera>& cam = gWorld->Player.pCamera;

		SizedArray<JPH::BodyID> hits = gPhysics->pBackend->RaycastObjects(cam->Position,
																		  cam->GetForwardVector() * 4.0f);

		bool did_hit = false;

		// Do not select another object unless the selection has been cleared.
		if (gSelectedEditorMode != nullptr && !gSelectedEditorMode->IsObjectSelected()) {
			for (int i = 0; i < hits.Size; i++) {
				JPH::BodyID body_id = hits[i];

				physics::Body* body = gPhysics->FindBody(body_id);

				if (body == nullptr) {
					continue;
				}

				did_hit = gSelectedEditorMode->SelectObject(gObjectManager->GetObject(body->GetObjectID()));
				if (did_hit) {
					break;
				}
			}

			if (did_hit == false) {
				gSelectedEditorMode->SelectObject(nullptr);
			}
		}
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_PERIOD)) {
		SwitchEditorMode(static_cast<eEditorMode>(static_cast<int32>(EditorModeType) + 1));
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_COMMA)) {
		SwitchEditorMode(static_cast<eEditorMode>(static_cast<int32>(EditorModeType) - 1));
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_L)) {
		gWorld->bRenderProbes = !gWorld->bRenderProbes;
		LogInfo("Probe debug render {}", gWorld->bRenderProbes ? "enabled" : "disabled");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_2)) {
		gGraphics->bOnlyRenderProbes = !gGraphics->bOnlyRenderProbes;
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

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_TAB)) {
		SwitchEditorMode(static_cast<eEditorMode>(static_cast<int32>(EditorModeType) + 1));
	}


	if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		gWorld->Player.bIsSprinting = true;
	}
	else {
		gWorld->Player.bIsSprinting = false;
	}

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_R)) {
		LogInfo("Reloading blockout...");
		gWorld->pBlockout->Load(gWorld->BlockoutPath);
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_0)) {
		LogInfo("Reloading all scripts...");
		gScriptManager->ReloadAllScripts();

		if (gSelectedEditorMode != nullptr) {
			gSelectedEditorMode->Load();
		}
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
		TileIndex tile_index = gWorldGrid->GetTileIndex(gWorld->Player.Position);

		Vec2u tile_xy = gWorldGrid->GetTileXY(tile_index);

		LogInfo("Tile index: {}, {}", tile_xy.X, tile_xy.Y);
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_N)) {
		gWorld->Player.SetFlyMode(!gWorld->Player.IsFlyMode());
		gWorld->Player.Physics.SetCollisionEnabled(!gWorld->Player.IsFlyMode());
	}


	// if (ControlManager::IsKeyPressed(eKey::FX_KEY_C)) {
	// 	if (!gProbeManager->IsCapturePending()) {
	// 		gProbeManager->BeginCaptureBake(gWorld->Player.pCamera->Position);
	// 		LogInfo("Probe capture bake armed at {}", gWorld->Player.pCamera->Position);
	// 	}
	// }

	// if (ControlManager::IsKeyPressed(eKey::FX_KEY_V)) {
	// 	// Dense local volume around the player.
	// 	gProbeManager->BeginGridBakeAt(gWorld->Player.pCamera->Position, Vec3f(24.0f, 8.0f, 24.0f));
	// }

	// `G` fits the probe grid to the whole level instead.
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_G)) {
		gProbeManager->BeginGridBake();
	}

	// `P` saves volume + probes for the current scene (auto-loaded next run).
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

void RaptorGame::RenderText()
{
	static const uint32 scWhite = Color::FromRGBA(255, 255, 255, 255).AsUInt();
	static const uint32 scGreen = Color::FromRGBA(100, 255, 0, 255).AsUInt();

	if (bInCommandMode) {
		gTextRenderer->DrawText(String::Fmt(":{}", mCommandConsole.GetString()).CStr(), 2.0f, scGreen);
		gTextRenderer->DrawText(String::Fmt("={}", mCommandConsole.Output).CStr(), 2.0f, scWhite);
		return;
	}


	gTextRenderer->DrawText(String::Fmt("Mode={}, Op={}",
										gSelectedEditorMode ? gSelectedEditorMode->ModeName : "Simulate",
										gCVars->Get<const char*>("s_editor_op", "None"))
								.CStr(),
							2.0f, scWhite);
	gTextRenderer->DrawText(String::Fmt("P={}", gWorld->Player.Position).CStr(), 2.0f, scWhite);

	if (gSelectedEditorMode != nullptr) {
		gTextRenderer->DrawText(String::Fmt("Q={}, QE={}", gSelectedEditorMode->GetQuantizeFraction(),
											gSelectedEditorMode->GetQuantizeEnabled())
									.CStr(),
								2.0, scGreen);

		if (gSelectedEditorMode->mpLastSelectedObject != nullptr) {
			gTextRenderer->DrawText(String::Fmt("SEL={}", gSelectedEditorMode->mpLastSelectedObject->Name.Get()).CStr(),
									2.0, scGreen);
		}
	}
}


void RaptorGame::Tick()
{
	const uint64 current_tick = SDL_GetPerformanceCounter();

	DeltaTime = static_cast<double>(current_tick - mLastTick) / sClockFreq;

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

	gWorld->Player.Move(DeltaTime, GetMovementVector());
	gWorld->Player.Update(DeltaTime);


	if (EditorModeType != eEditorMode::Simulate) {
		Vec3f forward = GetCameraForwardDominantAxis();
		Vec3f right = Vec3f(forward.Z, 0.0f, -forward.X);
		Vec3f rawMovement = GetMovementVector();
		Vec3f movement = forward * rawMovement.Z + right * rawMovement.X + Vec3f(0, rawMovement.Y, 0);
		gSelectedEditorMode->Update(movement, static_cast<float32>(DeltaTime));
	}

	Ref<PerspectiveCamera> camera = gWorld->Player.pCamera;

	gShadowRenderer->ShadowCamera.Position = (gWorld->Player.Position + (pSun->GetPosition().Normalize() * 25.0f));

	Vec3f target = gWorld->Player.Position;

	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(gShadowRenderer->ShadowCamera.Position, target, Vec3f(0, 1, 0));
	// LogInfo("{}", gShadowRenderer->ShadowCamera.ViewMatrix.Columns[3]);
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();
	gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;

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

	gMaterialManager->Destroy();
	gAssetManager->Shutdown();

	delete gGraphics->pRenderer;
	gGraphics->pRenderer = nullptr;
}

void RaptorGame::AddEditorModes()
{
	EditorModes.InitCapacity(static_cast<uint32>(eEditorMode::Simulate));
	// {
	// 	EditorMode* mode = new EditorMode;
	// 	mode->Create("Translate", "./Scripts/editor/mode_translate.strata");
	// 	EditorModes.Insert(mode);
	// }
	// {
	// 	EditorMode* mode = new EditorMode;
	// 	mode->Create("Scale", "./Scripts/editor/mode_scale.strata");
	// 	EditorModes.Insert(mode);
	// }
	{
		EditorMode* mode = new EditorMode;
		mode->Create("Prototype", "./Scripts/editor/prototype_editor.strata");
		EditorModes.Insert(mode);
	}


	// EditorModes.Insert(gEnginePool->Alloc<EditorModeMoveCollider>(sizeof(EditorModeMoveCollider), nullptr));
	// EditorModes.Insert(gEnginePool->Alloc<EditorModeScaleCollider>(sizeof(EditorModeScaleCollider), nullptr));
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
