#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>

#include <Asset/AssetManager.hpp>
#include <Asset/WorldFile.hpp>
#include <Blockout.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/Random.hpp>
#include <Core/String.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Physics/Ragdoll.hpp>
#include <Player.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/ShadowAtlas.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <Renderer/TextRenderer.hpp>
#include <Script/ScriptManager.hpp>
#include <Texture/TextureManager.hpp>
#include <World.hpp>
#include <WorldGrid.hpp>
#include <atomic>
#include <csignal>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef FX_IS_EDITOR
#include <Editor/EditorFrame.hpp>
#include <Editor/EditorTool.hpp>
#include <Editor/RaptorEditor.hpp>
#endif

FX_SET_MODULE_NAME("EngineHost");

using namespace fx;
using namespace fx::renderer;

namespace {

constexpr const char* scCrosshairPath = "Textures/crosshair.png";

bool sbQuit = false;

Ref<LightDirectional> spSun { nullptr };

AssetTicket sCrosshairTicket { nullptr };
std::atomic<Image*> spCrosshair = nullptr;

AssetTicket sRagdollTicket { nullptr };
std::atomic<Object*> spRagdollTemplate = nullptr;

struct RagdollDummy
{
	ObjectID RootID = ObjectID::scNull;
	std::unique_ptr<physics::Ragdoll> pRagdoll;
};

std::unordered_map<uint32, RagdollDummy> sDummies;
uint32 sNextDummy = 0;
std::vector<physics::RagdollImpact> sImpacts;

std::string sBlockoutPathScratch;

std::string FormatLines()
{
	std::string lines;

#ifdef FX_IS_EDITOR
	lines += std::format("Q={}, QE={}", gEditor->GetSnapStep(), gEditor->GetToolState().ToolSnapEnabled);

	const editor::EditorSelection& selection = gEditor->GetSelection();

	if (!selection.IsEmpty()) {
		lines += std::format("\nLast={}, Sel={}", selection.GetLast()->Name.Get(), selection.GetCount());
	}
#endif

	return lines;
}

size_t CopyOut(const std::string& text, char* buffer, size_t capacity)
{
	if (buffer != nullptr && capacity > 0) {
		const size_t count = std::min(text.size(), capacity);
		std::memcpy(buffer, text.data(), count);
	}

	return text.size();
}

void DestroyObjectTree(ObjectID root_id)
{
	Object* root = gObjectManager->GetObject(root_id);

	if (root == nullptr) {
		return;
	}

	std::vector<ObjectID> children;
	for (const ObjectID& child_id : root->AttachedNodes) {
		children.push_back(child_id);
	}

	root->AttachedNodes.Clear();

	for (const ObjectID& child_id : children) {
		DestroyObjectTree(child_id);
	}

	gWorld->Detach(root_id);
	gObjectManager->DestroyObject(root_id);
}

void CollectObjectTree(Object* root, std::vector<Object*>& out_parts)
{
	out_parts.push_back(root);

	for (ObjectID child_id : root->AttachedNodes) {
		Object* child = gObjectManager->GetObject(child_id);

		if (child != nullptr) {
			CollectObjectTree(child, out_parts);
		}
	}
}

void ReloadWorldFile(const std::string& scene)
{
	LogInfo("Reloading world...");

	WorldFile scene_file;
	scene_file.Load(std::format("RaptorData/Data/{}", scene));
}

std::string sScene;

} // namespace

extern "C" {

int rh_startup(int argc, char** argv, const char* title, uint32 width, uint32 height, int has_bob, int bob_enabled,
			   float bob_x, float bob_y, const char* blockout)
{
	fx::gEnginePool = new fx::MemPool;
	fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

	fx::gScriptMemPool = new fx::MemPool;
	fx::gScriptMemPool->Create(1024 * 64);

	gScriptManager = new ScriptManager;

#ifdef FX_IS_EDITOR
	gEditor = new editor::RaptorEditor;

	if (!gEditor->InitGUI(argc, argv)) {
		return 0;
	}
#else
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		LogError("Could not initialize SDL! (SDL err: {})", SDL_GetError());
		return 0;
	}
#endif

	fx::renderer::Globals::Init();

#ifdef FX_LOG_OUTPUT_TO_FILE
	LogCreateFile("FoxtrotLog.log");
#endif

	fx::Globals::Init();

	gWorld->Create();

	ControlManager::Init();
	ControlManager::GetInstance().OnQuit = [] { sbQuit = true; };

	signal(SIGABRT,
		   [](int)
		   {
			   LogError("Aborted!");
			   exit(1);
		   });

	Ref<Window> window = Window::New(title, Vec2u(width, height));

	if (has_bob) {
		gCVars->Set("b_headbob_enabled", static_cast<bool>(bob_enabled));

		gWorld->Player.HeadBobStrength.X = bob_x;
		gWorld->Player.HeadBobStrength.Y = bob_y;
	}

	gGraphics->SelectWindow(window);
	gGraphics->Init(window->GetSize());

	gPhysics->Create();
	gAssetManager->Start(3);
	gWorldGrid->Create(Vec2u(20, 20));

	gWorld->pBlockout = new Blockout;
	gWorld->pBlockout->Create(gWorld);

	if (blockout != nullptr) {
		gWorld->BlockoutPath = String(blockout);
		gWorld->pBlockout->Load(gWorld->BlockoutPath);
	}

	return 1;
}

