#include "FoxtrotGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AxManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/MipmapGen.hpp>
#include <Asset/SceneFile.hpp>
#include <Controls.hpp>
#include <Core/Defer.hpp>
#include <Core/Panic.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <csignal>

FX_SET_MODULE_NAME("FoxtrotGame");

namespace fx {

using namespace renderer;

static constexpr float scMouseSensitivity = 0.25;

static constexpr uint32 scFramesForAvg = 10;


static double sClockFreq = 1.0f;

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

    Player.bEnableHeadBob = static_cast<bool>(Config.GetEntryValue<int32>(HashStr32("EnableHeadBob"), 1));

    gRenderer->SelectWindow(window);
    gRenderer->Init(Vec2u(window_width, window_height));

    gPhysics->Create();

    gAssetManager->Start(3);
    gMaterialManager->Create();

    sClockFreq = static_cast<double>(SDL_GetPerformanceFrequency());
}

void FoxtrotGame::ReloadAllObjects()
{
    mMainScene.Destroy();
    mMainScene.Create();

    SceneFile scene_file;
    const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();
    scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load), mMainScene);
}

void FoxtrotGame::CreateLights()
{
    // Ref<LightPoint> pl = Ref<LightPoint>::New();

    // Ref<MeshGen::GeneratedMesh> sphere = MeshGen::MakeIcoSphere(4);
    // pl->SetLightVolume(sphere);
    // pl->Color = Color::FromRGBA(50, 250, 100, 3);
    // pl->MoveBy(Vec3f(0, 1, 0));
    // pl->SetRadius(5.0);
    // pl->SetScale(15);

    // mMainScene.Attach(pl);

    // Ref<LightPoint> pl2 = Ref<LightPoint>::New();

    // pl2->SetLightVolume(sphere);
    // pl2->Color = Color::FromRGBA(200, 80, 80, 3);
    // pl2->MoveBy(Vec3f(1, 0.5, 0));
    // pl2->SetRadius(5.0);
    // pl2->SetScale(15);

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

void FoxtrotGame::CreateFontObject()
{
    // Vec2f atlas_size = Vec2f(512.0f, 256.0f);

    // Vec2i glyph_size = Vec2i::sZero;
    // Vec2i glyph_offset = Vec2i::sZero;

    // {
    //     ConfigFile font_meta;
    //     font_meta.Load(FX_BASE_DIR "/Meta.conf");

    //     ConfigEntry* glyphs_entry = font_meta.GetEntry(HashStr32("Glyphs"));

    //     ConfigEntry* glyph = glyphs_entry->GetMember(HashStr32("65"));

    //     glyph_size = glyph->GetMemberValue(HashStr32("Size"), Vec2i(5, 5));
    //     glyph_offset = glyph->GetMemberValue(HashStr32("Offset"), Vec2i::sZero);
    // }


    // TSRef<Object> object = TSRef<Object>::New();
    // object->Name.Set("FontObject");

    // MeshGenOptions options { .Scale = 0.5 };
    // options.UvMin = PixelsToUV(glyph_offset, atlas_size);
    // options.UvMax = options.UvMin + PixelsToUV(glyph_size, atlas_size);

    // Ref<MeshGen::GeneratedMesh> quad = MeshGen::MakeQuad(options);
    // object->pMesh = quad->AsMesh(eVertexType::Default);

    // object->Material = gMaterialManager->NewMaterial("Font material", &gPipelineCache->Request(ePipelineName::Unlit),
    //                                                  false);
    // Material* material = gMaterialManager->GetMaterial(object->Material);

    // TSRef<AxImage> image = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
    //                                                 FX_BASE_DIR "/DefaultFont.png");
    // material->Attach(Material::eResourceType::Diffuse, image);

    // object->SetRenderUnlit(true);
    // object->SetGraphicsPipeline(&gPipelineCache->Request(ePipelineName::Unlit));

    // object->MarkReadyToRender();

    // // mMainScene.Attach(object);

    // object->PrintDebug();
}

void FoxtrotGame::CreateGame()
{
    mMainScene.Create();

    Player.Create();
    Player.pCamera->SetAspectRatio(gRenderer->GetWindow()->GetAspectRatio());
    // Move the player up and behind the other objects
    Player.TeleportTo(Vec3f(0.0f, 4.0f, -4.0f));
    Player.SetFlyMode(true);

    pEditorCamera = MakeRef<PerspectiveCamera>();

    pEditorCamera->SetAspectRatio(gRenderer->GetWindow()->GetAspectRatio());
    pEditorCamera->SetFov(80.0f);

    mMainScene.SelectCamera(Player.pCamera);

    AddEditorModes();

    SceneFile scene_file;

    const char* scene_to_load = Config.GetEntry(HashStr32("Scene"))->Get<const char*>();

    scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load), mMainScene);
    gPhysics->OptimizeBroadPhase();

    pSun = mMainScene.GetDirectionalLight();

    /*
        pPistolObject = mMainScene.FindObject(HashStr32("Pistol"));
        pArmsObject = mMainScene.FindObject(HashStr32("AnimTest"));*/

    LoadOffsetsFile();


    TSRef<Object> level_object = mMainScene.FindObject(HashStr32("Level"));

    gShadowRenderer = new ShadowDirectional(Vec2u(2048, 2048));
    gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
    gShadowRenderer->ShadowCamera.SetFarPlane(200.0f);
    gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
    gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
    gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;
    gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

    {
        // renderer::Font font;
        // if (font.LoadFromFile("/System/Library/Fonts/Courier.ttc", 48.0f)) {
        //     font.SaveToFile("./DefaultFont.png", eImageSaveFormat::Png);
        // }
        // else {
        //     LogError("Failed to load font!");
        // }
    }

    CreateLights();
    CreateFontObject();


    TSRef<AxImage> test_img = gAssetManager->LoadImage(FX_BASE_DIR "/Textures/beach.jpg", eImageFormat::RGBA8_UNorm,
                                                       eImageCreateFlags::KeepInMemory);
    test_img->WaitUntilLoaded();

    MipmapGen mm;
    // mm.GenerateMipmaps(eImageFormat::RGBA8_UNorm, test_img->Image.ImageData, test_img->Image.Size);
    mm.ExportMipmaps("TestMips.bin", "TestMipsExport");

    // DataPack dp;
    // dp.ReadFromFile("TestMips.bin");
    // dp.PrintInfo();
    // dp.Close();

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
        SizedArray<JPH::BodyID> hits = Player.Physics.Raycast(Player.pCamera->GetForwardVector() * 50.0f);
        LogInfo("HIT {} BODIES", hits.Size);
        if (hits.Size > 0) {
            mMainScene.SelectPhysicsObject(hits[0]);
        }

        for (JPH::BodyID body_id : hits) {
            LogInfo("HIT {}", body_id.GetIndex());
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
        mouse_delta.X = (DeltaTime * mouse_delta.X * scMouseSensitivity);
        mouse_delta.Y = (DeltaTime * mouse_delta.Y * -scMouseSensitivity);

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

    if (ControlManager::IsComboDown(eKey::FX_KEY_LSHIFT, eKey::FX_KEY_R)) {
        // Reload the object properties from the scene
        SceneFile scene_file;
        scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, Config.GetEntry(HashStr32("Scene"))->Get<const char*>()),
                        mMainScene);

        LoadOffsetsFile();
    }

    // if (ControlManager::IsKeyDown(eKey::FX_KEY_L)) {
    //     ReloadAllObjects();
    // }

    if (ControlManager::IsKeyPressed(eKey::FX_KEY_P)) {
        pHelmetObject->SetPhysicsEnabled(!pHelmetObject->GetPhysicsEnabled());
    }

    if (ControlManager::IsKeyPressed(eKey::FX_KEY_0)) {
        Player.SetFlyMode(true);
        Player.TeleportTo(Vec3f(0.0f, 4.0f, -4.0f));
    }


    if (ControlManager::IsKeyPressed(eKey::FX_KEY_N)) {
        // Player.Physics.bDisableGravity = !Player.Physics.bDisableGravity;
        Player.SetFlyMode(!Player.IsFlyMode());
    }
}


