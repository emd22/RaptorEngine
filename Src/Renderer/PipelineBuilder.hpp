#pragma once

#include "Attachment.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>

namespace fx::renderer {

using ColorComponentFlags = uint32;
enum eColorComponent : uint32
{
    ColorComponent_None = 0x0000,

    ColorComponent_R = VK_COLOR_COMPONENT_R_BIT,
    ColorComponent_G = VK_COLOR_COMPONENT_G_BIT,
    ColorComponent_B = VK_COLOR_COMPONENT_B_BIT,
    ColorComponent_A = VK_COLOR_COMPONENT_A_BIT,

    ColorComponent_RG = (ColorComponent_R | ColorComponent_G),
    ColorComponent_RGB = (ColorComponent_RG | ColorComponent_B),
    ColorComponent_RGBA = (ColorComponent_RGB | ColorComponent_A),
};


union PipelineBlendFactor
{
    struct
    {
        VkBlendFactor Src : 8;
        VkBlendFactor Dst : 8;
    } Ops;

    uint16 Value;
};

union PipelineBlendOp
{
    struct
    {
        VkBlendOp Alpha : 8;
        VkBlendOp Color : 8;
    } Ops;

    uint16 Value;
};

struct PipelineBlendAttachment
{
    bool Enabled = true;
    ColorComponentFlags Mask = ColorComponent_RGBA;
    PipelineBlendOp BlendOp = { .Ops = { .Alpha = VK_BLEND_OP_ADD, .Color = VK_BLEND_OP_ADD } };

    PipelineBlendFactor AlphaBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };

    PipelineBlendFactor ColorBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };
};


// PipelineBuilder::Build() // Build pipeline
//     .AddBlendAttachment({ .Enabled = true, .AlphaBlend = { .Src = VK_BLEND_FACTOR_ONE, .Dst = VK_BLEND_FACTOR_ZERO }
//     }) .AddBlendAttachment(ColorComponent_RGBA, false) .AddColorAttachment({
//         .Format = VK_FORMAT_B8G8R8A8_UNORM,
//     });


// using BlendFactorFlags = uint32;
// enum eBlendFactor : uint32
// {
//     BlendFactorSrc_Zero = (VK_BLEND_FACTOR_ZERO << 8),
//     BlendFactorSrc_One = (VK_BLEND_FACTOR_ONE << 8),
//     BlendFactorSrc_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA << 8),
//     BlendFactorSrc_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA << 8),

//     BlendFactorDst_Zero = (VK_BLEND_FACTOR_ZERO & 0xFF),
//     BlendFactorDst_One = (VK_BLEND_FACTOR_ONE & 0xFF),
//     BlendFactorDst_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA & 0xFF),
//     BlendFactorDst_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA & 0xFF),
// };


class PipelineBuilder
{
    static constexpr uint32 scMinAttachmentCount = 10;

public:
    PipelineBuilder() {}

    static PipelineBuilder Make() { return {}; }

    FX_FORCE_INLINE PipelineBuilder& AddBlendAttachment(const PipelineBlendAttachment& attachment)
    {
        if (mBlendAttachments == nullptr) {
            mBlendAttachments.InitCapacity(scMinAttachmentCount);
        }

        mBlendAttachments.Insert(attachment);

        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetOutputTargets(TargetList* attachment_list)
    {
        mpAttachmentList = attachment_list;

        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetShaders(const Ref<ShaderProgram>& vertex, const Ref<ShaderProgram>& fragment)
    {
        mVertexShader = vertex;
        mFragmentShader = fragment;

        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetVertexDescription(VertexDescription* vertex_info)
    {
        mVertexInfo = vertex_info;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetLayout(VkPipelineLayout layout)
    {
        // This is a new pipeline
        mbDidFirstBuild = false;
        mLayout = layout;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetRenderPass(RenderPass* render_pass)
    {
        mRenderPass = render_pass;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetName(const std::string& name)
    {
        mPipelineName = name;
        return *this;
    }

    /////////////////////////////
    // Properties setters
    /////////////////////////////


    FX_FORCE_INLINE PipelineBuilder& SetProperties(const PipelineProperties& properties)
    {
        mProperties = properties;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetWindingOrder(VkFrontFace winding_order)
    {
        mProperties.WindingOrder = winding_order;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetCullMode(VkCullModeFlags cull_mode)
    {
        mProperties.CullMode = cull_mode;
        return *this;
    }

    FX_FORCE_INLINE PipelineBuilder& SetPolygonMode(VkPolygonMode polygon_mode)
    {
        mProperties.PolygonMode = polygon_mode;
        return *this;
    }


    void Build(Pipeline& pipeline)
    {
        SizedArray<VkPipelineColorBlendAttachmentState> vk_blend_attachments;

        // Make vulkan blend attachments
        {
            vk_blend_attachments.InitCapacity(mBlendAttachments.Size);

            for (const PipelineBlendAttachment& am : mBlendAttachments) {
                vk_blend_attachments.Insert(VkPipelineColorBlendAttachmentState {
                    .blendEnable = am.Enabled ? 1U : 0U,
                    .srcColorBlendFactor = am.ColorBlend.Ops.Src,
                    .dstColorBlendFactor = am.ColorBlend.Ops.Dst,
                    .colorBlendOp = am.BlendOp.Ops.Color,
                    .srcAlphaBlendFactor = am.AlphaBlend.Ops.Src,
                    .dstAlphaBlendFactor = am.AlphaBlend.Ops.Dst,
                    .alphaBlendOp = am.BlendOp.Ops.Alpha,
                    .colorWriteMask = am.Mask,
                });
            }
        }

        SizedArray<Ref<ShaderProgram>> shader_list = { mVertexShader, mFragmentShader };

        // Make vulkan blend attachments

        pipeline.Layout = mLayout;

        LogInfo("Creating pipeline '{}'", mPipelineName);
        pipeline.Create(mPipelineName, shader_list, mpAttachmentList->GetDescriptions(), vk_blend_attachments,
                        mVertexInfo, *mRenderPass, mProperties);

        if (mbDidFirstBuild) {
            pipeline.mbDoNotDestroyLayout = true;
        }

        mbDidFirstBuild = true;
    }

private:
    SizedArray<PipelineBlendAttachment> mBlendAttachments;
    TargetList* mpAttachmentList = nullptr;

    VkPipelineLayout mLayout = nullptr;

    std::string mPipelineName = "Unnamed Pipeline";

    Ref<ShaderProgram> mVertexShader { nullptr };
    Ref<ShaderProgram> mFragmentShader { nullptr };

    RenderPass* mRenderPass { nullptr };

    VertexDescription* mVertexInfo { nullptr };
    PipelineProperties mProperties;

    bool mbDidFirstBuild = false;
};

} // namespace fx::renderer
