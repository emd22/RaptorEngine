
#include "vulkan/vulkan_core.h"
#define VMA_DEBUG_LOG(...) Log::Warning(__VA_ARGS__)

#include <ThirdParty/vk_mem_alloc.h>

#include "Core/Defines.hpp"
#include <Renderer/Backend/RvkFrameData.hpp>
#include "Renderer/Constants.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Backend/ShaderList.hpp"
#include "Renderer/Backend/RvkShader.hpp"
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

void CreateCompositionPipeline(RvkGraphicsPipeline& pipeline)
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        }
    };

    RvkShader vertex_shader("../shaders/composition.vert.spv", RvkShaderType::Vertex);
    RvkShader fragment_shader("../shaders/composition.frag.spv", RvkShaderType::Fragment);

    ShaderList shader_list{ .Vertex = vertex_shader, .Fragment = fragment_shader };

    const int attachment_count = FxSizeofArray(color_blend_attachments);
    pipeline.CreateComp(shader_list, pipeline.CreateCompLayout(), FxMakeSlice(color_blend_attachments, attachment_count), true);
}

// void CreateSolidPipeline(RvkGraphicsPipeline& pipeline)
// {
//     VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
//         // Positions
//         VkPipelineColorBlendAttachmentState {
//             .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
//                             | VK_COLOR_COMPONENT_G_BIT
//                             | VK_COLOR_COMPONENT_B_BIT
//                             | VK_COLOR_COMPONENT_A_BIT,
//             .blendEnable = VK_FALSE,
//         },
//         // Albedo
//         VkPipelineColorBlendAttachmentState {
//             .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
//                             | VK_COLOR_COMPONENT_G_BIT
//                             | VK_COLOR_COMPONENT_B_BIT
//                             | VK_COLOR_COMPONENT_A_BIT,
//             .blendEnable = VK_FALSE,
//         },
//     };

//     ShaderList shader_list;

//     RvkShader vertex_shader("../shaders/main.vert.spv", RvkShaderType::Vertex);
//     RvkShader fragment_shader("../shaders/main.frag.spv", RvkShaderType::Fragment);

//     shader_list.Vertex = vertex_shader.ShaderModule;
//     shader_list.Fragment = fragment_shader.ShaderModule;

//     const int attachment_count = FxSizeofArray(color_blend_attachments);
//     pipeline.Create(shader_list, pipeline.CreateGPassLayout(), FxMakeSlice(color_blend_attachments, attachment_count), false);
// }


#define PUSH_DESCRIPTOR_IMAGE(img_, sampler_, binding_) \
    { \
        VkDescriptorImageInfo image_info { \
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, \
            .imageView = (img_).View, \
            .sampler = (sampler_).Sampler, \
        }; \
        VkWriteDescriptorSet image_write { \
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
            .descriptorCount = 1, \
            .dstSet = descriptor_set.Set, \
            .dstBinding = binding_, \
            .dstArrayElement = 0, \
            .pImageInfo = &image_info, \
        }; \
        image_infos.Insert(image_write); \
    }