int rh_begin_game(const char* scene)
{
	gWorld->Player.Create();
	gWorld->Player.pCamera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio());
	gWorld->Player.TeleportTo(Vec3f(0.0f, -0.2f, -2.0f));
	gWorld->Player.SetFlyMode(false);

	gWorld->SelectCamera(gWorld->Player.pCamera);

	sCrosshairTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, scCrosshairPath,
												eImageCreateFlags::None);
	sCrosshairTicket.OnLoaded([](void* data) { spCrosshair.store(static_cast<Image*>(data)); });

	if (scene != nullptr) {
		sScene = scene;

		WorldFile scene_file;
		scene_file.Load(std::format("RaptorData/Data/{}", sScene));
	}

	gPhysics->pBackend->OptimizeBroadPhase();

	gProbeManager->LoadProbes();

	spSun = gLightManager->GetDirectionalLight();

	gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
	gShadowRenderer->ShadowCamera.SetFarPlane(400.0f);
	gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
	gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
	gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

#ifdef FX_IS_EDITOR
	gEditor->SetReloadHandler(editor::eReloadTarget::World, [] { ReloadWorldFile(sScene); });
	gEditor->SetReloadHandler(editor::eReloadTarget::Prototype,
							  []
							  {
								  LogInfo("Reloading blockout...");
								  gWorld->pBlockout->Load(gWorld->BlockoutPath);
							  });
	gEditor->SetReloadHandler(editor::eReloadTarget::Scripts,
							  []
							  {
								  gEditor->ReloadScripts();
								  gWorld->Player.Weapons.OnScriptsReloaded();
							  });
#endif

	return 1;
}

void rh_shutdown()
{
	gGraphics->GetDevice()->WaitForIdle();

	sDummies.clear();

	delete gShadowRenderer;
	gShadowRenderer = nullptr;

	delete gShadowAtlas;
	gShadowAtlas = nullptr;

	gAssetManager->StopWorkers();
	gMaterialManager->Destroy();
	gAssetManager->Shutdown();

	delete gGraphics->pRenderer;
	gGraphics->pRenderer = nullptr;

	delete gTextureManager;
	gTextureManager = nullptr;

	delete gObjectManager;
	gObjectManager = nullptr;

	gWorld->Destroy();

	fx::Globals::Destroy();
	fx::renderer::Globals::Destroy();

	if (gAssetManager) {
		delete gAssetManager;
		gAssetManager = nullptr;
	}

#ifdef FX_IS_EDITOR
	gEditor->Destroy();
	delete gEditor;
	gEditor = nullptr;
#endif

	delete fx::gEnginePool;
	fx::gEnginePool = nullptr;
}

void rh_poll() { ControlManager::Update(); }
bool rh_key_down(uint16_t key) { return ControlManager::IsKeyDown(static_cast<eKey>(key)); }
bool rh_key_pressed(uint16_t key) { return ControlManager::IsKeyPressed(static_cast<eKey>(key)); }
bool rh_mouse_locked() { return ControlManager::IsMouseLocked(); }
void rh_capture_mouse() { ControlManager::CaptureMouse(); }
void rh_release_mouse() { ControlManager::ReleaseMouse(); }

void rh_mouse_delta(float* out)
{
	const Vec2f& delta = ControlManager::GetMouseDelta();
	out[0] = delta.X;
	out[1] = delta.Y;
}

uint32_t rh_typed_char() { return static_cast<uint32_t>(ControlManager::GetAlphaKey()); }
bool rh_quit_requested() { return sbQuit; }

bool rh_window_focused() { return gGraphics->GetWindow()->IsFocused(); }

