#include "FoxtrotGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/MipmapGen.hpp>
#include <Asset/WorldFile.hpp>
#include <Controls.hpp>
#include <Core/Assert.hpp>
#include <Core/Defer.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>
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

FoxtrotGame::FoxtrotGame()
{
	InitEngine();
	CreateGame();
}


void FoxtrotGame::InitEngine()
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

	ControlManager::Init();
	ControlManager::GetInstance().OnQuit = [] { sbRunning = false; };

	// catch sigabrt to avoid macOS showing "report" popup
	signal(SIGABRT,
		   [](int signum)
		   {
			   LogError("Aborted!");
			   exit(1);
		   });

	ConfigEntry* window_entry = Config.GetEntry(HashStr32("Window"));

	const uint32 window_width = window_entry->GetMember(HashStr32("Width"))->Get<uint32>();
	const uint32 window_height = window_entry->GetMember(HashStr32("Height"))->Get<uint32>();

	Ref<Window> window = Window::New(window_entry->GetMember(HashStr32("Title"))->Get<const char*>(),
									 Vec2u(window_width, window_height));

	ConfigEntry* bob_entry = Config.GetEntry(HashStr32("HeadBob"));

	Player.bEnableHeadBob = static_cast<bool>(bob_entry->GetMemberValue(HashStr32("Enabled"), 1));
	Player.HeadBobStrength.X = bob_entry->GetMemberValue(HashStr32("ScaleX"), 0.011);
	Player.HeadBobStrength.Y = bob_entry->GetMemberValue(HashStr32("ScaleY"), 0.018);

	gGraphics->SelectWindow(window);
	gGraphics->Init(Vec2u(window_width, window_height));

	gPhysics->Create();
	gAssetManager->Start(3);
	gWorldGrid->Create(Vec2u(10, 10));

	sClockFreq = static_cast<double>(SDL_GetPerformanceFrequency());

	mBlockout.Create(&mMainScene);
	mBlockout.Load("./Data/Demo/blockout.prx");

	// script::Script test_script = gScriptManager->LoadScript("Scripts/strata_test.ssc");
	// if (test_script.HasErrors() == false) {
	// 	using FuncType = int (*)(void);

	// 	FuncType fn = test_script.GetFunction<FuncType>("run");
	// 	int result = fn();

	// 	LogInfo(LC_SCRIPT, "Strata result: {}", result);
	// }
}

void FoxtrotGame::ReloadAllObjects()
{
	mMainScene.Destroy();
	mMainScene.Create();

	WorldFile scene_file;
	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();
	scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load), mMainScene);
}

void FoxtrotGame::CreateLights()
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

void FoxtrotGame::LoadOffsetsFile()
{
	ConfigFile info;

	info.Load(FX_BASE_DIR "/Config/Offsets.conf");

	PistolOffset = info.GetEntryValue(HashStr32("PistolOffset"), Vec3f::sZero);
	ArmsOffset = info.GetEntryValue(HashStr32("ArmsOffset"), Vec3f::sZero);
}

Vec2f PixelsToUV(const Vec2i& pos, const Vec2f& size) { return Vec2f(pos.X / size.X, pos.Y / size.Y); }

