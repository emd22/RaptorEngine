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
#include <Core/Random.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <NPC.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Physics/Ragdoll.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Exposure.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <Renderer/TextRenderer.hpp>
#include <Script/ObjectScripts.hpp>
#include <Script/ScriptManager.hpp>
#include <Texture/TextureManager.hpp>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <filesystem>

#ifdef FX_IS_EDITOR
#include <Editor/EditorFrame.hpp>
#include <Editor/EditorTool.hpp>
#include <Editor/RaptorEditor.hpp>
#endif

#define FX_LIMIT_FRAMERATE_ON_FOCUS_LOST 1


FX_SET_MODULE_NAME("RaptorGame");

namespace fx {

using namespace renderer;

static constexpr float scMouseRadiansPerPixel = 0.25f / 120.0f;

/// How long the frame rate shown on screen is averaged over. Any shorter and it flickers too fast to read.
static constexpr double scFpsWindowSeconds = 0.25;

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

	mBaseWindowTitle = String(window_title);

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

void RaptorGame::UpdateExposure()
{
	if (!(mpExposureCVar && mpApertureCVar && mpShutterCVar && mpIsoCVar)) {
		return;
	}

	ExposureSettings exposure;
	exposure.Compensation = mpExposureCVar->FloatValue;
	exposure.Aperture = mpApertureCVar->FloatValue;
	exposure.ShutterTime = mpShutterCVar->FloatValue;
	exposure.ISO = mpIsoCVar->FloatValue;

	gGraphics->PreExposure = exposure.GetExposure();
}

void RaptorGame::CreateGame()
{
	gWorld->Player.Create();
	gWorld->Player.pCamera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	gWorld->RespawnPlayer();
	gWorld->Player.SetFlyMode(false);


	gWorld->SelectCamera(gWorld->Player.pCamera);

	gCVars->Set("i_crosshair_size", scCrosshairSize);

	mpShowFpsCVar = gCVars->Set("i_show_fps", 1);

	// GPU time per stage on screen, and switches for the light probes and the decals to see what they cost. Set
	// `$i_show_gpu`, `$r_probes` or `$r_decals` to 0 in the console
	mpShowGpuCVar = gCVars->Set("i_show_gpu", 1);
	mpProbesCVar = gCVars->Set("r_probes", 1);
	mpDecalsCVar = gCVars->Set("r_decals", 1);
	mpDebugBoundsCVar = gCVars->Set("r_debug_bounds", 0);

	mpExposureCVar = gCVars->Set("r_exposure_ev", gCVars->Get("r_exposure_ev", 0.0f));
	mpApertureCVar = gCVars->Set("r_aperture", gCVars->Get("r_aperture", 16.0f));
	mpShutterCVar = gCVars->Set("r_shutter", gCVars->Get("r_shutter", 0.01f));
	mpIsoCVar = gCVars->Set("r_iso", gCVars->Get("r_iso", 100.0f));
	mpTonemapperCVar = gCVars->Set("r_tonemapper", 1);

	// Metres between probes in a volume built from an editor brush. Set `$r_probe_spacing` in the console
	gCVars->Set("r_probe_spacing", 2.5f);
	gCVars->Set("r_probe_level_spacing", 0.5f);

	gCVars->Set("r_probe_bounces", 3);
	mpReflectionProbesCVar = gCVars->Set("r_reflection_probes", 1);
	gCVars->Set("r_reflection_level_probe", 0);
	mpReflectionDebugCVar = gCVars->Set("r_reflection_debug", 0);

	mpRagdollBloodSpeedCVar = gCVars->Set("b_ragdoll_blood_speed",
										  gCVars->Get("b_ragdoll_blood_speed", scDefaultRagdollBloodSpeed));

	mpNPCImpactDamageCVar = gCVars->Set("b_npc_impact_damage", gCVars->Get("b_npc_impact_damage", 0.25f));
	mpNPCImpactSpeedCVar = gCVars->Set("b_npc_impact_speed", gCVars->Get("b_npc_impact_speed", 1.5f));
	mpNPCReactStrengthCVar = gCVars->Set("b_npc_react_strength", gCVars->Get("b_npc_react_strength", 0.25f));

	gCVars->Set("r_ssao_radius", gCVars->Get("r_ssao_radius", 0.25f));
	gCVars->Set("r_ssao_bias", gCVars->Get("r_ssao_bias", 0.02f));
	gCVars->Set("r_ssao_strength", gCVars->Get("r_ssao_strength", 1.5f));
	gCVars->Set("r_ssao_power", gCVars->Get("r_ssao_power", 1.2f));
	gCVars->Set("r_ssao_floor", gCVars->Get("r_ssao_floor", 0.35f));

	mCrosshairTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, scCrosshairPath,
												eImageCreateFlags::None);