void FoxtrotGame::Tick()
{
    const uint64 current_tick = SDL_GetPerformanceCounter();

    DeltaTime = static_cast<double>(current_tick - mLastTick) / sClockFreq;

    FrameTimeAvg += DeltaTime;

    if (!(gRenderer->GetFrameNumber() % scFramesForAvg)) {
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

    // Vec3f pistol_destination = camera->Position + (camera->Direction * Vec3f(0.48)) -
    //                              camera->GetRightVector() * Vec3f(0.165) - camera->GetUpVector() *
    //                              Vec3f(0.15);


    /*pArmsObject->SetRotationOrigin(ArmsOffset);
    pArmsObject->SetPosition(camera->Position + ArmsOffset);

    PistolRotationGoal = Quat::FromEulerAngles(Vec3f(-camera->mAngleY, camera->mAngleX, 0));
    pArmsObject->mRotation = PistolRotationGoal;

    if (pArmsObject->pSkeleton) {
        if (RHandBone == BoneNull) {
            RHandBone = pArmsObject->pSkeleton->FindBone(pArmsObject->pCurrentAnimation, "hand.R");
        }

        BoneTransform hand_transform = pArmsObject->pSkeleton->GetBoneTransform(pArmsObject->pCurrentAnimation,
                                                                                pArmsObject->AnimationTime, RHandBone);

        hand_transform.Position *= Vec3f(pArmsObject->mScale);

        pPistolObject->SetRotationOrigin(ArmsOffset + hand_transform.Position + PistolOffset);
        pPistolObject->SetPosition(pArmsObject->mPosition + hand_transform.Position + PistolOffset);

        pPistolObject->mRotation = PistolRotationGoal;
    }*/

    gShadowRenderer->ShadowCamera.Position = (Player.Position + (pSun->GetPosition().Normalize() * 15.0f));

    Vec3f target = Player.Position;

    gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(gShadowRenderer->ShadowCamera.Position, target, Vec3f(0, 1, 0));
    // LogInfo("{}", gShadowRenderer->ShadowCamera.ViewMatrix.Columns[3]);
    gShadowRenderer->ShadowCamera.UpdateCameraMatrix();
    gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;

    if (gRenderer->BeginFrame() != eFrameResult::Success) {
        mLastTick = current_tick;
        return;
    }

    gPhysics->Update();

    FrameData* frame = gRenderer->GetFrame();

    frame->CmdBuffer.Reset();
    frame->CmdBuffer.Record();

    mMainScene.RenderShadows(&gShadowRenderer->ShadowCamera);
    mMainScene.Render(&gShadowRenderer->ShadowCamera);

    if (gRenderer->DidResize()) {
        LogInfo("Setting aspect ratio");
        camera->SetAspectRatio(gRenderer->GetWindow()->GetAspectRatio());
    }

    gRenderer->DoComposition(*mMainScene.GetCurrentCamera());

    mLastTick = current_tick;
}

void FoxtrotGame::DestroyGame()
{
    fx::LogInfo("===== MEMPOOL STATS =====");
    fx::LogInfo("Bytes Used: {}", fx::gEnginePool->GetBytesUsed());
    fx::LogInfo("Pool Size:  {}", fx::gEnginePool->GetCapacity());
    fx::LogInfo("=========================");

    gRenderer->GetDevice()->WaitForIdle();

    delete gShadowRenderer;
    gShadowRenderer = nullptr;

    gAssetManager->Shutdown();

    gRenderer->pDeferredRenderer.DestroyRef();
}

void FoxtrotGame::AddEditorModes()
{
    EditorModes.InitCapacity(static_cast<uint32>(eEditorMode::Default));

    EditorModes.Insert(gEnginePool->Alloc<EditorModeMoveCollider>(sizeof(EditorModeMoveCollider), pEditorCamera));
    EditorModes.Insert(gEnginePool->Alloc<EditorModeScaleCollider>(sizeof(EditorModeScaleCollider), pEditorCamera));
}


FoxtrotGame::~FoxtrotGame()
{
    DestroyGame();
    mMainScene.Destroy();
}


/////////////////////////////////////
// Editor modes
/////////////////////////////////////

void EditorModeMoveCollider::Update(const Scene& scene, const Vec3f& movement_vector)
{
    PhObjectId phys_id = scene.GetSelectedPhysicsObject();
    if (phys_id != PhObjectIdNull) {
        PhObject* phys = scene.GetPhysicsObject(phys_id);
        phys->Teleport(phys->GetPosition() + (movement_vector * Vec3f(0.05)), phys->GetRotation());

        Vec3f target = phys->GetPosition();
        pCamera->MoveTo(target + Vec3f(0, 10, -10));
        pCamera->Target = target;
        pCamera->bLookatTarget = true;

        pCamera->Update();
    }
}

void EditorModeMoveCollider::OnLeave(const Scene& scene) {}

void EditorModeScaleCollider::Update(const Scene& scene, const Vec3f& movement_vector)
{
    PhObjectId phys_id = scene.GetSelectedPhysicsObject();
    if (phys_id != PhObjectIdNull) {
        PhObject* phys = scene.GetPhysicsObject(phys_id);
        phys->Dimensions = phys->Dimensions + (movement_vector * Vec3f(0.05));
        if (phys->Dimensions.X < 0.01) {
            phys->Dimensions.X = 0.01;
        }
        if (phys->Dimensions.Y < 0.01) {
            phys->Dimensions.Y = 0.01;
        }
        if (phys->Dimensions.Z < 0.01) {
            phys->Dimensions.Z = 0.01;
        }

        Vec3f target = phys->GetPosition();
        pCamera->MoveTo(target + Vec3f(0, 10, -10));
        pCamera->Target = target;
        pCamera->bLookatTarget = true;

        pCamera->Update();
    }
}

void EditorModeScaleCollider::OnLeave(const Scene& scene)
{
    PhObjectId phys_id = scene.GetSelectedPhysicsObject();
    if (phys_id != PhObjectIdNull) {
        PhObject* phys = scene.GetPhysicsObject(phys_id);
        phys->CreatePrimitiveBody(phys->PrimitiveType, phys->Dimensions, phys->mMotionType, {});
    }
}


} // namespace fx
