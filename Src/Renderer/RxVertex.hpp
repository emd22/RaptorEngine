#pragma once

#include "Backend/RxGpuBuffer.hpp"

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Math/FxVec3.hpp>

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
    FxVertexUV = 0x04,
};

FX_DEFINE_ENUM_AS_FLAGS(FxVertexFlags);


// Pack all of the structs below
#pragma pack(push, 1)

template <FxVertexFlags TComponents>
struct RxVertex;

template <>
struct RxVertex<FxVertexPosition>
{
    static constexpr FxVertexFlags Components = FxVertexPosition;

    float32 Position[3];
} __attribute__((packed));

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal>
{
    static constexpr FxVertexFlags Components = (FxVertexPosition | FxVertexNormal);

    float32 Position[3];
    float32 Normal[3];
} __attribute__((packed));

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>
{
    static constexpr FxVertexFlags Components = (FxVertexPosition | FxVertexNormal | FxVertexUV);

    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
} __attribute__((packed));

// End packing structs
#pragma pack(pop)

using RxVertexDefault = RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>;

template <>
struct std::formatter<RxVertexDefault>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    constexpr auto format(const RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>& obj, FmtContext& ctx) const
    {
        return std::format_to(ctx.out(), "( {:.04}, {:.04}, {:.04} )", static_cast<float>(obj.Position[0]),
                              static_cast<float>(obj.Position[1]), static_cast<float>(obj.Position[2]));
    }
};

class RxVertexList
{
public:
    using VertexType = RxVertexDefault;

public:
    RxVertexList() = default;

    void Create(const FxSizedArray<float32>& positions, const FxSizedArray<float32>& normals,
                const FxSizedArray<float32>& uvs);

    void Create(const FxSizedArray<float32>& positions);

    FxVec3f CalculateDimensionsFromPositions();

    void Destroy();

    ~RxVertexList() = default;

public:
    FxVec3f Dimensions = FxVec3f::Zero;

    RxGpuBuffer<VertexType> mGpuBuffer {};
    FxSizedArray<VertexType> mLocalBuffer {};
};
