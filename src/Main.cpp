#include "Core/Defines.hpp"
#include "Renderer/Backend/FxRenderBackendVulkan.hpp"
#include "Renderer/Backend/Vulkan/Shader.hpp"
#include "Renderer/Backend/Vulkan/Shader.hpp"
#include "Renderer/Backend/Vulkan/ShaderList.hpp"
#include "Renderer/FxCamera.hpp"
#include "sdl3/3.2.8/include/SDL3/SDL_timer.h"

#define SDL_DISABLE_OLD_NAMES
#include <SDL3/SDL.h>
#include <SDL3/SDL_revision.h>
#include <SDL3/SDL_main.h>

#include <Core/Types.hpp>

#include <Renderer/Renderer.hpp>
#include <Renderer/FxWindow.hpp>
#include <Renderer/FxMesh.hpp>

#include <Asset/FxAssetManager.hpp>

#include "FxControls.hpp"

static bool Running = true;

FX_SET_MODULE_NAME("Main")

#include <csignal>

#include <Math/Mat4.hpp>

static uint64 LastTick = 0;
static float DeltaTime = 1.0f;

void TestMat4Multiply()
{
    Mat4f mat1((Vec4f [4]){
        (Vec4f){1.0, 2.0, 3.0, 4.0},
        (Vec4f){2.0, 3.0, 4.0, 1.0},
        (Vec4f){3.0, 4.0, 1.0, 2.0},
        (Vec4f){4.0, 1.0, 2.0, 3.0}
    });

    Mat4f mat2((Vec4f [4]){
        (Vec4f){1.0, 2.0, 3.0, 4.0},
        (Vec4f){2.0, 3.0, 4.0, 1.0},
        (Vec4f){3.0, 4.0, 1.0, 2.0},
        (Vec4f){4.0, 1.0, 2.0, 3.0}
    });

    Mat4f result = mat1.Multiply(mat2);
    result.Print();
}

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

int main()
{

    // TestMat4Multiply();
    // return 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxPanic("Could not initialize SDL! (SDL err: %s)\n", SDL_GetError());
    }

    FxControlManager::GetInstance().OnQuit = [] { Running = false; };

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT, [](int signum) {
        Log::Error("Aborted!");
        exit(1);
    });

    const int window_width = 1024;
    const int window_height = 720;

    auto window = FxWindow::New("Foxtrot Engine", window_width, window_height);

    vulkan::FxRenderBackendVulkan renderer_state;
    SetRendererBackend(&renderer_state);

    Renderer->SelectWindow(window);
    Renderer->Init(Vec2i(window_width, window_height));

    vulkan::GraphicsPipeline pipeline;

    {
        ShaderList shader_list;

        vulkan::Shader vertex_shader("../shaders/main.vert.spv", ShaderType::Vertex);
        vulkan::Shader fragment_shader("../shaders/main.frag.spv", ShaderType::Fragment);

        shader_list.Vertex = vertex_shader.ShaderModule;
        shader_list.Fragment = fragment_shader.ShaderModule;

        pipeline.Create(shader_list);

        RendererVulkan->Swapchain.CreateSwapchainFramebuffers(&pipeline);
    }

    auto &asset_manager = FxAssetManager::GetInstance();
    asset_manager.Start(2);

    // FxModel *model = new FxModel;
    // loader.LoadFromFile(model, "../models/Box.glb");

    // FxModel *test_model = FxAssetManager::LoadModel("../models/Box.glb");
    PtrContainer<FxModel> test_model = FxAssetManager::LoadModel("../models/Box.glb");

    test_model->OnLoaded =
        [](FxBaseAsset *model) {
            Log::Info("This was loaded!", 0);
        };

    // StaticArray<FxVertex<FxVertexPosition | FxVertexNormal>> test_mesh_verts = {
    //     FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = { -1,   1,  0 } },
    //     FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = {  1,   1,  0 } },
    //     FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = {  0,  -1,  0 } },
    // };

    FxPerspectiveCamera camera;

    camera.SetAspectRatio(((float32)window_width) / (float32)window_height);

    camera.Position.Z += 5.0f;

    // FxMesh test_mesh(test_mesh_verts);
    //
    //
    Mat4f model_matrix = Mat4f::AsTranslation(Vec3f(0, 0, -5));

    // model_matrix.Columns[3].Load4(0, 0, -5, 1);

    while (Running) {
        const uint64 CurrentTick = SDL_GetTicksNS();

        DeltaTime = static_cast<float>(CurrentTick - LastTick) / 1'000'000.0;

        FxControlManager::Update();

        if (FxControlManager::IsMouseLocked()) {
            Vec2f mouse_delta = FxControlManager::GetMouseDelta();
            mouse_delta.SetX(-0.001 * mouse_delta.GetX() * DeltaTime);
            mouse_delta.SetY(-0.001 * mouse_delta.GetY() * DeltaTime);

            camera.Rotate(mouse_delta.GetX(), mouse_delta.GetY());
        }

        camera.Update();


        // Mat4f VPMatrix = camera.ProjectionMatrix.Multiply(camera.ViewMatrix);
        Mat4f VPMatrix = camera.ViewMatrix.Multiply(camera.ProjectionMatrix);
        // Mat4f MVPMatrix = model_matrix.Multiply(VPMatrix);

        if (Renderer->BeginFrame(pipeline, VPMatrix) != FrameResult::Success) {
            continue;
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_F)) {
            Log::Info("View Matrix: ");
            camera.ViewMatrix.Print();
            Log::Info("Projection Matrix: ");
            camera.ProjectionMatrix.Print();
            Log::Info("VP Matrix: ");
            VPMatrix.Print();
        }

        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_S)) {
            camera.Position.Z -= 0.01f * DeltaTime;
            camera.RequireUpdate();
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_W)) {
            camera.Position.Z += 0.01f * DeltaTime;
            camera.RequireUpdate();
        }

        CheckGeneralControls();

        // if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_R) && !test_model->IsLoaded) {
        //     FxAssetManager::LoadModel(test_model, "../models/Box.glb");
        // }

        test_model->Render();
        Renderer->FinishFrame(pipeline);

        LastTick = CurrentTick;
    }

    asset_manager.ShutDown();

    return 0;
}
