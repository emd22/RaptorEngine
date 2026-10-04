#pragma once

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Renderer/Vertex.hpp>
#include <raptor_ffi.h>

namespace fx::renderer {

struct VertexDescription
{
    VkVertexInputBindingDescription Binding;
    SizedArray<VkVertexInputAttributeDescription> Attributes;

    bool bIsInited : 1 = false;
};

static_assert(sizeof(Vertex<eVertexType::Slim>) == RX_VERTEX_SLIM_SIZE);
static_assert(sizeof(Vertex<eVertexType::Default>) == RX_VERTEX_DEFAULT_SIZE);
static_assert(sizeof(Vertex<eVertexType::Skinned>) == RX_VERTEX_SKINNED_SIZE);
static_assert(offsetof(Vertex<eVertexType::Default>, Normal) == RX_VERTEX_NORMAL_OFFSET);
static_assert(offsetof(Vertex<eVertexType::Default>, UV) == RX_VERTEX_UV_OFFSET);
static_assert(offsetof(Vertex<eVertexType::Default>, Tangent) == RX_VERTEX_TANGENT_OFFSET);
static_assert(offsetof(Vertex<eVertexType::Skinned>, BoneIds) == RX_VERTEX_BONE_IDS_OFFSET);
static_assert(offsetof(Vertex<eVertexType::Skinned>, BoneWeights) == RX_VERTEX_BONE_WEIGHTS_OFFSET);

namespace VertexUtil {

FX_FORCE_INLINE VertexDescription BuildDescription(eVertexType vertex_type)
{
    constexpr size_t cMaxAttributes = 6;

    VertexDescription description {};
    VkVertexInputAttributeDescription attributes[cMaxAttributes];

    const size_t count = rx_vertex_description(static_cast<uint32>(vertex_type), &description.Binding, attributes,
                                               cMaxAttributes);

    if (count == 0) {
        LogError("Unsupported vertex type!");
        return VertexDescription {};
    }

    description.Attributes.InitSize(count);
    std::memcpy(description.Attributes.pData, attributes, count * sizeof(VkVertexInputAttributeDescription));
    description.bIsInited = true;

    return description;
}

}; // namespace VertexUtil

} // namespace fx::renderer
