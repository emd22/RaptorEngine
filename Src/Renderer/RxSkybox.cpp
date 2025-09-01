#include "RxSkybox.hpp"

#include "Backend/RxShader.hpp"
#include "Renderer.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("RxSkybox");

void RxSkyboxRenderer::Create(const FxVec2u& extent,
                              const FxRef<FxPrimitiveMesh<RxSkyboxRenderer::VertexType>>& skybox_mesh)
{
    CreateSkyboxPipeline();

    mDeferredRenderer = Renderer->DeferredRenderer.mPtr;
}


void RxSkyboxRenderer::Render() {}

void RxSkyboxRenderer::CreateSkyboxPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        // Color
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
    };

    ShaderList shader_list;

    RxShader vertex_shader("../shaders/Skybox.vs.spv", RxShaderType::Vertex);
    RxShader fragment_shader("../shaders/Skybox.fs.spv", RxShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    CreateSkyboxPipelineLayout();

    FxVertexInfo vert_info = FxMakeVertexInfo();

    SkyboxPipeline.Create("Skybox", shader_list, FxMakeSlice(attachments, FxSizeofArray(attachments)),
                          FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)), &vert_info,
                          Renderer->DeferredRenderer->RpGeometry, { .CullMode = VK_CULL_MODE_NONE });
}

VkPipelineLayout RxSkyboxRenderer::CreateSkyboxPipelineLayout()
{
    RxGpuDevice* device = Renderer->GetDevice();

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
            FxModulePanic("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = { DsLayoutSkyboxFragment };

    VkPipelineLayout layout = SkyboxPipeline.CreateLayout(sizeof(RxSkyboxPushConstants), 0,
                                                          FxMakeSlice(layouts, FxSizeofArray(layouts)));

    RxUtil::SetDebugLabel("Skybox Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}
