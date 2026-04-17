#include "FoxtrotGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AxManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/SceneFile.hpp>
#include <Controls.hpp>
#include <Core/Defer.hpp>
#include <Core/Panic.hpp>
#include <Core/Ref.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Material.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Globals.hpp>
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

    ConfigEntry* window_entry = Config.GetEntry(HashStr64("Window"));

    const uint32 window_width = window_entry->GetMember(HashStr64("Width"))->Get<uint32>();
    const uint32 window_height = window_entry->GetMember(HashStr64("Height"))->Get<uint32>();

    Ref<Window> window = Window::New(window_entry->GetMember(HashStr64("Title"))->Get<const char*>(),
                                     Vec2i(window_width, window_height));

    gRenderer->SelectWindow(window);
    gRenderer->Init(Vec2u(window_width, window_height));

    gPhysics->Create();

    gAssetManager->Start(3);

    gMaterialManager->Create();

    sClockFreq = static_cast<double>(SDL_GetPerformanceFrequency());
}

void FoxtrotGame::CreateLights()
{
    // pSun = MakeRef<LightDirectional>();
    // pSun->MoveTo(Vec3f(0.5, 5, -1.0).Normalize());
    // pSun->Color = Color::FromRGBA(0xFA, 0xD2, 0xC0, 6);
    // pSun->AmbientColor = Color::FromRGBA(0x4A, 0x3A, 0x2A, 1);
    // mMainScene.Attach(pSun);
}

void FoxtrotGame::LoadOffsetsFile()
{
    ConfigFile info;

    info.Load(FX_BASE_DIR "/Config/Offsets.conf");

    PistolOffset = info.GetEntryValue(HashStr64("PistolOffset"), Vec3f::sZero);
    ArmsOffset = info.GetEntryValue(HashStr64("ArmsOffset"), Vec3f::sZero);
}

void FoxtrotGame::CreateGame()
{
    mMainScene.Create();

    Player.Create();
    Player.pCamera->SetAspectRatio(gRenderer->GetWindow()->GetAspectRatio());
    // Move the player up and behind the other objects
    Player.TeleportTo(Vec3f(0.0f, 4.0f, -4.0f));
    Player.SetFlyMode(true);

    mMainScene.SelectCamera(Player.pCamera);

    SceneFile scene_file;

    const char* scene_to_load = Config.GetEntry(HashStr64("Scene"))->Get<const char*>();

    scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, scene_to_load), mMainScene);
    gPhysics->OptimizeBroadPhase();

    pSun = mMainScene.GetDirectionalLight();

    pPistolObject = mMainScene.FindObject(HashStr64("Pistol"));
    pArmsObject = mMainScene.FindObject(HashStr64("AnimTest"));

    LoadOffsetsFile();

    CreateLights();

    gShadowRenderer = new ShadowDirectional(Vec2u(2048, 2048));
    gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(Vec3f(0, 8, 5), Vec3f(0.0f, 8.0f, -2.0f), Vec3f(0, 1, 0));
    gShadowRenderer->ShadowCamera.SetFarPlane(200.0f);
    gShadowRenderer->ShadowCamera.SetNearPlane(0.1f);
    gShadowRenderer->ShadowCamera.UpdateProjectionMatrix();
    gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;
    gShadowRenderer->ShadowCamera.UpdateCameraMatrix();

    while (sbRunning) {
        Tick();
    }
}


static FX_FORCE_INLINE Vec3f GetMovementVector()
{
    Vec3f movement = Vec3f::sZero;

    if (ControlManager::IsKeyDown(Key::FX_KEY_W)) {
        movement.Z += 1.0f;
    }
    if (ControlManager::IsKeyDown(Key::FX_KEY_S)) {
        movement.Z += -1.0f;
    }
    if (ControlManager::IsKeyDown(Key::FX_KEY_A)) {
        movement.X += 1.0f;
    }
    if (ControlManager::IsKeyDown(Key::FX_KEY_D)) {
        movement.X += -1.0f;
    }

    return movement;
}

