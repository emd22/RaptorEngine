
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "Core/FxDefines.hpp"
#include "Renderer/Backend/ShaderList.hpp"
#include "Renderer/FxCamera.hpp"
#include "Renderer/FxLight.hpp"
#include "Renderer/Renderer.hpp"


/* Vulkan Memory Allocator */
#include <ThirdParty/vk_mem_alloc.h>

#include <Core/FxLinkedList.hpp>
#include <Core/FxMemory.hpp>
#include <FxEngine.hpp>
#include <Renderer/Backend/RxFrameData.hpp>
#include <Renderer/Backend/RxShader.hpp>

#define SDL_DISABLE_OLD_NAMES
#include "FxControls.hpp"
#include "FxEntity.hpp"
#include "FxMaterial.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_revision.h>

#include <Asset/FxAssetManager.hpp>
#include <Asset/FxConfigFile.hpp>
#include <Asset/FxMeshGen.hpp>
#include <Core/FxBitset.hpp>
#include <Core/FxLog.hpp>
#include <Core/FxTypes.hpp>
#include <FxObject.hpp>
#include <Math/Mat4.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>
#include <Renderer/FxWindow.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Script/FxScript.hpp>
#include <chrono>
#include <csignal>
#include <iostream>


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

// #pragma clang optimize off

// template <typename FuncType>
// void TestSpeed(FuncType ft, int iterations)
// {
//     using namespace std::chrono;
//     uint64 time_in_ns = 0;

//     auto t1 = high_resolution_clock::now();
//     for (int32 i = 0; i < iterations; i++) {
//         ft(i);
//     }
//     auto t2 = high_resolution_clock::now();
//     auto ns = duration_cast<nanoseconds>(t2 - t1);
//     time_in_ns = ns.count() / iterations;

//     std::cout << "Function " << typeid(FuncType).name() << " Took " << time_in_ns << "ns.\n";
// }

// #pragma clang optimize on


void TestScript()
{
    FxConfigScript script;
    script.LoadFile("../Scripts/Default.fxS");

    // Define an external variable that can be used in our script
    // script.DefineExternalVar("int", "eExternalVar", FxScriptValue(FxScriptValue::INT, 42));

    FxScriptVM vm;

    script.Execute(vm);


    // std::string command = "";

    // printf(
    //     "\nFoxtrot Script\nVersion %d.%d.%d\n",
    //     FX_SCRIPT_VERSION_MAJOR,
    //     FX_SCRIPT_VERSION_MINOR,
    //     FX_SCRIPT_VERSION_PATCH
    // );

    // while (true) {
    //     printf(" -> ");
    //     fflush(stdout);

    //     std::getline(std::cin, command);

    //     if (command == "quit") {
    //         break;
    //     }

    //     if (!command.ends_with(';')) {
    //         command += ';';
    //     }

    //     //printf("Executing command {%s}\n", command.c_str());

    //     script.ExecuteUserCommand(command.c_str(), interpreter);
    // }
}