void rh_window_size(uint32_t* out)
{
	const Vec2u size = gGraphics->GetWindow()->GetSize();
	out[0] = size.X;
	out[1] = size.Y;
}

void rh_set_window_title(const char* title) { gGraphics->GetWindow()->SetTitle(title); }
bool rh_did_resize() { return gGraphics->DidResize(); }
void rh_update_camera_aspect() { gWorld->Player.pCamera->SetAspectRatio(gGraphics->GetWindow()->GetAspectRatio()); }

size_t rh_blockout_path(char* buffer, size_t capacity)
{
	return CopyOut(std::string(gWorld->BlockoutPath.CStr()), buffer, capacity);
}

void rh_reload_blockout() { gWorld->pBlockout->Load(gWorld->BlockoutPath); }
void rh_save_blockout(const char* path) { gWorld->pBlockout->Save(String(path)); }

void rh_reload_scripts()
{
#ifdef FX_IS_EDITOR
	gEditor->ReloadScripts();
#else
	gScriptManager->ReloadAllScripts();
#endif

	gWorld->Player.Weapons.OnScriptsReloaded();
}

float rh_random_unit() { return RandomUnit(); }

bool rh_editor_active()
{
#ifdef FX_IS_EDITOR
	return true;
#else
	return false;
#endif
}

bool rh_editor_simulation_mode()
{
#ifdef FX_IS_EDITOR
	return gEditor->IsSimulationMode();
#else
	return true;
#endif
}

void rh_editor_set_tool(int tool)
{
#ifdef FX_IS_EDITOR
	gEditor->SetTool(tool == 0 ? editor::eEditorTool::None : editor::eEditorTool::Translate);
#endif
}

void rh_editor_update(float delta_time)
{
#ifdef FX_IS_EDITOR
	gEditor->Update(delta_time);
#endif
}

void rh_editor_refresh_panels()
{
#ifdef FX_IS_EDITOR
	gEditor->RefreshPanels();
#endif
}

size_t rh_editor_status_lines(char* buffer, size_t capacity) { return CopyOut(FormatLines(), buffer, capacity); }

bool rh_editor_run_command(const char* name)
{
#ifdef FX_IS_EDITOR
	return gEditor->RunCommand(String(name));
#else
	return false;
#endif
}

void rh_player_position(float* out)
{
	const Vec3f& p = gWorld->Player.Position;
	out[0] = p.X;
	out[1] = p.Y;
	out[2] = p.Z;
}

void rh_camera_forward(float* out)
{
	const Vec3f v = gWorld->Player.pCamera->GetForwardVector();
	out[0] = v.X;
	out[1] = v.Y;
	out[2] = v.Z;
}

void rh_camera_right(float* out)
{
	const Vec3f v = gWorld->Player.pCamera->GetRightVector();
	out[0] = v.X;
	out[1] = v.Y;
	out[2] = v.Z;
}

void rh_player_move(double delta_time, const float* movement)
{
	gWorld->Player.Move(delta_time, Vec3f(movement[0], movement[1], movement[2]));
}

void rh_player_update(double delta_time) { gWorld->Player.Update(delta_time); }
void rh_player_rotate_head(float x, float y) { gWorld->Player.RotateHead(Vec2f(x, y)); }
void rh_player_jump() { gWorld->Player.Jump(); }
bool rh_player_fly_mode() { return gWorld->Player.IsFlyMode(); }

void rh_player_set_fly_mode(bool fly)
{
	gWorld->Player.SetFlyMode(fly);
	gWorld->Player.Physics.SetCollisionEnabled(!fly);
}

void rh_player_set_sprinting(bool sprinting) { gWorld->Player.bIsSprinting = sprinting; }

void rh_weapons_set_enabled(bool enabled) { gWorld->Player.Weapons.SetEnabled(enabled); }
void rh_weapons_set_input(uint32_t flags) { gWorld->Player.Weapons.SetInput(static_cast<eWeaponInputFlags>(flags)); }
void rh_weapons_render_hud() { gWorld->Player.Weapons.RenderHud(); }

bool rh_toggle_render_probes()
{
	gWorld->bRenderProbes = !gWorld->bRenderProbes;
	return gWorld->bRenderProbes;
}

bool rh_toggle_only_render_probes()
{
	gGraphics->bOnlyRenderProbes = !gGraphics->bOnlyRenderProbes;
	return gGraphics->bOnlyRenderProbes;
}