	// Runs on the asset thread, RenderCrosshair() starts drawing once it's set
	mCrosshairTicket.OnLoaded([this](void* data) { mpCrosshair.store(static_cast<Image*>(data)); });

	WorldFile scene_file;

	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();

	scene_file.Load(std::format("RaptorData/Data/{}", scene_to_load));
	gPhysics->pBackend->OptimizeBroadPhase();

	// Baked probes if the scene has them, procedural gradient otherwise.
	gProbeManager->LoadProbes();

	pSun = gLightManager->GetDirectionalLight();


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
	gEditor->SetReloadHandler(editor::eReloadTarget::Clean, [this] { CleanWorld(); });

	// Start out editing
	gEditor->SetTool(editor::eEditorTool::Translate);
#endif

	// Start the frame timer now, otherwise the first Tick() measures from counter zero (time since boot) and steps
	// physics by that much in one go.
	mLastTick = SDL_GetPerformanceCounter();

	while (sbRunning) {
		const uint64 frame_start = SDL_GetPerformanceCounter();

		Tick();

#ifdef FX_LIMIT_FRAMERATE_ON_FOCUS_LOST
		if (!gGraphics->GetWindow()->IsFocused()) {
			const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - frame_start) / sClockFreq;
			const double remaining = scUnfocusedFrameTime - elapsed;

			if (remaining > 0.0) {
				SDL_Delay(static_cast<uint32>(remaining * 1000.0));
			}
		}
#endif
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
		// Unset the key to block other click actions from happening
		ControlManager::ResetKey(eKey::FX_MOUSE_LEFT);
	}
	// Escape to unlock mouse
	else if (ControlManager::IsKeyPressed(eKey::FX_KEY_ESCAPE) && ControlManager::IsMouseLocked()) {
		ControlManager::ReleaseMouse();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_F8)) {
		RequestRagdollDrop();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_F9)) {
		++mNPCSpawnsPending;
		RequestRagdollTemplate();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_L)) {
		gWorld->bRenderProbes = !gWorld->bRenderProbes;
		LogInfo("Probe debug render {}", gWorld->bRenderProbes ? "enabled" : "disabled");
	}

	if (mpDebugBoundsCVar != nullptr) {
		const std::pair<eKey, uint32> bounds_keys[] = {
			{ eKey::FX_KEY_F5, World::scDebugBoundsObjects },
			{ eKey::FX_KEY_F6, World::scDebugBoundsLights },
			{ eKey::FX_KEY_F7, World::scDebugBoundsPhysics },
			{ eKey::FX_KEY_F4, World::scDebugBoundsRagdolls },
		};

		for (const auto& [key, bit] : bounds_keys) {
			if (ControlManager::IsKeyPressed(key)) {
				mpDebugBoundsCVar->IntValue ^= bit;

				const int64 mask = mpDebugBoundsCVar->IntValue;
				LogInfo("Debug bounds: objects {}, lights {}, physics {}, ragdolls {}",
						(mask & World::scDebugBoundsObjects) ? "on" : "off",
						(mask & World::scDebugBoundsLights) ? "on" : "off",
						(mask & World::scDebugBoundsPhysics) ? "on" : "off",
						(mask & World::scDebugBoundsRagdolls) ? "on" : "off");
			}
		}
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_2)) {
		gGraphics->bOnlyRenderProbes = !gGraphics->bOnlyRenderProbes;
		LogInfo("Probe irradiance debug view {}", gGraphics->bOnlyRenderProbes ? "enabled" : "disabled");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_3)) {
		gGraphics->bRenderProbeVisibility = !gGraphics->bRenderProbeVisibility;
		LogInfo("Probe visibility debug view {}", gGraphics->bRenderProbeVisibility ? "enabled" : "disabled");
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_4) && mpReflectionDebugCVar != nullptr) {
		static const char* const scViewNames[] = { "off", "mirror reflections", "probe coverage" };

		mpReflectionDebugCVar->IntValue = (std::clamp<int64>(mpReflectionDebugCVar->IntValue, 0, 2) + 1) % 3;
		LogInfo("Reflection probe debug view: {}", scViewNames[mpReflectionDebugCVar->IntValue]);
	}

	if (ControlManager::IsMouseLocked()) {
		Vec2f mouse_delta = ControlManager::GetMouseDelta();
		mouse_delta.X *= scMouseRadiansPerPixel;
		mouse_delta.Y *= -scMouseRadiansPerPixel;

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

	gWorld->Player.Weapons.SetEnabled(is_simulation_mode);

	if (is_simulation_mode) {
		eWeaponInputFlags input_flags = eWeaponInputFlags::None;

		if (was_mouse_locked && ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
			input_flags |= eWeaponInputFlags::FirePressed;
		}

		if (ControlManager::IsMouseLocked() && ControlManager::IsKeyDown(eKey::FX_MOUSE_LEFT)) {
			input_flags |= eWeaponInputFlags::FireHeld;
		}


		if (ControlManager::IsKeyPressed(eKey::FX_KEY_R) && !ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
			input_flags |= eWeaponInputFlags::ReloadPressed;
		}

		if (ControlManager::ControlManager::IsKeyPressed(eKey::FX_KEY_V)) {
			input_flags |= eWeaponInputFlags::SwitchMode;
		}

		if (ControlManager::ControlManager::IsKeyPressed(eKey::FX_KEY_TAB)) {
			input_flags |= eWeaponInputFlags::SwitchWeapon;
		}

		gWorld->Player.Weapons.SetInput(input_flags);
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
	// if (ControlManager::IsKeyPressed(eKey::FX_KEY_G)) {
	// 	gProbeManager->RebuildVolumesFromWorld();
	// 	gProbeManager->BeginBake();
	// }


	// `P` saves the volumes + probes for the current scene (auto-loaded next run).
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_P)) {
		gProbeManager->SaveProbes();
	}


	if (ControlManager::IsKeyPressed(eKey::FX_KEY_SLASH)) {
		bInCommandMode = !bInCommandMode;
	}

	// Save the blockout to a file
	if (ControlManager::IsComboPressed(eKey::FX_KEY_LMETA, eKey::FX_KEY_S) &&
		!ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
		const String path = (gWorld->BlockoutPath.GetLength() > 0) ? gWorld->BlockoutPath
																   : String("RaptorData/Data/blockouts/btemp.prx");

		LogInfo("Saving blockout to '{}'", path);
		gWorld->pBlockout->Save(path);
	}
}