void FoxtrotGame::CreateGame()
{
	mMainScene.Create();

	Player.Create();
	Player.pCamera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	// Move the player up and behind the other objects
	Player.TeleportTo(Vec3f(0.0f, -0.2f, -2.0f));
	Player.SetFlyMode(false);


	mMainScene.SelectCamera(Player.pCamera);

	AddEditorModes();

	WorldFile scene_file;

	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();

	scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load), mMainScene);
	gPhysics->OptimizeBroadPhase();

	pSun = mMainScene.GetDirectionalLight();

	LoadOffsetsFile();

	TSRef<Object> level_object = mMainScene.FindObject(HashStr32("Level"));

	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
	gShadowRenderer->ShadowCamera.SetFarPlane(200.0f);
	gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
	gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
	gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

	CreateLights();

	{
		// {
		// 	MaterialID mat_id = gMaterialManager->NewMaterial("TestMat", ePipelineName::Geometry, false);
		// 	Material* test_material = gMaterialManager->GetMaterial(mat_id);

		// 	AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
		// 												   "Data/Demo/Textures/white_grid.png",
		// 												   eImageCreateFlags::None);

		// 	test_material->Attach(Material::eResourceType::Diffuse, diffuse);
		// 	test_material->Finalize();
		// 	mBlockoutMaterial = mat_id;
		// }


		// Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube({ .Left = { .Scale = 3.0 } });

		// Object* object = gObjectManager->NewObject("HitMarker");
		// object->pMesh = cube_mesh->AsDefaultMesh();

		// object->MoveBy(Vec3f(0, 0.1, 0));
		// object->ScaleBy(0.2);

		// mRaycastHitMarker = object->ID;

		// object->mMaterialID = mBlockoutMaterial;

		// AssetTicket ticket(static_cast<void*>(object));
		// ticket.MarkAndSignalLoaded();

		// mMainScene.Attach(ticket);
	}


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

void FoxtrotGame::NextEditorMode()
{
	if (SelectedEditorMode != nullptr) {
		SelectedEditorMode->OnLeave(mMainScene);
	}


	EditorModeType = static_cast<eEditorMode>(static_cast<int32>(EditorModeType) + 1);
	if (static_cast<int32>(EditorModeType) > static_cast<int32>(eEditorMode::Default)) {
		EditorModeType = eEditorMode::MoveCollider;
	}
}

void FoxtrotGame::SwitchEditorMode(eEditorMode mode)
{
	if (SelectedEditorMode != nullptr) {
		SelectedEditorMode->OnLeave(mMainScene);
	}


	EditorModeType = mode;

	if (static_cast<int32>(EditorModeType) > static_cast<int32>(eEditorMode::Default)) {
		EditorModeType = eEditorMode::MoveCollider;
	}

	if (static_cast<int32>(EditorModeType) < static_cast<int32>(eEditorMode::MoveCollider)) {
		EditorModeType = eEditorMode::Default;
	}

	// Update cameras + current editor mode ptr
	if (EditorModeType != eEditorMode::Default) {
		SelectedEditorMode = EditorModes[static_cast<uint32>(EditorModeType)];
		// mMainScene.SelectCamera(pEditorCamera);
	}
	else {
		// mMainScene.SelectCamera(Player.pCamera);
		SelectedEditorMode = nullptr;
	}
}

void FoxtrotGame::NewBlockoutBrush() {}


