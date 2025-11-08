#pragma once

#include "Backend/RxPipeline.hpp"
#include "Backend/RxShader.hpp"
#include "RxAttachment.hpp"

#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxStackArray.hpp>


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


union RxPipelineBlendFactor
{
    struct
    {
        VkBlendFactor Src : 8 = VK_BLEND_FACTOR_ZERO;
        VkBlendFactor Dst : 8 = VK_BLEND_FACTOR_ZERO;
    };

    uint16 Value;
};

union RxPipelineBlendOp
{
    struct
    {
        VkBlendOp Alpha : 8 = VK_BLEND_OP_ADD;
        VkBlendOp Color : 8 = VK_BLEND_OP_ADD;
    };

    uint16 Value;
};

struct RxPipelineBlendAttachment
{
    bool Enabled = true;
    RxColorComponentFlags Mask = RxColorComponent_RGBA;
    RxPipelineBlendOp BlendOp {};
    RxPipelineBlendFactor AlphaBlend {};
    RxPipelineBlendFactor ColorBlend {};
};


// RxPipelineBuilder::Build() // Build pipeline
//     .AddBlendAttachment({ .Enabled = true, .AlphaBlend = { .Src = VK_BLEND_FACTOR_ONE, .Dst = VK_BLEND_FACTOR_ZERO }
//     }) .AddBlendAttachment(RxColorComponent_RGBA, false) .AddColorAttachment({
//         .Format = VK_FORMAT_B8G8R8A8_UNORM,
//     });


// using RxBlendFactorFlags = uint32;
// enum RxBlendFactor : uint32
// {
//     RxBlendFactorSrc_Zero = (VK_BLEND_FACTOR_ZERO << 8),
//     RxBlendFactorSrc_One = (VK_BLEND_FACTOR_ONE << 8),
//     RxBlendFactorSrc_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA << 8),
//     RxBlendFactorSrc_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA << 8),

//     RxBlendFactorDst_Zero = (VK_BLEND_FACTOR_ZERO & 0xFF),
//     RxBlendFactorDst_One = (VK_BLEND_FACTOR_ONE & 0xFF),
//     RxBlendFactorDst_SrcAlpha = (VK_BLEND_FACTOR_SRC_ALPHA & 0xFF),
//     RxBlendFactorDst_DstAlpha = (VK_BLEND_FACTOR_DST_ALPHA & 0xFF),
// };


class RxPipelineBuilder
{
    static constexpr uint32 scMinAttachmentCount = 10;

public:
    RxPipelineBuilder() {}

    static RxPipelineBuilder Make() { return {}; }

    FX_FORCE_INLINE RxPipelineBuilder& AddBlendAttachment(const RxPipelineBlendAttachment& attachment)
    {
        if (mBlendAttachments == nullptr) {
            mBlendAttachments.InitCapacity(scMinAttachmentCount);
        }

        mBlendAttachments.Insert(attachment);

        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetAttachments(RxAttachmentList* attachment_list)
    {
        mpAttachmentList = attachment_list;

        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetShaders(const FxRef<RxShader>& vertex, const FxRef<RxShader>& fragment)
    {
        mVertexShader = vertex;
        mFragmentShader = fragment;

        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetVertexInfo(FxVertexInfo* vertex_info)
    {
        mVertexInfo = vertex_info;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetLayout(VkPipelineLayout layout)
    {
        mLayout = layout;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetRenderPass(RxRenderPass* render_pass)
    {
        mRenderPass = render_pass;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetName(const std::string& name)
    {
        mPipelineName = name;
        return *this;
    }

    /////////////////////////////
    // Properties setters
    /////////////////////////////


    FX_FORCE_INLINE RxPipelineBuilder& SetProperties(const RxGraphicsPipelineProperties& properties)
    {
        mProperties = properties;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetWindingOrder(VkFrontFace winding_order)
    {
        mProperties.WindingOrder = winding_order;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetCullMode(VkCullModeFlags cull_mode)
    {
        mProperties.CullMode = cull_mode;
        return *this;
    }

    FX_FORCE_INLINE RxPipelineBuilder& SetPolygonMode(VkPolygonMode polygon_mode)
    {
        mProperties.PolygonMode = polygon_mode;
        return *this;
    }


    void Build(RxGraphicsPipeline& pipeline)
    {
        FxSizedArray<VkPipelineColorBlendAttachmentState> vk_blend_attachments;

        // Make vulkan blend attachments
        {
            vk_blend_attachments.InitCapacity(mBlendAttachments.Size);

            for (const RxPipelineBlendAttachment& am : mBlendAttachments) {
                vk_blend_attachments.Insert(VkPipelineColorBlendAttachmentState {
                    .colorWriteMask = am.Mask,
                    .blendEnable = am.Enabled ? 1U : 0U,
                    .alphaBlendOp = am.BlendOp.Alpha,
                    .colorBlendOp = am.BlendOp.Color,
                    .srcAlphaBlendFactor = am.AlphaBlend.Src,
                    .dstAlphaBlendFactor = am.AlphaBlend.Dst,
                    .srcColorBlendFactor = am.ColorBlend.Src,
                    .dstColorBlendFactor = am.ColorBlend.Dst,
                });
            }
        }

        FxSizedArray<FxRef<RxShader>> shader_list = { mVertexShader, mFragmentShader };

        // Make vulkan blend attachments

        pipeline.Layout = mLayout;

        FxLogInfo("Creating pipeline '{}'", mPipelineName);
        pipeline.Create(mPipelineName, shader_list, mpAttachmentList->GetBuiltAttachments(), vk_blend_attachments,
                        mVertexInfo, *mRenderPass, mProperties);
    }

private:
    FxSizedArray<RxPipelineBlendAttachment> mBlendAttachments;
    RxAttachmentList* mpAttachmentList = nullptr;

    VkPipelineLayout mLayout = nullptr;

    std::string mPipelineName = "Unnamed Pipeline";

    FxRef<RxShader> mVertexShader { nullptr };
    FxRef<RxShader> mFragmentShader { nullptr };

    RxRenderPass* mRenderPass { nullptr };

    FxVertexInfo* mVertexInfo { nullptr };
    RxGraphicsPipelineProperties mProperties;
};