void RaptorGame::RequestRagdollDrop()
{
	++mRagdollDropsPending;
	RequestRagdollTemplate();
}

void RaptorGame::RequestRagdollTemplate()
{
	if (mbRagdollTemplateRequested) {
		return;
	}

	mbRagdollTemplateRequested = true;

	mRagdollTemplateTicket = gAssetManager->LoadObject("ragdoll_template",
													   "RaptorData/Data/Demo/Models/ragdoll/ragdoll.glb");

	mRagdollTemplateTicket.OnLoaded(
		[this](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);

			if (object != nullptr) {
				object->SetShadowCaster(true);

				mpRagdollTemplate.store(object);
			}
		});

	LogInfo("Loading the ragdoll model, dummies drop once it has loaded");
}

void RaptorGame::SpawnDebugNPC(Object* model)
{
	if (gNPCManager->GetCount() >= scMaxDebugNPCs) {
		gNPCManager->Destroy(gNPCManager->Get(0));
	}

	Vec3f forward = gWorld->Player.pCamera->GetForwardVector();
	forward.Y = 0.0f;
	forward = forward.Normalize();

	const physics::RayResult floor = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->Position + forward * 2.5f,
																 Vec3f(0.0f, -10.0f, 0.0f));

	NPCDesc desc;
	desc.Name = String::Fmt("npc_{}", mNPCSpawnCount);
	desc.Position = floor.bHit ? floor.Point : (gWorld->Player.Position + forward * 2.5f);
	desc.Yaw = std::atan2(-forward.X, -forward.Z);

	++mNPCSpawnCount;

	gNPCManager->Spawn(model, desc);
}

