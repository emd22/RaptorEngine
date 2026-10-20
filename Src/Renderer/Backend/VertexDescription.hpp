#pragma once

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Renderer/Vertex.hpp>

namespace fx::renderer {

struct VertexDescription
{
    static constexpr uint32 scMaxBindings = 3;

    static constexpr uint32 scLightmapBinding = scLightmapVertexBinding;
    static constexpr uint32 scLightmapLocation = 4;

    static constexpr uint32 scColorBinding = scColorVertexBinding;

    VkVertexInputBindingDescription Bindings[scMaxBindings];
    uint32 BindingCount = 1;

    SizedArray<VkVertexInputAttributeDescription> Attributes;

    bool bIsInited : 1 = false;
};

namespace VertexUtil {


template <eVertexType TVertexType>
VertexDescription BuildDescription()
{
    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(Vertex<TVertexType>),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    SizedArray<VkVertexInputAttributeDescription> attribs;

    if constexpr (TVertexType == eVertexType::Slim) {
        attribs = {
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
        };
    }
    else if constexpr (TVertexType == eVertexType::Default) {
        using VertexType = Vertex<eVertexType::Default>;
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
            // Tangent, xyz plus the bitangent handedness in w
            {
                .location = 3,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(VertexType, Tangent),
            },
        };
    }

    else if constexpr (TVertexType == eVertexType::Skinned) {
        using VertexType = Vertex<eVertexType::Skinned>;
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
            // Tangent, xyz plus the bitangent handedness in w
            {
                .location = 3,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
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
        LogError(LC_RENDER, "Unsupported vertex type!");
    }

    return { { binding_desc }, 1, std::move(attribs), true };
}

/**
 * @brief The default vertex plus extra per-vertex streams. The shader declares the lightmap UV before the vertex
 * colour, so the colour takes the location after the lightmap UV when both are there.
 */
inline VertexDescription BuildStreamDescription(bool has_lightmap, bool has_color)
{
    VertexDescription base = VertexUtil::BuildDescription<eVertexType::Default>();

    VertexDescription description;
    description.Bindings[0] = base.Bindings[0];
    description.BindingCount = 1;

    description.Attributes.InitCapacity(base.Attributes.Size + 2);

    for (const VkVertexInputAttributeDescription& attrib : base.Attributes) {
        description.Attributes.Insert(attrib);
    }

    uint32 location = VertexDescription::scLightmapLocation;

    if (has_lightmap) {
        description.Bindings[description.BindingCount++] = {
            .binding = VertexDescription::scLightmapBinding,
            .stride = sizeof(float32) * 2,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        description.Attributes.Insert(VkVertexInputAttributeDescription {
            .location = location++,
            .binding = VertexDescription::scLightmapBinding,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        });
    }

    if (has_color) {
        description.Bindings[description.BindingCount++] = {
            .binding = VertexDescription::scColorBinding,
            .stride = sizeof(uint32),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        description.Attributes.Insert(VkVertexInputAttributeDescription {
            .location = location++,
            .binding = VertexDescription::scColorBinding,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = 0,
        });
    }

    description.bIsInited = true;

    return description;
}

FX_FORCE_INLINE VertexDescription BuildDescription(eVertexType vertex_type)
{
    switch (vertex_type) {
    case eVertexType::Slim:
        return VertexUtil::BuildDescription<eVertexType::Slim>();
    case eVertexType::Default:
        return VertexUtil::BuildDescription<eVertexType::Default>();
    case eVertexType::Skinned:
        return VertexUtil::BuildDescription<eVertexType::Skinned>();
    case eVertexType::Lightmapped:
        return VertexUtil::BuildStreamDescription(true, false);
    case eVertexType::Colored:
        return VertexUtil::BuildStreamDescription(false, true);
    case eVertexType::LightmappedColored:
        return VertexUtil::BuildStreamDescription(true, true);
    default:;
    }

    LogError("Unsupported vertex type!");

    return VertexDescription {};
}
}; // namespace VertexUtil

} // namespace fx::renderer