void FoxtrotGame::ProcessControls()
{
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_GRAVE)) {
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

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_ESCAPE) && (EditorModeType != eEditorMode::Default)) {
		EditorModeType = eEditorMode::Default;
		mMainScene.SelectCamera(Player.pCamera);
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_G)) {
		SizedArray<JPH::BodyID> hits = Player.Physics.RaycastBodies(Player.pCamera->GetForwardVector() * 50.0f);
		LogInfo("HIT {} BODIES", hits.Size);
		if (hits.Size > 0) {
			mMainScene.SelectPhysicsObject(hits[0]);
		}

		for (JPH::BodyID body_id : hits) {
			LogInfo("HIT {}", body_id.GetIndex());
		}
	}

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_N)) {
		NewBlockoutBrush();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		RayResult hit_point = gPhysics->Raycast(Player.pCamera->Position, Player.pCamera->GetForwardVector() * 10.0f);
		LogInfo(LC_PHYSICS, "Hit?={}, Pos={}", hit_point.bHit, hit_point.Point);

		if (hit_point.bHit) {
			// Object* hit_marker = gObjectManager->GetObject(mRaycastHitMarker);
			// if (hit_marker) {
			// hit_marker->SetPosition(hit_point.Point);
			// }
		}
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_PERIOD)) {
		SwitchEditorMode(static_cast<eEditorMode>(static_cast<int32>(EditorModeType) + 1));
	}
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_COMMA)) {
		SwitchEditorMode(static_cast<eEditorMode>(static_cast<int32>(EditorModeType) - 1));
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_8)) {
		sbShowShadowCam = !sbShowShadowCam;


		if (sbShowShadowCam) {
			Player.pCamera->ProjectionMatrix = gShadowRenderer->ShadowCamera.ProjectionMatrix;
			Player.pCamera->ViewMatrix = gShadowRenderer->ShadowCamera.ViewMatrix;
			Player.pCamera->UpdateCameraMatrix();
		}
		else {
			Player.pCamera->UpdateProjectionMatrix();
			Player.pCamera->UpdateCameraMatrix();
		}
	}

	if (ControlManager::IsMouseLocked()) {
		Vec2f mouse_delta = ControlManager::GetMouseDelta();
		mouse_delta.X = static_cast<float32>(DeltaTime * static_cast<double>(mouse_delta.X) *
											 static_cast<double>(scMouseSensitivity));
		mouse_delta.Y = static_cast<float32>(DeltaTime * static_cast<double>(mouse_delta.Y) *
											 -static_cast<double>(scMouseSensitivity));

		// camera->Rotate(mouse_delta.GetX(), mouse_delta.GetY());
		Player.RotateHead(mouse_delta);
	}


	if (ControlManager::IsKeyDown(eKey::FX_KEY_SPACE)) {
		if (!Player.IsFlyMode()) {
			Player.Jump();
		}
	}

	// // Elevate up
	// if (ControlManager::IsKeyDown(eKey::FX_KEY_E)) {
	//     Player.Move(DeltaTime, Vec3f::sUp);
	// }
	// // Elevate down
	// if (ControlManager::IsKeyDown(eKey::FX_KEY_Q)) {
	//     Player.Move(DeltaTime, -Vec3f::sUp);
	// }


	if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		Player.bIsSprinting = true;
	}
	else {
		Player.bIsSprinting = false;
	}

	if (ControlManager::IsComboPressed(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_R)) {
		mBlockout.Load("./Data/Demo/blockout.prx");

		LogInfo("Reloading blockout...");


		// Reload the object properties from the scene
		// WorldFile scene_file;
		// scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, Config.GetEntry(HashStr32("Scene"))->Get<const
		// char*>()), 				mMainScene);

		// LoadOffsetsFile();
	}

	// if (ControlManager::IsKeyDown(eKey::FX_KEY_L)) {
	//     ReloadAllObjects();
	// }

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_P)) {
		// pHelmetObject->SetPhysicsEnabled(!pHelmetObject->GetPhysicsEnabled());
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
		TileIndex tile_index = gWorldGrid->GetTileIndex(Player.Position);

		Vec2u tile_xy = gWorldGrid->GetTileXY(tile_index);

		LogInfo("Tile index: {}, {}", tile_xy.X, tile_xy.Y);

		Object* object = gObjectManager->FindObject(HashStr32("FireExtinguisher"));
		LogInfo("object bounds:  {} -> {}  = {}", object->Bounds.Min, object->Bounds.Max, object->Bounds.GetSize());

		// Player.SetFlyMode(true);
		// Player.TeleportTo(Vec3f(0.0f, 4.0f, -4.0f));
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_N)) {
		// Player.Physics.bDisableGravity = !Player.Physics.bDisableGravity;
		Player.SetFlyMode(!Player.IsFlyMode());
		Player.Physics.SetCollisionEnabled(!Player.IsFlyMode());
	}
}