void FoxtrotGame::ProcessControls()
{
    if (ControlManager::IsKeyPressed(Key::FX_KEY_GRAVE)) {
        // Release the mouse before quitting the game incase there is a crash.
        ControlManager::ReleaseMouse();
        sbRunning = false;
    }

    // Click to lock mouse
    if (ControlManager::IsKeyPressed(Key::FX_MOUSE_LEFT) && !ControlManager::IsMouseLocked()) {
        ControlManager::CaptureMouse();
    }
    // Escape to unlock mouse
    else if (ControlManager::IsKeyPressed(Key::FX_KEY_ESCAPE) && ControlManager::IsMouseLocked()) {
        ControlManager::ReleaseMouse();
    }

    if (ControlManager::IsKeyPressed(Key::FX_KEY_8)) {
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


    if (ControlManager::IsKeyDown(Key::FX_KEY_SPACE)) {
        if (!Player.IsFlyMode()) {
            Player.Jump();
        }
    }

    // Elevate up
    if (ControlManager::IsKeyDown(Key::FX_KEY_E)) {
        Player.Move(DeltaTime, Vec3f::sUp);
    }
    // Elevate down
    if (ControlManager::IsKeyDown(Key::FX_KEY_Q)) {
        Player.Move(DeltaTime, -Vec3f::sUp);
    }


    if (ControlManager::IsKeyDown(Key::FX_KEY_LSHIFT)) {
        Player.bIsSprinting = true;
    }
    else {
        Player.bIsSprinting = false;
    }

    if (ControlManager::IsComboDown(Key::FX_KEY_LSHIFT, Key::FX_KEY_R)) {
        // Reload the object properties from the scene
        SceneFile scene_file;
        scene_file.Load(std::format("{}/Data/{}", FX_BASE_DIR, Config.GetEntry(HashStr64("Scene"))->Get<const char*>()),
                        mMainScene);

        LoadOffsetsFile();
    }

    if (ControlManager::IsKeyPressed(Key::FX_KEY_P)) {
        pHelmetObject->SetPhysicsEnabled(!pHelmetObject->GetPhysicsEnabled());
    }

    if (ControlManager::IsKeyPressed(Key::FX_KEY_0)) {
        Player.SetFlyMode(true);
        Player.TeleportTo(Vec3f(0.0f, 4.0f, -4.0f));
    }


    if (ControlManager::IsKeyPressed(Key::FX_KEY_N)) {
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


    if (!sbShowShadowCam) {
        Player.Move(DeltaTime, GetMovementVector());
        Player.Update(DeltaTime);
    }

    Ref<PerspectiveCamera> camera = Player.pCamera;

    // Vec3f pistol_destination = camera->Position + (camera->Direction * Vec3f(0.48)) -
    //                              camera->GetRightVector() * Vec3f(0.165) - camera->GetUpVector() *
    //                              Vec3f(0.15);


    pArmsObject->SetRotationOrigin(ArmsOffset);

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

        pArmsObject->UpdateIfOutOfDate();

        pPistolObject->SetRotationOrigin(ArmsOffset + hand_transform.Position + PistolOffset);
        pPistolObject->SetPosition(pArmsObject->mPosition + hand_transform.Position + PistolOffset);

        LogInfo("{}", hand_transform.Position);
        pPistolObject->mRotation = PistolRotationGoal;
        pPistolObject->UpdateIfOutOfDate();
        // pPistolObject->SetRotationOrigin(pistol_destination);
    }

    // pPistolObject->SetRotationOrigin(ArmsOffset);


    // pPistolObject->mRotation.SmoothInterpolate(PistolRotationGoal, 40.0, DeltaTime);
    // pArmsObject->mRotation.SmoothInterpolate(PistolRotationGoal, 40.0, DeltaTime);
    // pArmsObject->mRotation = PistolRotationGoal;


    gShadowRenderer->ShadowCamera.Position = (Player.Position + (pSun->GetPosition().Normalize() * 15.0f));

    Vec3f target = Player.Position;

    gShadowRenderer->ShadowCamera.ViewMatrix.LookAt(gShadowRenderer->ShadowCamera.Position, target, Vec3f(0, 1, 0));
    // LogInfo("{}", gShadowRenderer->ShadowCamera.ViewMatrix.Columns[3]);
    gShadowRenderer->ShadowCamera.UpdateCameraMatrix();
    gShadowRenderer->ShadowCamera.mbRequireMatrixUpdate = false;

    if (gRenderer->BeginFrame() != FrameResult::Success) {
        mLastTick = current_tick;
        return;
    }

    gPhysics->Update();

    FrameData* frame = gRenderer->GetFrame();

    frame->CommandBuffer.Reset();
    frame->CommandBuffer.Record();

    mMainScene.RenderShadows(&gShadowRenderer->ShadowCamera);
    mMainScene.Render(&gShadowRenderer->ShadowCamera);

    mLastTick = current_tick;
}

void FoxtrotGame::DestroyGame()
{
    gRenderer->GetDevice()->WaitForIdle();

    gMaterialManager->Destroy();

    delete gShadowRenderer;
    gShadowRenderer = nullptr;

    gAssetManager->Shutdown();

    gRenderer->pDeferredRenderer.DestroyRef();
}


FoxtrotGame::~FoxtrotGame()
{
    DestroyGame();
    mMainScene.Destroy();
}

} // namespace fx
