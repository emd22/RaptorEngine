#include "FoxtrotGame.hpp"

#define SDL_DISABLE_OLD_NAMES

#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>

#include <Asset/AxManager.hpp>
#include <Asset/FxConfigFile.hpp>
#include <Core/FxDefer.hpp>
#include <Core/FxPanic.hpp>
#include <Core/FxRef.hpp>
#include <FxControls.hpp>
#include <FxEngine.hpp>
#include <FxMaterial.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <csignal>

FX_SET_MODULE_NAME("FoxtrotGame");

static constexpr float scMouseSensitivity = 0.0005;

static bool sbRunning = true;

FoxtrotGame::FoxtrotGame()
{
    InitEngine();
    CreateGame();
}

void FoxtrotGame::InitEngine()
{
#ifdef FX_LOG_OUTPUT_TO_FILE
    FxLogCreateFile("FoxtrotLog.log");
#endif

    FxConfigFile config;
    config.Load(FX_BASE_DIR "/Config/Main.conf");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxModulePanic("Could not initialize SDL! (SDL err: {})\n", SDL_GetError());
    }

    // Create the global engine variables
    FxEngineGlobalsInit();

    FxControlManager::Init();
    FxControlManager::GetInstance().OnQuit = [] { sbRunning = false; };

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT,
           [](int signum)
           {
               FxLogError("Aborted!");
               exit(1);
           });

    const uint32 window_width = config.GetValue<uint32>("Width");
    const uint32 window_height = config.GetValue<uint32>("Height");

    FxRef<FxWindow> window = FxWindow::New(config.GetValue<const char*>("WindowTitle"),
                                           FxVec2i(window_width, window_height));

    gRenderer->SelectWindow(window);
    gRenderer->Init(FxVec2u(window_width, window_height));

    gPhysics->Create();

    AxManager& asset_manager = AxManager::GetInstance();
    asset_manager.Start(2);

    FxMaterialManager& material_manager = FxMaterialManager::GetGlobalManager();
    material_manager.Create();
}

void FoxtrotGame::CreateLights()
{
    constexpr float32 scNumLightsX = 4;
    constexpr float32 scNumLightsY = 4;

    constexpr float32 scAreaX = 20.0f;
    constexpr float32 scAreaY = 20.0f;

    constexpr float32 scDistanceX = scAreaX / scNumLightsX;
    constexpr float32 scDistanceY = scAreaY / scNumLightsY;

    constexpr float32 scOffsetX = scAreaX / 2.0f;
    constexpr float32 scOffsetY = scAreaY / 2.0f;

    constexpr float32 scHeight = 5.0f;

    auto light_volume = FxMeshGen::MakeIcoSphere(2);


    for (int y = 0; y < scNumLightsY; y++) {
        for (int x = 0; x < scNumLightsX; x++) {
            FxRef<FxLight> light = FxMakeRef<FxLight>();
            FxVec3f position = FxVec3f((scDistanceX * x) - scOffsetX, scHeight, (scDistanceY * y) - scOffsetY);
            light->MoveTo(position);
            light->SetLightVolume(light_volume, false);
            light->Color = FxColor(0x76a6a6FF);

            light->Scale(FxVec3f(25));

            mMainScene.Attach(light);
        }
    }
}