void RaptorGame::DropRagdollDummy(Object* model)
{
	if (mRagdollDummies.size() >= scMaxRagdollDummies) {
		DestroyRagdollDummy(0);
	}

	Object* root = model->CloneWithOwnSkeleton(String::Fmt("ragdoll_dummy_{}", mRagdollSpawnCount).Str());

	std::vector<Object*> parts;
	CollectObjectTree(root, parts);

	const auto skinned_part = std::find_if(parts.begin(), parts.end(),
										   [](const Object* part) { return part->pSkeleton.IsValid(); });

	if (skinned_part == parts.end()) {
		LogWarning("The ragdoll model has no skeleton");
		DestroyObjectTree(root->ID);
		return;
	}

	Object* skinned = *skinned_part;

	Vec3f forward = gWorld->Player.pCamera->GetForwardVector();
	forward.Y = 0.0f;
	forward = forward.Normalize();

	const Vec3f right = gWorld->Player.pCamera->GetRightVector();

	const float32 lateral = (static_cast<float32>(mRagdollSpawnCount % 3) - 1.0f) * 0.9f;
	const float32 height = 1.0f + 0.3f * static_cast<float32>(mRagdollSpawnCount % 2);

	++mRagdollSpawnCount;

	root->SetPosition(gWorld->Player.Position + forward * 1.5f + right * lateral + Vec3f(0.0f, height, 0.0f));

	for (Object* part : parts) {
		part->SetCullable(false);
	}

	root->SetShadowCaster(true);
	root->SetProbeVisible(false);

	std::unique_ptr<physics::Ragdoll> ragdoll = std::make_unique<physics::Ragdoll>();

	if (!ragdoll->Create(skinned->pSkeleton, skinned->GetWorldMatrix())) {
		DestroyObjectTree(root->ID);
		return;
	}

	gWorld->AttachLoaded(root);

	ragdoll->Activate(forward * 2.0f);

	mRagdollDummies.push_back(
		RagdollDummy { .RootID = root->ID, .pRagdoll = std::move(ragdoll), .SkinnedID = skinned->ID });
}

void RaptorGame::DestroyRagdollDummy(size_t index)
{
	RagdollDummy& dummy = mRagdollDummies[index];

	dummy.pRagdoll->Destroy();

	DestroyObjectTree(dummy.RootID);

	mRagdollDummies.erase(mRagdollDummies.begin() + static_cast<std::ptrdiff_t>(index));
}

void RaptorGame::CleanWorld()
{
	while (!mRagdollDummies.empty()) {
		DestroyRagdollDummy(mRagdollDummies.size() - 1);
	}

	gNPCManager->ClearRagdolls();
	gDecalManager->Clear();
}

void RaptorGame::AddDummyWound(RagdollDummy& dummy, const physics::RagdollHit& hit)
{
	constexpr float32 scWoundCooldown = 0.25f;

	if (dummy.BloodCooldown > 0.0f) {
		return;
	}

	Object* skinned = gObjectManager->GetObject(dummy.SkinnedID);
	uint32 bone_index = 0;

	if (skinned == nullptr || !skinned->pSkeleton.IsValid() || !dummy.pRagdoll->FindBoneForBody(hit.Body, bone_index) ||
		bone_index >= skinned->pSkeleton->SkinningMatrices.Size) {
		return;
	}

	const Mat4f rest_to_world = skinned->pSkeleton->SkinningMatrices[bone_index] * skinned->GetWorldMatrix();

	gDecalManager->AddSkinnedBloodSplat(&(*skinned->pSkeleton), rest_to_world, hit.Point, hit.Direction,
										GetHitWoundSize(hit));

	dummy.BloodCooldown = scWoundCooldown;
}

static float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

