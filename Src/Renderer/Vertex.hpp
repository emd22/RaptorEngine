#pragma once

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx::renderer {

enum class eVertexType
{
    Slim,
    Default,
    Skinned,
};

constexpr eVertexType VertexLargestType = eVertexType::Skinned;


// Suppress clangd and its hate for #pragma pack
static_assert(true);

// Pack all of the structs below
#pragma pack(push, 1)

template <eVertexType TType>
struct Vertex
{
};

template <>
struct Vertex<eVertexType::Slim>
{
    float32 Position[3];
};

template <>
struct Vertex<eVertexType::Default>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];
};

template <>
struct Vertex<eVertexType::Skinned>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];
    uint32 BoneIds[4];      /// Skinning bone ids
    float32 BoneWeights[4]; /// Skinning bone weights
};


// End packing structs
#pragma pack(pop)

} // namespace fx::renderer


template <>
struct std::formatter<fx::renderer::Vertex<fx::renderer::eVertexType::Default>>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    auto format(const fx::renderer::Vertex<fx::renderer::eVertexType::Default>& obj, FmtContext& ctx) const
    {
        return std::format_to(ctx.out(), "( {:.04}, {:.04}, {:.04} )", static_cast<float>(obj.Position[0]),
                              static_cast<float>(obj.Position[1]), static_cast<float>(obj.Position[2]));
    }
};


namespace fx::renderer::VertexUtil {

/**
 * @brief Returns the size of a vertex given a vertex type.
 */
FX_FORCE_INLINE uint32 GetSize(fx::renderer::eVertexType type)
{
    if (type == eVertexType::Slim) {
        return sizeof(Vertex<eVertexType::Slim>);
    }
    else if (type == eVertexType::Default) {
        return sizeof(Vertex<eVertexType::Default>);
    }
    else if (type == eVertexType::Skinned) {
        return sizeof(Vertex<eVertexType::Skinned>);
    }

    return 0;
}

/**
 * @brief Returns the position values given a vertex struct.
 */
template <eVertexType TVertexType>
Vec3f GetPosition(const Vertex<TVertexType>& vertex)
{
    static_assert(offsetof(Vertex<eVertexType::Slim>, Position) == offsetof(Vertex<eVertexType::Default>, Position) &&
                  offsetof(Vertex<eVertexType::Default>, Position) == offsetof(Vertex<eVertexType::Skinned>, Position));

    return Vec3f(vertex.Position);
}
}; // namespace fx::renderer::VertexUtil