void FoxtrotGame::CreateGame()
{
    Player.Create();

    // CreateSkybox();

    Player.pCamera->SetAspectRatio(gRenderer->GetWindow()->GetAspectRatio());
    // Move the player up and behind the other objects
    Player.TeleportBy(FxVec3f(0.0f, 4.0f, -5.0f));

    mMainScene.SelectCamera(Player.pCamera);

    FxRef<FxObject> ground_object = FxAssetManager::LoadObject(FX_BASE_DIR "/Models/Platform.glb",
                                                               { .KeepInMemory = true });
    ground_object->WaitUntilLoaded();

    ground_object->PhysicsObjectCreate(static_cast<PhObject::PhysicsFlags>(PhObject::PF_CreateInactive),
                                       PhObject::PhysicsType::Static, {});
    mMainScene.Attach(ground_object);

    pHelmetObject = FxAssetManager::LoadObject(FX_BASE_DIR "/Models/BrickTest.glb", { .KeepInMemory = true });
    // pHelmetObject->RotateX(M_PI_2);
    // pHelmetObject->Scale(FxVec3f(0.5));
    pHelmetObject->WaitUntilLoaded();
    pHelmetObject->MoveBy(FxVec3f(0, 0, 3.5));

    pHelmetObject->PhysicsObjectCreate(static_cast<PhObject::PhysicsFlags>(0), PhObject::PhysicsType::Static, {});

    gPhysics->OptimizeBroadPhase();


    mMainScene.Attach(pHelmetObject);


    pPistolObject = FxAssetManager::LoadObject(FX_BASE_DIR "/Models/PistolTextured.glb", { .KeepInMemory = true });
    pPistolObject->WaitUntilLoaded();

    pPistolObject->SetObjectLayer(FxObjectLayer::ePlayerLayer);

    PistolRotationGoal = pPistolObject->mRotation;

    mMainScene.Attach(pPistolObject);

    CreateLights();


    while (sbRunning) {
        Tick();
    }
}


static FX_FORCE_INLINE FxVec3f GetMovementVector()
{
    FxVec3f movement = FxVec3f::sZero;

    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_W)) {
        movement.Z = 1.0f;
    }
    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_S)) {
        movement.Z = -1.0f;
    }
    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_A)) {
        movement.X = 1.0f;
    }
    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_D)) {
        movement.X = -1.0f;
    }

    return movement;
}

void FoxtrotGame::ProcessControls()
{
    if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_Q)) {
        sbRunning = false;
    }

    // Click to lock mouse
    if (FxControlManager::IsKeyPressed(FxKey::FX_MOUSE_LEFT) && !FxControlManager::IsMouseLocked()) {
        FxControlManager::CaptureMouse();
    }
    // Escape to unlock mouse
    else if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_ESCAPE) && FxControlManager::IsMouseLocked()) {
        FxControlManager::ReleaseMouse();
    }

    if (FxControlManager::IsMouseLocked()) {
        FxVec2f mouse_delta = FxControlManager::GetMouseDelta();
        mouse_delta.X = (DeltaTime * mouse_delta.X * scMouseSensitivity);
        mouse_delta.Y = (DeltaTime * mouse_delta.Y * -scMouseSensitivity);


        // camera->Rotate(mouse_delta.GetX(), mouse_delta.GetY());
        Player.RotateHead(mouse_delta);
    }

    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_LSHIFT)) {
        Player.bIsSprinting = true;
    }
    else {
        Player.bIsSprinting = false;
    }

    if (FxControlManager::IsKeyDown(FxKey::FX_KEY_SPACE)) {
        Player.Jump();
    }

    if (FxControlManager::IsKeyPressed(FX_KEY_EQUALS)) {
        gRenderer->pDeferredRenderer->ToggleWireframe(true);
    }
    if (FxControlManager::IsKeyPressed(FX_KEY_MINUS)) {
        gRenderer->pDeferredRenderer->ToggleWireframe(false);
    }


    if (FxControlManager::IsKeyPressed(FX_KEY_O)) {
        // Print out memory pool statistics
        FxLogInfo("=== Memory Pool Stats ====");

        FxMemPool& global_pool = FxMemPool::GetGlobalPool();

        const double total_used = static_cast<double>(global_pool.GetTotalUsed()) / FxUnitMebibyte;
        const double total_capacity = static_cast<double>(global_pool.GetTotalCapacity()) / FxUnitMebibyte;

        float usage_percent = (total_used / total_capacity) * 100.0f;
        FxLogInfo("Usage: {:.02f} MiB / {:.02f} MiB ({:.02f}% In Use)", total_used, total_capacity, usage_percent);

        const FxMemPoolStatistics& stats = global_pool.GetStatistics();

        FxLogInfo("Peak Used: {:.02f} MiB", (static_cast<double>(stats.BytesAllocatedPeak) / FxUnitMebibyte));
        FxLogInfo("");
        FxLogInfo("Total Allocs: {}", stats.TotalAllocs);
        FxLogInfo("Total Frees:  {}", stats.TotalFrees);

        FxLogInfo("");
    }

    if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_R)) {
        FxLogInfo("Recompiling shaders...");
        FxShaderCompiler::CompileAllShaders(FX_BASE_DIR "/Shaders/");
    }

    if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_P)) {
        pHelmetObject->SetPhysicsEnabled(!pHelmetObject->GetPhysicsEnabled());
    }
}

