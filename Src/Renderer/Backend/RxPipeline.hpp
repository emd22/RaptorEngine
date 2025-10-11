#pragma once

#include "../RxVertex.hpp"
#include "RxRenderPass.hpp"
#include "ShaderList.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>
#include <Math/Mat4.hpp>

class RxCommandBuffer;

struct FxVertexInfo
{
    VkVertexInputBindingDescription binding;
    FxSizedArray<VkVertexInputAttributeDescription> attributes;
};


struct alignas(16) FxDrawPushConstants
{
    float32 MVPMatrix[16];
    float32 NormalMatrix[12];
    uint32 MaterialIndex = 0;
};

struct alignas(16) FxLightVertPushConstants
{
    float32 MVPMatrix[16];
};

struct alignas(16) FxLightFragPushConstants
{
    float32 InvView[16];
    float32 InvProj[16];
    float32 LightPos[4];
    float32 LightColor[4];
    float32 PlayerPos[4];
    float32 LightRadius;
};

struct alignas(16) FxCompositionPushConstants
{
    float32 ViewInverse[16];
    float32 ProjInverse[16];
};

FxVertexInfo FxMakeVertexInfo();
FxVertexInfo FxMakeLightVertexInfo();

struct RxGraphicsPipelineProperties
{
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;
    bool ForceNoDepthTest = false;
};

struct RxPushConstants
{
    uint32 Size;
    VkShaderStageFlags StageFlags;
};


class RxGraphicsPipeline
{
public:
    void Create(const std::string& name, ShaderList shader_list, const FxSlice<VkAttachmentDescription>& attachments,
                const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, FxVertexInfo* vertex_info,
                const RxRenderPass& render_pass, const RxGraphicsPipelineProperties& properties);

    // void CreateComp(ShaderList shader_list, VkPipelineLayout layout, const
    // FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, bool is_comp);

    // VkPipelineLayout CreateCompLayout();

    VkPipelineLayout CreateLayout(const FxSlice<const RxPushConstants>& push_constant_defs,
                                  const FxSlice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(const RxCommandBuffer& command_buffer);

    ~RxGraphicsPipeline() { Destroy(); }

private:
public:
    // VkDescriptorSetLayout MainDescriptorSetLayout = nullptr;
    // VkDescriptorSetLayout MaterialDescriptorSetLayout = nullptr;

    VkDescriptorSetLayout CompDescriptorSetLayout = nullptr;

    // VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    // RxRenderPass RenderPass;
    //
    // RxRenderPass* RenderPass = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;
    ShaderList mShaders;
};


using RxColorComponentFlags = uint32;
enum RxColorComponent : uint32
{
    RxColorComponent_None = 0x0000,

    RxColorComponent_R = VK_COLOR_COMPONENT_R_BIT,
    RxColorComponent_G = VK_COLOR_COMPONENT_G_BIT,
    RxColorComponent_B = VK_COLOR_COMPONENT_B_BIT,
    RxColorComponent_A = VK_COLOR_COMPONENT_A_BIT,

    RxColorComponent_RG = (RxColorComponent_R | RxColorComponent_G),
    RxColorComponent_RGB = (RxColorComponent_RG | RxColorComponent_B),
    RxColorComponent_RGBA = (RxColorComponent_RGB | RxColorComponent_A),
};


using RxBlendOpFlags = uint32;
enum RxBlendOp : uint32
{
    RxBlendOp_Add = VK_BLEND_OP_ADD,
    RxBlendOp_Subtract = VK_BLEND_OP_SUBTRACT,
};

using RxBlendFactorFlags = uint32;
enum RxBlendFactor : uint32
{
    RxBlendFactorSrc_Zero = (VK_BLEND_FACTOR_ZERO << 8),
    RxBlendFactorSrc_One = (VK_BLEND_FACTOR_ONE << 8),
    RxBlendFactorSrc_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA << 8),
    RxBlendFactorSrc_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA << 8),

    RxBlendFactorDst_Zero = (VK_BLEND_FACTOR_ZERO & 0xFF),
    RxBlendFactorDst_One = (VK_BLEND_FACTOR_ONE & 0xFF),
    RxBlendFactorDst_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA & 0xFF),
    RxBlendFactorDst_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA & 0xFF),
};


class RxGraphicsPipelineBuilder
{
public:
    RxGraphicsPipelineBuilder() = default;

    template <typename... TAttachments>
    RxGraphicsPipeline& AddBlendAttachments(TAttachments... attachments)
    {
    }

    VkPipelineColorBlendAttachmentState BlendAttachment(RxColorComponentFlags color_write_mask = RxColorComponent_RGBA)
    {
        return VkPipelineColorBlendAttachmentState { .colorWriteMask = color_write_mask, .blendEnable = false };
    }

    static inline VkPipelineColorBlendAttachmentState
    BlendAttachment(RxColorComponentFlags color_write_mask, RxBlendOpFlags color_op, RxBlendOpFlags alpha_op,
                    RxBlendFactorFlags alpha_blend_factors = (RxBlendFactorSrc_Zero | RxBlendFactorDst_Zero),
                    RxBlendFactorFlags color_blend_factors = (RxBlendFactorSrc_Zero | RxBlendFactorDst_Zero))
    {
        const uint16 src_alpha = (alpha_blend_factors >> 8);
        const uint16 dst_alpha = (alpha_blend_factors & 0xFF);

        const uint16 src_color = (color_blend_factors >> 8);
        const uint16 dst_color = (color_blend_factors & 0xFF);

        return VkPipelineColorBlendAttachmentState {
            .colorWriteMask = color_write_mask,
            .blendEnable = true,
            .colorBlendOp = static_cast<VkBlendOp>(color_op),
            .alphaBlendOp = static_cast<VkBlendOp>(alpha_op),
            .srcAlphaBlendFactor = static_cast<VkBlendFactor>(src_alpha),
            .dstAlphaBlendFactor = static_cast<VkBlendFactor>(dst_alpha),
            .srcColorBlendFactor = static_cast<VkBlendFactor>(src_color),
            .dstColorBlendFactor = static_cast<VkBlendFactor>(dst_color),
        };
    }
};
