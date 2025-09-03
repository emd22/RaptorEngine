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

// End packing structs
#pragma pack(pop)
