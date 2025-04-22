

#include "Asset/Loader/FxGltfLoader.hpp"
#include "Core/Defines.hpp"
#include "Renderer/Backend/FxRenderBackendVulkan.hpp"
#include "Renderer/Backend/Vulkan/Shader.hpp"
#include "Renderer/Backend/Vulkan/Shader.hpp"
#include "Renderer/Backend/Vulkan/ShaderList.hpp"
#include "sdl3/3.2.8/include/SDL3/SDL_events.h"
#include "sdl3/3.2.8/include/SDL3/SDL_scancode.h"

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

AR_SET_MODULE_NAME("Main")

inline void ProcessEvents() {
    SDL_Event event;

    FxControlManager::UpdateControlManager();

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                Running = false;
                break;

            // Keyboard events

            case SDL_EVENT_KEY_DOWN: // fallthrough
            case SDL_EVENT_KEY_UP:
                FxControlManager::UpdateFromKeyboardEvent(&event);
                break;

            default:;
        }
    }
}

#include <csignal>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        FxPanic("Could not initialize SDL! (SDL err: %s)\n", SDL_GetError());
    }

    // catch sigabrt to avoid macOS showing "report" popup
    signal(SIGABRT, [](int signum) {
        Log::Error("Aborted!");

        exit(1);
    });

    auto window = FxWindow::New("Foxtrot Engine", 1024, 720);

    vulkan::FxRenderBackendVulkan renderer_state;
    SetRendererBackend(&renderer_state);

    Renderer->SelectWindow(window);
    Renderer->Init(Vec2i(1024, 720));

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
    PtrContainer<FxModel> test_model = FxAssetManager::NewModel();

    test_model->OnLoaded =
        [](FxBaseAsset *model) {
            Log::Info("This was loaded!", 0);
        };

    StaticArray<FxVertex<FxVertexPosition | FxVertexNormal>> test_mesh_verts = {
        FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = { -1,   1,  0 } },
        FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = {  1,   1,  0 } },
        FxVertex<FxVertexPosition | FxVertexNormal>{ .Position = {  0,  -1,  0 } },
    };

    // FxMesh test_mesh(test_mesh_verts);

    while (Running) {
        ProcessEvents();

        if (Renderer->BeginFrame(pipeline) != FrameResult::Success) {
            continue;
        }

        if (FxControlManager::IsKeyPressed(SDL_SCANCODE_R) && !test_model->IsLoaded) {
            FxAssetManager::LoadModel(test_model, "../models/Box.glb");
        }

        if (FxControlManager::IsKeyPressed(SDL_SCANCODE_Q)) {
            Running = false;
        }

        // test_mesh.Render();
        // test_model->Render();

        test_model->Render();

        Renderer->FinishFrame(pipeline);
    }

    asset_manager.ShutDown();

    return 0;
}