void BuildCompositionDescriptorSet(FxDeferredRenderer& renderer, int index)
{
    RvkFrameData* frame = &Renderer->Frames[index];
    FxStackArray<VkWriteDescriptorSet, 3> image_infos;

    RvkDescriptorSet& descriptor_set = frame->CompDescriptorSet;

    RvkSwapchain& swapchain = Renderer->Swapchain;

    PUSH_DESCRIPTOR_IMAGE(renderer.GPasses[index].PositionsAttachment, swapchain.PositionSampler, 1);
    PUSH_DESCRIPTOR_IMAGE(renderer.GPasses[index].ColorAttachment, swapchain.ColorSampler, 2);

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, image_infos.Size, image_infos.Data, 0, nullptr);

    // PUSH_IMAGE_IF_SET()
}

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
    Renderer->Init(Vec2u(window_width, window_height));

    // RvkGraphicsPipeline pipeline;

    // CreateSolidPipeline(pipeline);

    // Renderer->GPassDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RendererFramesInFlight);
    // Renderer->GPassDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Renderer->GPassDescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    RvkGraphicsPipeline composition_pipeline;

    // Positions sampler
    Renderer->CompDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Albedo sampler
    Renderer->CompDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    Renderer->CompDescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    CreateCompositionPipeline(composition_pipeline);

    Renderer->Swapchain.CreateSwapchainFramebuffers(&composition_pipeline);

    // Renderer->OffscreenSemaphore.Create(Renderer->GetDevice());

    FxDeferredRenderer deferred_renderer;
    deferred_renderer.Create(Renderer->Swapchain.Extent);
    Renderer->DeferredRenderer = &deferred_renderer;



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

    FxRef<FxMaterial> cheese_material = FxMaterialManager::New("Cheese", &deferred_renderer.GPassPipeline);
    cheese_material->Attach(FxMaterial::ResourceType::Diffuse, cheese_image);

    // FxSceneObject scene_object;
    // scene_object.Attach(other_model);
    // scene_object.Attach(material);

    FxSceneObject helmet_object;
    helmet_object.Attach(helmet_model);

    if (helmet_model->Materials.size() > 0) {
        FxRef<FxMaterial>& helmet_material = helmet_model->Materials.at(0);
        helmet_material->Pipeline = &deferred_renderer.GPassPipeline;

        helmet_object.Attach(helmet_material);
    }
    else {
        helmet_object.Attach(cheese_material);
    }

    // FxRef<FxScript> script_instance = FxRef<TestScript>::New();
    // helmet_object.Attach(script_instance);

    int index = 0;
    for (RvkFrameData& frame : Renderer->Frames) {
        // frame.DescriptorSet.Create(Renderer->GPassDescriptorPool, pipeline.MainDescriptorSetLayout);
        frame.CompDescriptorSet.Create(Renderer->CompDescriptorPool, composition_pipeline.CompDescriptorSetLayout);

        // VkDescriptorBufferInfo ubo_info{
        //     .buffer = frame.Ubo.Buffer,
        //     .offset = 0,
        //     .range = sizeof(RvkUniformBufferObject)
        // };

        // VkWriteDescriptorSet ubo_write{
        //     .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //     .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        //     .descriptorCount = 1,
        //     .dstSet = frame.DescriptorSet,
        //     .dstBinding = 0,
        //     .dstArrayElement = 0,
        //     .pBufferInfo = &ubo_info,
        // };

        // const VkWriteDescriptorSet writes[] = { ubo_write };

        // vkUpdateDescriptorSets(Renderer->GetDevice()->Device, sizeof(writes) / sizeof(writes[0]), writes, 0, nullptr);


        BuildCompositionDescriptorSet(deferred_renderer, index++);
        // frame.DescriptorSet.SubmitWrites();
    }



    // PtrContainer<FxModel> other_model = FxAssetManager::NewModel();

    camera.SetAspectRatio(((float32)window_width) / (float32)window_height);

    camera.Position.Z += 5.0f;

    // Mat4f model_matrix = Mat4f::AsTranslation(FxVec3f(0, 0, 0));

    FxSizedArray<VkDescriptorSet> sets_to_bind;
    sets_to_bind.InitSize(2);


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

        camera.Update();

        // FxVec3f translation = FxVec3f(sin(Renderer->GetElapsedFrameCount() * 0.05f) * 0.005f * DeltaTime, cos(Renderer->GetElapsedFrameCount() * 0.05f) * 0.005f * DeltaTime, 0.0f);

        // helmet_object.Translate(translation);


        // Mat4f MVPMatrix = model_matrix * camera.VPMatrix;

        if (Renderer->BeginFrame(deferred_renderer) != FrameResult::Success) {
            continue;
        }

        CheckGeneralControls();


        // helmet_object.mModelMatrix.LookAt(helmet_object.GetPosition(), current_camera->Position, FxVec3f::Up);

        // RvkFrameData* frame = Renderer->GetFrame();




        // sets_to_bind[0] = frame->DescriptorSet.Set;
        // sets_to_bind[1] = material->mDescriptorSet.Set;

        // RvkDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, sets_to_bind);



        // material->mDescriptorSet.Bind(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // new_model->Render(pipeline);
        // other_model->Render(pipeline);
        //
        helmet_object.Render(camera);
        // scene_object.Render(camera);

        Renderer->FinishFrame(composition_pipeline);

        LastTick = CurrentTick;
    }

    Renderer->GetDevice()->WaitForIdle();

    material_manager.Destroy();
    asset_manager.Shutdown();

    // composition_pipeline.Destroy();

    std::cout << "this thread: " << std::this_thread::get_id() << std::endl;

    return 0;
}
