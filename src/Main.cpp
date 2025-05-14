#include "Core/Defines.hpp"
#include "Renderer/Constants.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Backend/Vulkan/ShaderList.hpp"
#include <Renderer/Backend/Vulkan/RvkShader.hpp>
#include "Renderer/FxCamera.hpp"
#include "vulkan/vulkan_core.h"

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

FX_SET_MODULE_NAME("Main")

#include <csignal>

#include <Math/Mat4.hpp>

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

int main()
{

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxPanic("Could not initialize SDL! (SDL err: %s)\n", SDL_GetError());
    }

    FxControlManager::GetInstance().OnQuit = [] { Running = false; };

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT, [](int signum) {
        Log::Error("Aborted!");
        exit(1);
    });

    const uint32 window_width = 1024;
    const uint32 window_height = 720;

    auto window = FxWindow::New("Foxtrot Engine", window_width, window_height);

    vulkan::FxRenderBackendVulkan renderer_state;
    SetRendererBackend(&renderer_state);

    Renderer->SelectWindow(window);
    Renderer->Init(Vec2u(window_width, window_height));

    vulkan::RvkGraphicsPipeline pipeline;

    {
        ShaderList shader_list;

        vulkan::RvkShader vertex_shader("../shaders/main.vert.spv", RvkShaderType::Vertex);
        vulkan::RvkShader fragment_shader("../shaders/main.frag.spv", RvkShaderType::Fragment);

        shader_list.Vertex = vertex_shader.ShaderModule;
        shader_list.Fragment = fragment_shader.ShaderModule;

        pipeline.Create(shader_list);

        RendererVulkan->Swapchain.CreateSwapchainFramebuffers(&pipeline);
    }

    RendererVulkan->DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RendererFramesInFlight);
    RendererVulkan->DescriptorPool.Create(RendererVulkan->GetDevice(), RendererFramesInFlight);

    for (RvkFrameData &frame : RendererVulkan->Frames) {
        frame.DescriptorSet.Create(RendererVulkan->DescriptorPool, pipeline.DescriptorSetLayout);
        frame.DescriptorSet.SetBuffer(frame.UniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    auto &asset_manager = FxAssetManager::GetInstance();
    asset_manager.Start(2);

    // PtrContainer<FxModel> test_model = FxAssetManager::LoadModel("../models/Box.glb");
    PtrContainer<FxModel> new_model = FxAssetManager::LoadModel("../models/Box.glb");
    FxPerspectiveCamera camera;

    PtrContainer<FxModel> other_model = FxAssetManager::LoadModel("../models/DamagedHelmet.glb");

    camera.SetAspectRatio(((float32)window_width) / (float32)window_height);

    camera.Position.Z += 5.0f;

    Mat4f model_matrix = Mat4f::AsTranslation(Vec3f(0, 0, 0));


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

        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_W)) {
            camera.Move(Vec3f(0.0f, 0.0f, 0.01f * DeltaTime));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_S)) {
            camera.Move(Vec3f(0.0f, 0.0f, -0.01f * DeltaTime));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_A)) {
            camera.Move(Vec3f(0.01f * DeltaTime, 0.0f, 0.0f));
        }
        if (FxControlManager::IsKeyDown(FxKey::FX_KEY_D)) {
            camera.Move(Vec3f(-0.01f * DeltaTime, 0.0f, 0.0f));
        }

        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_C) && !other_model->IsUploadedToGpu) {
            // FxAssetManager::LoadModel(new_model, "../models/DamagedHelmet.glb");
            FxAssetManager::LoadModel(other_model, "../models/DamagedHelmet.glb");
        }
        if (FxControlManager::IsKeyPressed(FxKey::FX_KEY_V) && other_model->IsUploadedToGpu) {
            other_model->Destroy();
        }

        camera.Update();

        Mat4f VPMatrix = camera.ViewMatrix * camera.ProjectionMatrix;
        Mat4f MVPMatrix = model_matrix * VPMatrix;

        if (Renderer->BeginFrame(pipeline, MVPMatrix) != FrameResult::Success) {
            continue;
        }

        CheckGeneralControls();

        new_model->Render(pipeline);
        other_model->Render(pipeline);

        Renderer->FinishFrame(pipeline);

        LastTick = CurrentTick;
    }

    asset_manager.Shutdown();

    return 0;
}
