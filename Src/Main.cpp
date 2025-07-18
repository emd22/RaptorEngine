
#include "vulkan/vulkan_core.h"
#define VMA_DEBUG_LOG(...) Log::Warning(__VA_ARGS__)

#include <ThirdParty/vk_mem_alloc.h>

#include "Core/Defines.hpp"
#include <Renderer/Backend/RvkFrameData.hpp>
#include "Renderer/Backend/ShaderList.hpp"
#include "Renderer/Backend/RvkShader.hpp"

#include "Renderer/Renderer.hpp"
#include "Renderer/FxLight.hpp"
#include "Renderer/FxCamera.hpp"

#include <Core/FxMemory.hpp>
#include <Core/FxLinkedList.hpp>

#define SDL_DISABLE_OLD_NAMES
#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>
#include <SDL3/SDL_main.h>

#include <Core/Types.hpp>

#include <Renderer/Renderer.hpp>
#include <Renderer/FxWindow.hpp>
#include <Renderer/FxMesh.hpp>
#include <Renderer/FxDeferred.hpp>

#include <Asset/FxAssetManager.hpp>
#include "FxControls.hpp"
#include <Math/Mat4.hpp>
#include <Asset/FxConfigFile.hpp>

#include "FxMaterial.hpp"
#include "FxEntity.hpp"

#include <Asset/FxMeshGen.hpp>

#include <csignal>

FX_SET_MODULE_NAME("Main")

static bool Running = true;

static uint64 LastTick = 0;
static float DeltaTime = 1.0f;

void CheckGeneralControls()
{
    if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_Q)) {
        Running = false;
    }

    // Click to lock mouse
    if (FxControlManager::IsKeyPressed(FxKey::FX_MOUSE_LEFT) && !FxControlManager::IsMouseLocked()) {
        FxControlManager::CaptureMouse();
    }
    // Escape to unlock mouse
    else if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_ESCAPE) && FxControlManager::IsMouseLocked()) {
        FxControlManager::ReleaseMouse();
    }
}

static FxPerspectiveCamera* current_camera = nullptr;

class TestScript : public FxScript
{
public:
    TestScript()
    {
    }

    void RenderTick() override
    {
        Entity->mModelMatrix.LookAt(Entity->GetPosition(), current_camera->Position, FxVec3f::Up);
    }

    ~TestScript() override
    {
    }
};