void FoxtrotGame::Tick()
{
    const uint64 current_tick = SDL_GetTicksNS();
    DeltaTime = static_cast<float>(current_tick - mLastTick) / 1'000'000.0f;

    FxControlManager::Update();
    ProcessControls();

    Player.Move(DeltaTime, GetMovementVector());
    Player.Update(DeltaTime);

    FxRef<FxPerspectiveCamera> camera = Player.pCamera;

    PistolRotationGoal = FxQuat::FromEulerAngles(FxVec3f(-camera->mAngleY, camera->mAngleX, 0));

    pPistolObject->mRotation.LerpIP(PistolRotationGoal, 0.06 * DeltaTime);

    pPistolObject->MoveTo(camera->Position + (camera->Direction * FxVec3f(0.45)) -
                          camera->GetRightVector() * FxVec3f(0.18) - camera->GetUpVector() * FxVec3f(0.15));
    // pPistolObject->MoveBy();

    gPhysics->Update();

    if (gRenderer->BeginFrame() != RxFrameResult::Success) {
        return;
    }

    // deferred_renderer->SkyboxRenderer.Render(gRenderer->GetFrame()->CommandBuffer, *camera);

    mMainScene.Render();

    gRenderer->DoComposition(*camera);

    mLastTick = current_tick;
}

void FoxtrotGame::CreateSkybox()
{
    // // Load the cubemap as a 2d image
    // FxRef<FxAssetImage> skybox_texture = FxAssetManager::LoadImage(RxImageType::Image, VK_FORMAT_R8G8B8A8_SRGB,
    //                                                                "../Textures/TestCubemap.png");
    // skybox_texture->WaitUntilLoaded();

    // // Create the layers 2d image to use in the renderer
    // RxImage cubemap_image;
    // cubemap_image.CreateLayeredImageFromCubemap(skybox_texture->Texture.Image, VK_FORMAT_R8G8B8A8_SRGB);

    // auto generated_cube = FxMeshGen::MakeCube({ .Scale = 5 });

    // for (int i = 0; i < RendererFramesInFlight; i++) {
    //     RxImage& skybox_output_image = gRenderer->Swapchain.OutputImages[i];
    //     gRenderer->pDeferredRenderer->SkyboxRenderer.SkyboxAttachments.Insert(&skybox_output_image);
    // }

    // gRenderer->pDeferredRenderer->SkyboxRenderer.SkyAttachment = &cubemap_image;

    // pSkyboxMesh = generated_cube->AsPositionsMesh();
    // gRenderer->pDeferredRenderer->SkyboxRenderer.Create(gRenderer->Swapchain.Extent, pSkyboxMesh);

    // pSkyboxMesh->IsReference = false;
}

void FoxtrotGame::DestroyGame()
{
    gRenderer->GetDevice()->WaitForIdle();

    // FxMaterialManager::GetGlobalManager().Destroy();
    FxMaterialManager::GetGlobalManager().Destroy();
    AxManager::GetInstance().Shutdown();

    // pSkyboxMesh->IsReference = false;
    // pSkyboxMesh->Destroy();

    // gRenderer->pDeferredRenderer->SkyboxRenderer.Destroy();

    gRenderer->pDeferredRenderer->Destroy();
}


FoxtrotGame::~FoxtrotGame()
{
    DestroyGame();
    mMainScene.Destroy();
}