int main()
{
#ifdef FX_LOG_OUTPUT_TO_FILE
    FxLogCreateFile("FoxtrotLog.log");
#endif

    FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);

    // TestScript();
    // return 0;

    FxConfigFile config;
    config.Load("../Config/Main.conf");


    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxModulePanic("Could not initialize SDL! (SDL err: {})\n", SDL_GetError());
    }

    FxEngineGlobalsInit();

    FxControlManager::Init();
    FxControlManager::GetInstance().OnQuit = [] { Running = false; };

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT,
           [](int signum)
           {
               FxLogError("Aborted!");
               exit(1);
           });

    const uint32 window_width = config.GetValue<uint32>("Width");
    const uint32 window_height = config.GetValue<uint32>("Height");

    FxRef<FxWindow> window = FxWindow::New(config.GetValue<const char*>("WindowTitle"), window_width, window_height);

    // RxRenderBackend renderer_state;
    // SetRendererBackend(&renderer_state);

    gRenderer->SelectWindow(window);
    gRenderer->Init(FxVec2u(window_width, window_height));

    gRenderer->Swapchain.CreateSwapchainFramebuffers();

    // gRenderer->OffscreenSemaphore.Create(gRenderer->GetDevice());

    FxRef<RxDeferredRenderer> deferred_renderer = FxMakeRef<RxDeferredRenderer>();
    deferred_renderer->Create(gRenderer->Swapchain.Extent);
    gRenderer->DeferredRenderer = deferred_renderer;

    FxAssetManager& asset_manager = FxAssetManager::GetInstance();
    FxMaterialManager& material_manager = FxMaterialManager::GetGlobalManager();

    asset_manager.Start(2);
    material_manager.Create();

    FxPerspectiveCamera camera;
    current_camera = &camera;

    FxRef<FxObject> fireplace_object = FxAssetManager::LoadObject("../models/FireplaceRoom.glb");
    fireplace_object->WaitUntilLoaded();

    FxRef<FxAssetImage> skybox_texture = FxAssetManager::LoadImage(RxImageType::Image, "../Textures/TestCubemap.png");
    skybox_texture->WaitUntilLoaded();

    RxImage cubemap_image;
    cubemap_image.CreateLayeredImageFromCubemap(skybox_texture->Texture.Image, VK_FORMAT_R8G8B8A8_SRGB);

    auto generated_cube = FxMeshGen::MakeCube({ .Scale = 5 });

    auto generated_quad = FxMeshGen::MakeQuad({ .Scale = 1 });

    for (int i = 0; i < RendererFramesInFlight; i++) {
        RxImage& skybox_output_image = gRenderer->Swapchain.OutputImages[i];
        gRenderer->DeferredRenderer->SkyboxRenderer.SkyboxAttachments.Insert(&skybox_output_image);
    }

    gRenderer->DeferredRenderer->SkyboxRenderer.SkyAttachment = &cubemap_image;

    gRenderer->DeferredRenderer->SkyboxRenderer.Create(gRenderer->Swapchain.Extent, generated_cube->AsLightVolume());


    auto generated_sphere = FxMeshGen::MakeIcoSphere(2);

    camera.SetAspectRatio(((float32)window_width) / (float32)window_height);

    camera.Position.Z += 5.0f;
    camera.Position.Y += 4.0f;

    // Mat4f model_matrix = Mat4f::AsTranslation(FxVec3f(0, 0, 0));


    fireplace_object->RotateX(M_PI_2);

    FxSizedArray<VkDescriptorSet> sets_to_bind;
    sets_to_bind.InitSize(2);

    FxLight light;
    light.SetLightVolume(generated_sphere, true);

    light.MoveBy(FxVec3f(0.0, 2.80, 1.20));
    light.Scale(FxVec3f(25));

    light.Color = FxVec3f(0.6, 0.7, 0.6);

    FxDirectionalLight light2;
    light2.SetLightVolume(generated_quad, false);

    light2.MoveBy(FxVec3f(1, 0, -0.5));
    // light2.Scale(FxVec3f(25));

    bool second_light_on = false;


    while (Running) {
        const uint64 CurrentTick = SDL_GetTicksNS();

        DeltaTime = static_cast<float>(CurrentTick - LastTick) / 1'000'000.0;

        FxControlManager::Update();

        if (FxControlManager::IsMouseLocked()) {
            FxVec2f mouse_delta = FxControlManager::GetMouseDelta();
            mouse_delta.SetX(DeltaTime * mouse_delta.GetX() * -0.001);
            mouse_delta.SetY(DeltaTime * mouse_delta.GetY() * 0.001);

            camera.Rotate(mouse_delta.GetX(), mouse_delta.GetY());
        }

        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_W)) {
            camera.Move(FxVec3f(0.0f, 0.0f, DeltaTime * 0.01f));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_S)) {
            camera.Move(FxVec3f(0.0f, 0.0f, DeltaTime * -0.01f));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_A)) {
            camera.Move(FxVec3f(DeltaTime * 0.01f, 0.0f, 0.0f));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_D)) {
            camera.Move(FxVec3f(DeltaTime * -0.01f, 0.0f, 0.0f));
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_L)) {
            light.MoveTo(camera.Position);

            light.mPosition.Print();
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_Y)) {
            // FxAssetManager::LoadAsset(helmet_model, "../models/FireplaceRoom.glb");

            // helmet_object.Attach(helmet_model);
            // helmet_object.Attach(cheese_material);
            FxMemPool::GetGlobalPool().PrintAllocations();
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_F)) {
            second_light_on = !second_light_on;
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_R)) {
            gRenderer->DeferredRenderer->RebuildLightingPipeline();
        }

        CheckGeneralControls();

        camera.Update();

        //        helmet_object.RotateY(0.001 * DeltaTime);


        if (gRenderer->BeginFrame(*deferred_renderer) != RxFrameResult::Success) {
            continue;
        }

        deferred_renderer->SkyboxRenderer.Render(gRenderer->GetFrame()->CommandBuffer, *current_camera);
        deferred_renderer->GPassPipeline.Bind(gRenderer->GetFrame()->CommandBuffer);

        //         helmet_object.mPosition.X = sin((0.05 * gRenderer->GetElapsedFrameCount())) * 0.01;
        //         helmet_object.Translate(FxVec3f(0, 0, 0));

        fireplace_object->Render(camera);


        gRenderer->BeginLighting();

        if (second_light_on) {
            light2.mModelMatrix = camera.VPMatrix;

            light2.Render(camera);
        }

        light.Render(camera);
        //        light2.Render(camera);
        // light2.Render(camera);

        gRenderer->DoComposition(camera);

        LastTick = CurrentTick;
    }

    gRenderer->GetDevice()->WaitForIdle();

    material_manager.Destroy();
    asset_manager.Shutdown();

    deferred_renderer->Destroy();

    // composition_pipeline.Destroy();

    //    ground_object->Destroy();

    std::cout << "this thread: " << std::this_thread::get_id() << std::endl;

    return 0;
}