bool rh_toggle_probe_visibility()
{
	gGraphics->bRenderProbeVisibility = !gGraphics->bRenderProbeVisibility;
	return gGraphics->bRenderProbeVisibility;
}

void rh_set_debug_bounds_mask(uint32_t mask) { gWorld->DebugBoundsMask = mask; }

void rh_log_nearby_objects()
{
	const ObjectIDSpan nearby_objects = gWorldGrid->GetNearbyObjects();

	LogInfo("=== Nearby Objects ===");

	for (ObjectID id : nearby_objects) {
		LogInfo("{}", id);
	}

	LogInfo("");
}

void rh_log_player_tile()
{
	TileIndex tile_index = gWorldGrid->WorldToTile(gWorld->Player.Position);

	Vec2u tile_xy = gWorldGrid->TileToTileXY(tile_index);

	LogInfo("Tile index: {}, {}", tile_xy.X, tile_xy.Y);
}

void rh_probes_rebuild_and_bake()
{
	gProbeManager->RebuildVolumesFromWorld();
	gProbeManager->BeginBake();
}

void rh_probes_save() { gProbeManager->SaveProbes(); }
void rh_probes_service_bake() { gProbeManager->ServiceCaptureBake(); }

void rh_update_sun()
{
	spSun->bEnabled = gCVars->Get("b_sun_enabled", true);

	if (spSun->bEnabled) {
		gShadowRenderer->PlaceCamera(gShadowRenderer->ShadowCamera, gWorld->Player.Position,
									 spSun->GetPosition().Normalize());
	}
}

bool rh_begin_frame(float delta_time)
{
	gGraphics->DeltaTime = delta_time;

	return gGraphics->BeginFrame() == eFrameResult::Success;
}

uint64_t rh_elapsed_frames() { return gGraphics->GetElapsedFrameCount(); }
void rh_physics_update() { gPhysics->pBackend->Update(); }

void rh_gpu_read_results(double delta_time)
{
	gGraphics->Profiler.ReadResults(gGraphics->GetFrameNumber(), delta_time);
}

bool rh_gpu_times(double* out)
{
	const GpuProfiler& gpu = gGraphics->Profiler;

	if (!gpu.IsEnabled()) {
		return false;
	}

	out[0] = gpu.GetTotalMs();
	out[1] = gpu.GetMs(eGpuMarker::Shadows);
	out[2] = gpu.GetMs(eGpuMarker::Prepass);
	out[3] = gpu.GetMs(eGpuMarker::LightCulling);
	out[4] = gpu.GetMs(eGpuMarker::SSAO);
	out[5] = gpu.GetMs(eGpuMarker::Forward);
	out[6] = gpu.GetMs(eGpuMarker::Composition);
	out[7] = gpu.GetMs(eGpuMarker::ProbeCapture);

	return true;
}

void rh_set_render_flags(bool disable_probes, bool disable_reflection_probes, uint32_t reflection_debug_view,
						 uint32_t tonemapper, bool disable_decals, float pre_exposure)
{
	gGraphics->bDisableProbes = disable_probes;
	gGraphics->bDisableReflectionProbes = disable_reflection_probes;
	gGraphics->ReflectionDebugView = reflection_debug_view;
	gGraphics->Tonemapper = tonemapper;
	gGraphics->bDisableDecals = disable_decals;
	gGraphics->PreExposure = pre_exposure;
}

void rh_begin_commands()
{
	FrameData* frame = gGraphics->GetFrame();

	frame->CmdBuffer.Reset();
	frame->CmdBuffer.Record();
	gGraphics->Profiler.BeginFrame(frame->CmdBuffer, gGraphics->GetFrameNumber());
}

void rh_render_world() { gWorld->Render(&gShadowRenderer->ShadowCamera); }
void rh_compose() { gGraphics->DoComposition(*gWorld->GetCurrentCamera()); }

void rh_scene_stats(uint32_t* out)
{
	out[0] = static_cast<uint32_t>(gWorld->mRenderList.GetItemCount());
	out[1] = static_cast<uint32_t>(gWorld->FrustumCulledObjects);
	out[2] = static_cast<uint32_t>(gWorld->FrustumTestedObjects);
	out[3] = static_cast<uint32_t>(gWorld->mLightList.GetItemCount());
	out[4] = static_cast<uint32_t>(gLightManager->GetCache().Size);
}

void rh_draw_text(const char* text, uint32_t color) { gTextRenderer->DrawText(text, 1.0f, color); }

