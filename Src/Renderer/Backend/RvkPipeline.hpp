#pragma once

#include "ShaderList.hpp"
#include "RvkRenderPass.hpp"

#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

#include <vulkan/vulkan.h>

#include <Math/Mat4.hpp>

class RvkCommandBuffer;

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
    FxVertexUV = 0x04,
};

template <int8 Flags>
struct RvkVertex;

template <>
struct RvkVertex<FxVertexPosition>
{
    float32 Position[3];
} __attribute__((packed));

template <>
struct RvkVertex<FxVertexPosition | FxVertexNormal>
{
    float32 Position[3];
    float32 Normal[3];
} __attribute__((packed));

template <>
struct RvkVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
} __attribute__((packed));

struct FxVertexInfo
{
    VkVertexInputBindingDescription binding;
    FxSizedArray<VkVertexInputAttributeDescription> attributes;
};


struct alignas(16) FxDrawPushConstants
{
    float32 MVPMatrix[16];
    float32 ModelMatrix[16];
};

struct alignas(16) FxLightPushConstants
{
    float32 MVPMatrix[16];
    float32 VPMatrix[16];
    float32 LightPos[3];
    float32 PlayerPos[3];
    float32 LightRadius;
};

struct alignas(16) FxCompositionPushConstants
{
    float32 ViewInverse[16];
    float32 ProjInverse[16];
};


FxVertexInfo FxMakeVertexInfo();
FxVertexInfo FxMakeLightVertexInfo();


class RvkGraphicsPipeline
{
public:
    void Create(
        ShaderList shader_list,
        VkPipelineLayout layout,
        const FxSlice<VkAttachmentDescription>& attachments,
        const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
        FxVertexInfo* vertex_info,
        VkCullModeFlags cull_mode = VK_CULL_MODE_NONE
    );

    // void CreateComp(ShaderList shader_list, VkPipelineLayout layout, const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, bool is_comp);

    // VkPipelineLayout CreateCompLayout();

    VkPipelineLayout CreateLayout(uint32 vert_push_consts_size, uint32 frag_push_consts_size, const FxSlice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(RvkCommandBuffer &command_buffer);

    ~RvkGraphicsPipeline()
    {
        Destroy();
    }
private:

public:
    // VkDescriptorSetLayout MainDescriptorSetLayout = nullptr;
    // VkDescriptorSetLayout MaterialDescriptorSetLayout = nullptr;

    VkDescriptorSetLayout CompDescriptorSetLayout = nullptr;

    // VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    RvkRenderPass RenderPass;
private:
    RvkGpuDevice *mDevice = nullptr;
    ShaderList mShaders;
};
