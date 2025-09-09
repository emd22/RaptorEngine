#pragma once

#include <Core/Types.hpp>

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
    FxVertexUV = 0x04,
};

// Pack all of the structs below
#pragma pack(push, 1)

template <int8 Flags>
struct RxVertex;

template <>
struct RxVertex<FxVertexPosition>
{
    float32 Position[3];
} __attribute__((packed));

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal>
{
    float32 Position[3];
    float32 Normal[3];
} __attribute__((packed));

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
} __attribute__((packed));

template <>
struct std::formatter<RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    constexpr auto format(const RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>& obj, FmtContext& ctx) const
    {
        return std::format_to(ctx.out(), "( {:.04}, {:.04}, {:.04} )", static_cast<float>(obj.Position[0]),
                              static_cast<float>(obj.Position[1]), static_cast<float>(obj.Position[2]));
    }
};

// End packing structs
#pragma pack(pop)