void RaptorGame::SpawnRagdollBlood(float32& cooldown, const physics::RagdollImpact& impact, float32 min_speed)
{
	constexpr float32 scCooldown = 0.25f;
	constexpr float32 scMinSize = 0.4f;
	constexpr float32 scMaxSize = 1.0f;
	constexpr float32 scSpeedRange = 8.0f;

	if (cooldown > 0.0f) {
		return;
	}

	cooldown = scCooldown;

	const float32 strength = std::clamp((impact.Speed - min_speed) / scSpeedRange, 0.0f, 1.0f);
	const float32 size = scMinSize + (scMaxSize - scMinSize) * strength;

	gDecalManager->AddBloodSplat(impact.Point, impact.Normal, size);

	const Vec3f reference = (std::abs(impact.Normal.Y) < 0.9f) ? Vec3f::sUp : Vec3f::sRight;
	const Vec3f tangent = reference.Cross(impact.Normal).Normalize();
	const Vec3f bitangent = impact.Normal.Cross(tangent);

	const uint32 satellites = 2 + static_cast<uint32>(strength * 2.0f);

	for (uint32 i = 0; i < satellites; i++) {
		const float32 angle = RandomUnit() * 2.0f * static_cast<float32>(M_PI);
		const float32 distance = size * (0.4f + 0.6f * RandomUnit());

		const Vec3f offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * distance;

		gDecalManager->AddBloodSplat(impact.Point + offset, impact.Normal, size * (0.25f + 0.25f * RandomUnit()));
	}
}

void RaptorGame::UpdateObjectScripts()
{
#ifdef FX_IS_EDITOR
	const bool is_simulation_mode = gEditor->IsSimulationMode();
#else
	const bool is_simulation_mode = true;
#endif

	if (is_simulation_mode) {
		gObjectScripts->Update(static_cast<float32>(DeltaTime), gWorld->Player.Position);
	}
	else {
		gObjectScripts->Stop();
	}
}

void RaptorGame::UpdateRagdollDummies()
{
	Object* model = mpRagdollTemplate.load();

	while (mRagdollDropsPending > 0 && model != nullptr) {
		--mRagdollDropsPending;
		DropRagdollDummy(model);
	}

	while (mNPCSpawnsPending > 0 && model != nullptr) {
		--mNPCSpawnsPending;
		SpawnDebugNPC(model);
	}

	const bool draw_debug = (gWorld->DebugBoundsMask & World::scDebugBoundsRagdolls) != 0;

	if (mpNPCImpactDamageCVar != nullptr && mpNPCImpactSpeedCVar != nullptr) {
		gNPCManager->ImpactDamageScale = mpNPCImpactDamageCVar->FloatValue;
		gNPCManager->MinImpactSpeed = mpNPCImpactSpeedCVar->FloatValue;
	}

	if (mpNPCReactStrengthCVar != nullptr) {
		gNPCManager->ReactStrength = mpNPCReactStrengthCVar->FloatValue;
	}

	gNPCManager->Update(static_cast<float32>(DeltaTime), draw_debug);

	if (mRagdollDummies.empty() && gNPCManager->GetCount() == 0) {
		return;
	}

	const float32 min_blood_speed = (mpRagdollBloodSpeedCVar != nullptr) ? mpRagdollBloodSpeedCVar->FloatValue
																		 : scDefaultRagdollBloodSpeed;

	gPhysics->pBackend->SetMinRagdollImpactSpeed(min_blood_speed);

	mRagdollImpacts.clear();
	gPhysics->pBackend->DrainRagdollImpacts(mRagdollImpacts);

	for (RagdollDummy& dummy : mRagdollDummies) {
		dummy.BloodCooldown = std::max(dummy.BloodCooldown - static_cast<float32>(DeltaTime), 0.0f);
	}

	for (const physics::RagdollHit& hit : gNPCManager->GetHits()) {
		for (RagdollDummy& dummy : mRagdollDummies) {
			if (dummy.pRagdoll->GetSerial() == hit.RagdollSerial) {
				AddDummyWound(dummy, hit);
				break;
			}
		}
	}

	for (const physics::RagdollImpact& impact : mRagdollImpacts) {
		if (NPC* npc = gNPCManager->FindByRagdollSerial(impact.RagdollSerial)) {
			SpawnRagdollBlood(npc->BloodCooldown, impact, min_blood_speed);
			continue;
		}

		for (RagdollDummy& dummy : mRagdollDummies) {
			if (dummy.pRagdoll->GetSerial() == impact.RagdollSerial) {
				SpawnRagdollBlood(dummy.BloodCooldown, impact, min_blood_speed);
				break;
			}
		}
	}

	for (RagdollDummy& dummy : mRagdollDummies) {
		dummy.pRagdoll->WriteToSkeleton();

		if (draw_debug) {
			dummy.pRagdoll->DebugDraw();
		}
	}
}

