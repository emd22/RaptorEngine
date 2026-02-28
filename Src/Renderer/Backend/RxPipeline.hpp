#pragma once

#include "../RxVertex.hpp"
#include "RxRenderPass.hpp"
#include "RxShader.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>
#include <Math/FxMat4.hpp>
#include <Renderer/RxVertex.hpp>

class RxCommandBuffer;

struct RxVertexDescription
{
    VkVertexInputBindingDescription Binding;
    FxSizedArray<VkVertexInputAttributeDescription> Attributes;

    bool bIsInited : 1 = false;
};


struct alignas(16) FxDrawPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
    uint32 MaterialIndex = 0;
};

struct alignas(16) FxLightVertPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
};

struct alignas(16) FxCompositionPushConstants
{
    float32 ViewInverse[16];
    float32 ProjInverse[16];
};

struct RxPipelineProperties
{
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool bDisableDepthTest : 1 = false;
    bool bDisableDepthWrite : 1 = false;

    FxVec2u ViewportSize = FxVec2u::sZero;
    VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
};

struct RxPushConstants
{
    uint32 Size;
    VkShaderStageFlags StageFlags;
};


class RxPipeline
{
public:
    void Create(const std::string& name, const FxSlice<FxRef<RxShaderProgram>>& shaders,
                const FxSlice<VkAttachmentDescription>& attachments,
                const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                RxVertexDescription* vertex_info, const RxRenderPass& render_pass,
                const RxPipelineProperties& properties);

    FX_FORCE_INLINE void SetLayout(VkPipelineLayout layout) { Layout = layout; }

    static VkPipelineLayout CreateLayout(const FxSlice<const RxPushConstants>& push_constant_defs,
                                         const FxSlice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(const RxCommandBuffer& command_buffer);

    ~RxPipeline() { Destroy(); }

private:
public:
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;

protected:
    friend class RxPipelineBuilder;

    bool mbDoNotDestroyLayout = false;
};


namespace RxVertexUtil {

template <RxVertexType TVertexType>
RxVertexDescription BuildDescription()
{
    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(RxVertex<TVertexType>),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    FxSizedArray<VkVertexInputAttributeDescription> attribs;

    if constexpr (TVertexType == RxVertexType::eSlim) {
        attribs = {
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
        };
    }
    else if constexpr (TVertexType == RxVertexType::eDefault) {
        using VertexType = RxVertex<RxVertexType::eDefault>;
        attribs = {
            // Position
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
            // Normal
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(VertexType, Normal),
            },
            // UV
            {
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(VertexType, UV),
            },
            // Tangent
            {
                .location = 3,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(VertexType, Tangent),
            },
        };
    }

    else if constexpr (TVertexType == RxVertexType::eSkinned) {
        using VertexType = RxVertex<RxVertexType::eSkinned>;
        attribs = {
            // Position
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
            // Normal
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(VertexType, Normal),
            },
            // UV
            {
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(VertexType, UV),
            },
            // Tangent
            {
                .location = 3,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(VertexType, Tangent),
            },
            // Bone IDs
            {
                .location = 4,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32A32_UINT,
                .offset = offsetof(VertexType, BoneIds),
            },
            // Bone Weights
            {
                .location = 5,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(VertexType, BoneWeights),
            },
        };
    }
    else {
        FxLogError("Unsupported vertex type!");
    }

    return { binding_desc, std::move(attribs), true };
}

FX_FORCE_INLINE RxVertexDescription BuildDescription(RxVertexType vertex_type)
{
    switch (vertex_type) {
    case RxVertexType::eSlim:
        return RxVertexUtil::BuildDescription<RxVertexType::eSlim>();
    case RxVertexType::eDefault:
        return RxVertexUtil::BuildDescription<RxVertexType::eDefault>();
    case RxVertexType::eSkinned:
        return RxVertexUtil::BuildDescription<RxVertexType::eSkinned>();
    default:;
    }

    FxLogError("Unsupported vertex type!");

    return RxVertexDescription {};
}
}; // namespace RxVertexUtil
