#pragma once

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Renderer/Vertex.hpp>

namespace fx::renderer {

struct VertexDescription
{
    VkVertexInputBindingDescription Binding;
    SizedArray<VkVertexInputAttributeDescription> Attributes;

    bool bIsInited : 1 = false;
};

namespace VertexUtil {


template <VertexType TVertexType>
VertexDescription BuildDescription()
{
    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(Vertex<TVertexType>),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    SizedArray<VkVertexInputAttributeDescription> attribs;

    if constexpr (TVertexType == VertexType::Slim) {
        attribs = {
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
        };
    }
    else if constexpr (TVertexType == VertexType::Default) {
        using VertexType = Vertex<VertexType::Default>;
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

    else if constexpr (TVertexType == VertexType::Skinned) {
        using VertexType = Vertex<VertexType::Skinned>;
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
        LogError("Unsupported vertex type!");
    }

    return { binding_desc, std::move(attribs), true };
}

FX_FORCE_INLINE VertexDescription BuildDescription(VertexType vertex_type)
{
    switch (vertex_type) {
    case VertexType::Slim:
        return VertexUtil::BuildDescription<VertexType::Slim>();
    case VertexType::Default:
        return VertexUtil::BuildDescription<VertexType::Default>();
    case VertexType::Skinned:
        return VertexUtil::BuildDescription<VertexType::Skinned>();
    default:;
    }

    LogError("Unsupported vertex type!");

    return VertexDescription {};
}
}; // namespace VertexUtil

} // namespace fx::renderer