void RaptorGame::UpdateWindowTitle()
{
	if (gWorld == nullptr || (mbTitleShown && gWorld->BlockoutPath == mTitleBlockoutPath)) {
		return;
	}

	Ref<Window> window = gGraphics->GetWindow();
	if (!window.IsValid()) {
		return;
	}

	mTitleBlockoutPath = gWorld->BlockoutPath;
	mbTitleShown = true;

	if (mTitleBlockoutPath.GetLength() == 0) {
		window->SetTitle(mBaseWindowTitle.CStr());
		return;
	}

	const std::string file_name = std::filesystem::path(mTitleBlockoutPath.CStr()).filename().string();

	window->SetTitle(String::Fmt("{} - {}", mBaseWindowTitle.CStr(), file_name).CStr());
}

void RaptorGame::ReloadWorldFile()
{
	LogInfo("Reloading world...");

	WorldFile scene_file;
	const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();
	scene_file.Load(std::format("RaptorData/Data/{}", scene_to_load));
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

	gWorld->Player.Weapons.OnScriptsReloaded();
	gObjectScripts->OnScriptsReloaded();
}

void RaptorGame::RenderText()
{
	static const uint32 scWhite = Color::FromRGBA(255, 255, 255, 255).AsUInt();
	static const uint32 scGreen = Color::FromRGBA(100, 255, 0, 255).AsUInt();

	if (bInCommandMode) {
		gTextRenderer->DrawText(String::Fmt(":{}", mCommandConsole.GetString()).CStr(), 1.0f, scGreen);
		gTextRenderer->DrawText(String::Fmt("={}", mCommandConsole.Output).CStr(), 1.0f, scWhite);
		return;
	}


	if (mpShowFpsCVar == nullptr || mpShowFpsCVar->IntValue != 0) {
		gTextRenderer->DrawText(String::Fmt("FPS={:.0f} ({:.2f}ms)", Fps, FrameTimeMs).CStr(), 1.0f, scWhite);
	}

	const GpuProfiler& gpu = gGraphics->Profiler;

	if (gpu.IsEnabled() && (mpShowGpuCVar == nullptr || mpShowGpuCVar->IntValue != 0)) {
		gTextRenderer->DrawText(String::Fmt("GPU={:.2f}ms", gpu.GetTotalMs()).CStr(), 1.0f, scWhite);
		gTextRenderer->DrawText(String::Fmt("Sh {:.2f} Pre {:.2f} Cull {:.2f} SSAO {:.2f} Fwd {:.2f} Comp {:.2f}",
											gpu.GetMs(eGpuMarker::Shadows), gpu.GetMs(eGpuMarker::Prepass),
											gpu.GetMs(eGpuMarker::LightCulling), gpu.GetMs(eGpuMarker::SSAO),
											gpu.GetMs(eGpuMarker::Forward), gpu.GetMs(eGpuMarker::Composition))
									.CStr(),
								1.0f, scWhite);

		// Only bakes have this
		if (gpu.GetMs(eGpuMarker::ProbeCapture) > 0.005) {
			gTextRenderer->DrawText(String::Fmt("Bake {:.2f}", gpu.GetMs(eGpuMarker::ProbeCapture)).CStr(), 1.0f,
									scWhite);
		}
	}

	gTextRenderer->DrawText(String::Fmt("Vis={} Culled={}/{} Lights={}/{}", gWorld->mRenderList.GetItemCount(),
										gWorld->FrustumCulledObjects, gWorld->FrustumTestedObjects,
										gWorld->mLightList.GetItemCount(), gLightManager->GetCache().Size)
								.CStr(),
							1.0f, scWhite);

#ifdef FX_IS_EDITOR
	gTextRenderer->DrawText(
		String::Fmt("Q={}, QE={}", gEditor->GetSnapStep(), gEditor->GetToolState().ToolSnapEnabled).CStr(), 1.0,
		scGreen);

	const editor::EditorSelection& selection = gEditor->GetSelection();

	if (!selection.IsEmpty()) {
		gTextRenderer->DrawText(
			String::Fmt("Last={}, Sel={}", selection.GetLast()->Name.Get(), selection.GetCount()).CStr(), 1.0, scGreen);
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

	mFpsWindowTime += DeltaTime;

	if (mFpsWindowTime >= scFpsWindowSeconds) {
		// Frames that were finished, not calls to Tick(), since a Tick() that returns early (the swapchain is being
		// rebuilt) does not draw a frame and would make the frame rate read too high
		const uint64 elapsed_frames = gGraphics->GetElapsedFrameCount();
		const uint64 frames = elapsed_frames - mFpsWindowStartFrame;

		if (frames > 0) {
			FrameTimeMs = (mFpsWindowTime / static_cast<double>(frames)) * 1000.0;
			Fps = static_cast<double>(frames) / mFpsWindowTime;
		}

		mFpsWindowTime = 0.0;
		mFpsWindowStartFrame = elapsed_frames;
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

	gPhysics->pBackend->Update(static_cast<float32>(DeltaTime));

	FrameData* frame = gGraphics->GetFrame();

	// This frame slot's fence has been waited on, so what it timed last time round is ready
	gGraphics->Profiler.ReadResults(gGraphics->GetFrameNumber(), DeltaTime);

	gGraphics->bDisableProbes = (mpProbesCVar != nullptr) && (mpProbesCVar->IntValue == 0);
	gGraphics->bDisableReflectionProbes = (mpReflectionProbesCVar != nullptr) &&
										  (mpReflectionProbesCVar->IntValue == 0);
	gGraphics->ReflectionDebugView = (mpReflectionDebugCVar != nullptr)
										 ? static_cast<uint32>(std::clamp<int64>(mpReflectionDebugCVar->IntValue, 0, 2))
										 : 0;
	gGraphics->Tonemapper = (mpTonemapperCVar != nullptr)
								? static_cast<uint32>(std::clamp<int64>(mpTonemapperCVar->IntValue, 0, 1))
								: 1;
	gGraphics->bDisableDecals = (mpDecalsCVar != nullptr) && (mpDecalsCVar->IntValue == 0);

	UpdateWindowTitle();
	gWorld->DebugBoundsMask = (mpDebugBoundsCVar != nullptr) ? static_cast<uint32>(mpDebugBoundsCVar->IntValue) : 0;

	UpdateObjectScripts();
	UpdateRagdollDummies();

	UpdateExposure();

	frame->CmdBuffer.Reset();
	frame->CmdBuffer.Record();
	gGraphics->Profiler.BeginFrame(frame->CmdBuffer, gGraphics->GetFrameNumber());

	// mMainScene.RenderShadows(&gShadowRenderer->ShadowCamera);
	gWorld->Render(&gShadowRenderer->ShadowCamera);

	if (gGraphics->DidResize()) {
		LogInfo("Setting aspect ratio");
		camera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	}

	RenderText();
	RenderCrosshair();
	gWorld->Player.Weapons.RenderHud();

	gGraphics->DoComposition(*gWorld->GetCurrentCamera());

	// Progressive probe bakes (single captures finish in one call, grid bakes
	// advance one probe per frame).
	gProbeManager->ServiceCaptureBake();

	mLastTick = current_tick;
}

void RaptorGame::DestroyGame()
{
	gGraphics->GetDevice()->WaitForIdle();

	for (RagdollDummy& dummy : mRagdollDummies) {
		dummy.pRagdoll->Destroy();
	}

	mRagdollDummies.clear();
	gNPCManager->Clear();

	delete gShadowRenderer;
	gShadowRenderer = nullptr;

	delete gShadowAtlas;
	gShadowAtlas = nullptr;

	gAssetManager->StopWorkers();
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


// void EditorModeMoveCollider::Update(const World& scene, const Vec3f movement_vector)
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

// void EditorModeScaleCollider::Update(const World& scene, const Vec3f movement_vector)
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