void rh_draw_crosshair(float x, float y, float size, uint32_t color)
{
	gTextRenderer->DrawImage(spCrosshair.load(), Vec2f(x, y), Vec2f(size), color);
}

void rh_cvar_mirror_int(const char* name, int64_t value) { gCVars->Set(name, value); }
void rh_cvar_mirror_float(const char* name, float value) { gCVars->Set(name, value); }
void rh_cvar_mirror_bool(const char* name, bool value) { gCVars->Set(name, value); }
void rh_cvar_mirror_string(const char* name, const char* value) { gCVars->Set(name, value); }

void rh_ragdoll_request_template()
{
	sRagdollTicket = gAssetManager->LoadObject("ragdoll_template", "RaptorData/Data/Demo/Models/ragdoll/ragdoll.glb");

	sRagdollTicket.OnLoaded(
		[](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);

			if (object != nullptr) {
				object->SetShadowCaster(true);

				spRagdollTemplate.store(object);
			}
		});
}

bool rh_ragdoll_template_ready() { return spRagdollTemplate.load() != nullptr; }

bool rh_ragdoll_spawn(const float* position, const float* forward, uint32_t index, uint32_t* out_handle,
					  uint64_t* out_serial)
{
	Object* model = spRagdollTemplate.load();

	if (model == nullptr) {
		return false;
	}

	Object* root = model->CloneWithOwnSkeleton(String::Fmt("ragdoll_dummy_{}", index).Str());

	std::vector<Object*> parts;
	CollectObjectTree(root, parts);

	const auto skinned_part = std::find_if(parts.begin(), parts.end(),
										   [](const Object* part) { return part->pSkeleton.IsValid(); });

	if (skinned_part == parts.end()) {
		LogWarning("The ragdoll model has no skeleton");
		DestroyObjectTree(root->ID);
		return false;
	}

	Object* skinned = *skinned_part;

	root->SetPosition(Vec3f(position[0], position[1], position[2]));

	for (Object* part : parts) {
		part->SetCullable(false);
	}

	root->SetShadowCaster(true);
	root->SetProbeVisible(false);

	std::unique_ptr<physics::Ragdoll> ragdoll = std::make_unique<physics::Ragdoll>();

	if (!ragdoll->Create(skinned->pSkeleton, skinned->GetWorldMatrix())) {
		DestroyObjectTree(root->ID);
		return false;
	}

	gWorld->AttachLoaded(root);

	ragdoll->Activate(Vec3f(forward[0], forward[1], forward[2]) * 2.0f);

	*out_serial = ragdoll->GetSerial();
	*out_handle = sNextDummy++;

	sDummies[*out_handle] = RagdollDummy { .RootID = root->ID, .pRagdoll = std::move(ragdoll) };

	return true;
}

void rh_ragdoll_destroy(uint32_t handle)
{
	auto it = sDummies.find(handle);

	if (it == sDummies.end()) {
		return;
	}

	it->second.pRagdoll->Destroy();
	DestroyObjectTree(it->second.RootID);

	sDummies.erase(it);
}

void rh_ragdoll_sync(uint32_t handle, bool debug_draw)
{
	auto it = sDummies.find(handle);

	if (it == sDummies.end()) {
		return;
	}

	it->second.pRagdoll->WriteToSkeleton();

	if (debug_draw) {
		it->second.pRagdoll->DebugDraw();
	}
}

struct RhImpact
{
	uint64_t Serial;
	float Point[3];
	float Normal[3];
	float Speed;
};

size_t rh_ragdoll_drain_impacts(float min_speed, RhImpact* out, size_t capacity)
{
	gPhysics->pBackend->SetMinRagdollImpactSpeed(min_speed);

	sImpacts.clear();
	gPhysics->pBackend->DrainRagdollImpacts(sImpacts);

	const size_t count = std::min(sImpacts.size(), capacity);

	for (size_t i = 0; i < count; i++) {
		const physics::RagdollImpact& impact = sImpacts[i];

		out[i] = RhImpact {
			.Serial = impact.RagdollSerial,
			.Point = { impact.Point.X, impact.Point.Y, impact.Point.Z },
			.Normal = { impact.Normal.X, impact.Normal.Y, impact.Normal.Z },
			.Speed = impact.Speed,
		};
	}

	return count;
}

void rh_add_blood_splat(const float* point, const float* normal, float size)
{
	gDecalManager->AddBloodSplat(Vec3f(point[0], point[1], point[2]), Vec3f(normal[0], normal[1], normal[2]), size);
}

} // extern "C"