void FoxtrotGame::Tick()
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
	ProcessControls();

	Player.Move(DeltaTime, GetMovementVector());
	Player.Update(DeltaTime);


	if (EditorModeType != eEditorMode::Default) {
		SelectedEditorMode->Update(mMainScene, GetEditorMovementVector());
	}

	Ref<PerspectiveCamera> camera = Player.pCamera;

	gShadowRenderer->ShadowCamera.Position = (Player.Position + (pSun->GetPosition().Normalize() * 15.0f));

	Vec3f target = Player.Position;

	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(gShadowRenderer->ShadowCamera.Position, target, Vec3f(0, 1, 0));
	// LogInfo("{}", gShadowRenderer->ShadowCamera.ViewMatrix.Columns[3]);
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();
	gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;

	if (gGraphics->BeginFrame() != eFrameResult::Success) {
		mLastTick = current_tick;
		return;
	}

	gPhysics->Update();

	FrameData* frame = gGraphics->GetFrame();

	frame->CmdBuffer.Reset();
	frame->CmdBuffer.Record();

	// mMainScene.RenderShadows(&gShadowRenderer->ShadowCamera);
	mMainScene.Render(&gShadowRenderer->ShadowCamera);

	if (gGraphics->DidResize()) {
		LogInfo("Setting aspect ratio");
		camera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	}

	gGraphics->DoComposition(*mMainScene.GetCurrentCamera());

	mLastTick = current_tick;
}

void FoxtrotGame::DestroyGame()
{
	gGraphics->GetDevice()->WaitForIdle();

	delete gShadowRenderer;
	gShadowRenderer = nullptr;

	gMaterialManager->Destroy();
	gAssetManager->Shutdown();

	delete gGraphics->pRenderer;
	gGraphics->pRenderer = nullptr;
}

void FoxtrotGame::AddEditorModes()
{
	EditorModes.InitCapacity(static_cast<uint32>(eEditorMode::Default));

	EditorModes.Insert(gEnginePool->Alloc<EditorModeMoveCollider>(sizeof(EditorModeMoveCollider), nullptr));
	EditorModes.Insert(gEnginePool->Alloc<EditorModeScaleCollider>(sizeof(EditorModeScaleCollider), nullptr));
}


FoxtrotGame::~FoxtrotGame()
{
	DestroyGame();

	delete gTextureManager;
	gTextureManager = nullptr;

	delete gObjectManager;
	gObjectManager = nullptr;

	mMainScene.Destroy();

	// empty_images_list.Destroy();
}


/////////////////////////////////////
// Editor modes
/////////////////////////////////////

void EditorModeMoveCollider::Update(const World& scene, const Vec3f& movement_vector)
{
	physics::PhysID phys_id = scene.GetSelectedPhysicsObject();
	if (phys_id != physics::PhysID::scNull) {
		PhObject* phys = scene.GetPhysicsObject(phys_id);
		phys->Teleport(phys->GetPosition() + (movement_vector * Vec3f(0.05)), phys->GetRotation());

		Vec3f target = phys->GetPosition();
		pCamera->MoveTo(target + Vec3f(0, 10, -10));
		pCamera->Target = target;
		pCamera->bLookatTarget = true;

		pCamera->Update();
	}
}

void EditorModeMoveCollider::OnLeave(const World& scene) {}

void EditorModeScaleCollider::Update(const World& scene, const Vec3f& movement_vector)
{
	physics::PhysID phys_id = scene.GetSelectedPhysicsObject();
	if (phys_id != physics::PhysID::scNull) {
		PhObject* phys = scene.GetPhysicsObject(phys_id);
		phys->Dimensions = phys->Dimensions + (movement_vector * Vec3f(0.05));
		if (phys->Dimensions.X < 0.01f) {
			phys->Dimensions.X = 0.01f;
		}
		if (phys->Dimensions.Y < 0.01f) {
			phys->Dimensions.Y = 0.01f;
		}
		if (phys->Dimensions.Z < 0.01f) {
			phys->Dimensions.Z = 0.01f;
		}

		Vec3f target = phys->GetPosition();
		pCamera->MoveTo(target + Vec3f(0, 10, -10));
		pCamera->Target = target;
		pCamera->bLookatTarget = true;

		pCamera->Update();
	}
}

void EditorModeScaleCollider::OnLeave(const World& scene)
{
	physics::PhysID phys_id = scene.GetSelectedPhysicsObject();
	if (phys_id != physics::PhysID::scNull) {
		PhObject* phys = scene.GetPhysicsObject(phys_id);
		phys->CreatePrimitiveBody(phys->PrimitiveType, phys->Dimensions, phys->mMotionType, {});
	}
}


} // namespace fx
