#include "RxSkybox.hpp"

#include "Backend/RxShader.hpp"
#include "Renderer.hpp"

#include <Core/FxPanic.hpp>
#include <Renderer/FxCamera.hpp>

FX_SET_MODULE_NAME("RxSkybox");

void RxSkyboxRenderer::Create(const FxVec2u& extent,
                              const FxRef<FxPrimitiveMesh<RxSkyboxRenderer::VertexType>>& skybox_mesh)
{
    CreateSkyboxPipeline();

    mDeferredRenderer = Renderer->DeferredRenderer.mPtr;
    mSkyboxMesh = skybox_mesh;

    // SkyboxAttachment.Create(RxImageType::Cubemap, extent, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
    //                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    //                         VK_IMAGE_ASPECT_COLOR_BIT);

    for (int frame_index = 0; frame_index < RendererFramesInFlight; frame_index++) {
        BuildDescriptorSets(frame_index);
    }
}


void RxSkyboxRenderer::Render(const RxCommandBuffer& cmd, const FxCamera& render_cam)
{
    FxMat4f model_matrix = FxMat4f::Identity;


    RxSkyboxPushConstants push_constants {};
    memcpy(push_constants.ProjectionMatrix, render_cam.VPMatrix.GetWithoutTranslation().RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, model_matrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(cmd.CommandBuffer, SkyboxPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants),
                       &push_constants);


    DsSkyboxFragments[Renderer->GetFrameNumber()].Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SkyboxPipeline);
    SkyboxPipeline.Bind(cmd);
    mSkyboxMesh->Render(cmd, SkyboxPipeline);
}


void RxSkyboxRenderer::CreateSkyboxPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        // Color
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
        // // Normals
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
    };

    VkAttachmentDescription attachments[] = {
        // Frag colour output
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },

        VkAttachmentDescription {
            .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        },
    };

    ShaderList shader_list;

    RxShader vertex_shader("../shaders/Skybox.vs.spv", RxShaderType::Vertex);
    RxShader fragment_shader("../shaders/Skybox.fs.spv", RxShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    CreateSkyboxPipelineLayout();

    FxVertexInfo vert_info = FxMakeLightVertexInfo();

    SkyboxPipeline.Create(
        "Skybox", shader_list, FxMakeSlice(attachments, FxSizeofArray(attachments)),
        FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)), &vert_info,
        Renderer->DeferredRenderer->RpGeometry,
        { .CullMode = VK_CULL_MODE_NONE, .WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE, .ForceNoDepthTest = true });
}

VkPipelineLayout RxSkyboxRenderer::CreateSkyboxPipelineLayout()
{
    RxGpuDevice* device = Renderer->GetDevice();

    mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    mDescriptorPool.Create(Renderer->GetDevice());

    {
        // Albedo texture
        VkDescriptorSetLayoutBinding skybox_layout_binding {
            .binding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo ds_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &skybox_layout_binding,
        };

        VkResult status = vkCreateDescriptorSetLayout(device->Device, &ds_layout_info, nullptr,
                                                      &DsLayoutSkyboxFragment);
        if (status != VK_SUCCESS) {
            FxModulePanicVulkan("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = { DsLayoutSkyboxFragment };


    VkPipelineLayout layout = SkyboxPipeline.CreateLayout(sizeof(RxSkyboxPushConstants), 0,
                                                          FxMakeSlice(layouts, FxSizeofArray(layouts)));


    RxUtil::SetDebugLabel("Skybox Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    for (int i = 0; i < RendererFramesInFlight; i++) {
        RxDescriptorSet descriptor_set {};

        descriptor_set.Create(mDescriptorPool, DsLayoutSkyboxFragment, 1);

        DsSkyboxFragments.Insert(descriptor_set);
    }

    return layout;
}

void RxSkyboxRenderer::BuildDescriptorSets(uint32 frame_index)
{
    FxStackArray<VkWriteDescriptorSet, 1> write_infos;

    {
        const int binding_index = 0;

        VkDescriptorImageInfo positions_image_info {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = SkyAttachment->View,
            .sampler = Renderer->Swapchain.ColorSampler.Sampler,
        };

        VkWriteDescriptorSet positions_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DsSkyboxFragments[frame_index].Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &positions_image_info,

        };

        write_infos.Insert(positions_write);
    }

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_infos.Size, write_infos.Data, 0, nullptr);
}