int main()
{
    FxMemPool::GetGlobalPool().Create(100000);

    FxConfigFile config;
    config.Load("../Config/Main.conf");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxModulePanic("Could not initialize SDL! (SDL err: %s)\n", SDL_GetError());
    }

    FxControlManager::Init();
    FxControlManager::GetInstance().OnQuit = [] { Running = false; };

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT, [](int signum) {
        Log::Error("Aborted!");
        exit(1);
    });

    const uint32 window_width = config.GetValue<uint32>("Width");
    const uint32 window_height = config.GetValue<uint32>("Height");

    FxRef<FxWindow> window = FxWindow::New(config.GetValue<const char*>("WindowTitle"), window_width, window_height);

    FxRenderBackend renderer_state;
    SetRendererBackend(&renderer_state);

    Renderer->SelectWindow(window);
    Renderer->Init(FxVec2u(window_width, window_height));

    Renderer->Swapchain.CreateSwapchainFramebuffers();

    // Renderer->OffscreenSemaphore.Create(Renderer->GetDevice());

    FxRef<FxDeferredRenderer> deferred_renderer = FxMakeRef<FxDeferredRenderer>();
    deferred_renderer->Create(Renderer->Swapchain.Extent);
    Renderer->DeferredRenderer = deferred_renderer;

    FxAssetManager& asset_manager = FxAssetManager::GetInstance();
    FxMaterialManager& material_manager = FxMaterialManager::GetGlobalManager();

    asset_manager.Start(2);
    material_manager.Create();

    // PtrContainer<FxModel> test_model = FxAssetManager::LoadModel("../models/Box.glb");
    // PtrContainer<FxModel> new_model = FxAssetManager::LoadModel("../models/Box.glb");
    FxPerspectiveCamera camera;
    current_camera = &camera;

    // FxRef<FxModel> other_model = FxAssetManager::LoadAsset<FxModel>("../models/Cube.glb");
    FxRef<FxModel> helmet_model = FxAssetManager::LoadAsset<FxModel>("../models/DamagedHelmet.glb");

    // FxRef<FxImage> test_image = FxAssetManager::LoadAsset<FxImage>("../textures/squid.jpg");
    FxRef<FxImage> cheese_image = FxAssetManager::LoadAsset<FxImage>("../textures/cheese.jpg");

    // FxRef<FxMaterial> material = FxMaterialManager::New("Default");
    // material->Attach(FxMaterial::ResourceType::Diffuse, test_image);
    // material->Build(&pipeline);
    //
    helmet_model->WaitUntilLoaded();

    FxRef<FxMaterial> cheese_material = FxMaterialManager::New("Cheese", &deferred_renderer->GPassPipeline);
    cheese_material->Attach(FxMaterial::ResourceType::Diffuse, cheese_image);

    // FxSceneObject scene_object;
    // scene_object.Attach(other_model);
    // scene_object.Attach(material);

    FxSceneObject helmet_object;
    helmet_object.Attach(helmet_model);

    if (helmet_model->Materials.size() > 0) {
        FxRef<FxMaterial>& helmet_material = helmet_model->Materials.at(0);
        helmet_material->Pipeline = &deferred_renderer->GPassPipeline;

        helmet_object.Attach(helmet_material);
    }
    else {
        helmet_object.Attach(cheese_material);
    }

    helmet_object.Translate(FxVec3f(-3, 0, 0));

    auto generated_sphere = FxMeshGen::MakeIcoSphere(1);

    camera.SetAspectRatio(((float32)window_width) / (float32)window_height);

    camera.Position.Z += 5.0f;

    // Mat4f model_matrix = Mat4f::AsTranslation(FxVec3f(0, 0, 0));

    FxSizedArray<VkDescriptorSet> sets_to_bind;
    sets_to_bind.InitSize(2);

    FxLight light;
    light.SetLightVolume(generated_sphere, true);

    light.Scale(FxVec3f(2, 2, 2));

    while (Running) {
        const uint64 CurrentTick = SDL_GetTicksNS();

        DeltaTime = static_cast<float>(CurrentTick - LastTick) / 1'000'000.0;

        FxControlManager::Update();

        if (FxControlManager::IsMouseLocked()) {
            FxVec2f mouse_delta = FxControlManager::GetMouseDelta();
            mouse_delta.SetX(-0.001 * mouse_delta.GetX() * DeltaTime);
            mouse_delta.SetY(-0.001 * mouse_delta.GetY() * DeltaTime);

            camera.Rotate(mouse_delta.GetX(), mouse_delta.GetY());
        }

        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_W)) {
            camera.Move(FxVec3f(0.0f, 0.0f, 0.01f * DeltaTime));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_S)) {
            camera.Move(FxVec3f(0.0f, 0.0f, -0.01f * DeltaTime));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_A)) {
            camera.Move(FxVec3f(0.01f * DeltaTime, 0.0f, 0.0f));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_D)) {
            camera.Move(FxVec3f(-0.01f * DeltaTime, 0.0f, 0.0f));
        }

        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_R)) {
            // camera.Move(FxVec3f(-0.01f * DeltaTime, 0.0f, 0.0f));
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_P)) {

            helmet_object.mModelMatrix.Print();
            // FxMemPool::GetGlobalPool().PrintAllocations();
        }

        CheckGeneralControls();

        camera.Update();

        if (Renderer->BeginFrame(*deferred_renderer) != FrameResult::Success) {
            continue;
        }

        helmet_object.Render(camera);
        // light.RenderDebugMesh(camera);


        Renderer->BeginLighting();

        light.Render(camera);

        Renderer->DoComposition();

        LastTick = CurrentTick;
    }

    Renderer->GetDevice()->WaitForIdle();

    material_manager.Destroy();
    asset_manager.Shutdown();

    deferred_renderer->Destroy();

    // composition_pipeline.Destroy();

    std::cout << "this thread: " << std::this_thread::get_id() << std::endl;

    return 0;
}
