#pragma once

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx::renderer {

enum class RxVertexType
{
    eSlim,
    eDefault,
    eSkinned,
};

constexpr RxVertexType RxVertexLargestType = RxVertexType::eSkinned;


// Suppress clangd and its hate for #pragma pack
static_assert(true);

// Pack all of the structs below
#pragma pack(push, 1)

template <RxVertexType TType>
struct RxVertex
{
};

template <>
struct RxVertex<RxVertexType::eSlim>
{
    float32 Position[3];
};

template <>
struct RxVertex<RxVertexType::eDefault>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];
};

template <>
struct RxVertex<RxVertexType::eSkinned>
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
struct std::formatter<fx::renderer::RxVertex<fx::renderer::RxVertexType::eDefault>>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    auto format(const fx::renderer::RxVertex<fx::renderer::RxVertexType::eDefault>& obj, FmtContext& ctx) const
    {
        return std::format_to(ctx.out(), "( {:.04}, {:.04}, {:.04} )", static_cast<float>(obj.Position[0]),
                              static_cast<float>(obj.Position[1]), static_cast<float>(obj.Position[2]));
    }
};


namespace fx::renderer::RxVertexUtil {

/**
 * @brief Returns the size of a vertex given a vertex type.
 */
FX_FORCE_INLINE uint32 GetSize(fx::renderer::RxVertexType type)
{
    if (type == RxVertexType::eSlim) {
        return sizeof(RxVertex<RxVertexType::eSlim>);
    }
    else if (type == RxVertexType::eDefault) {
        return sizeof(RxVertex<RxVertexType::eDefault>);
    }
    else if (type == RxVertexType::eSkinned) {
        return sizeof(RxVertex<RxVertexType::eSkinned>);
    }

    return 0;
}

/**
 * @brief Returns the position values given a vertex struct.
 */
template <RxVertexType TVertexType>
Vec3f GetPosition(const RxVertex<TVertexType>& vertex)
{
    static_assert(
        offsetof(RxVertex<RxVertexType::eSlim>, Position) == offsetof(RxVertex<RxVertexType::eDefault>, Position) &&
        offsetof(RxVertex<RxVertexType::eDefault>, Position) == offsetof(RxVertex<RxVertexType::eSkinned>, Position));

    return Vec3f(vertex.Position);
}
}; // namespace fx::renderer::RxVertexUtil
