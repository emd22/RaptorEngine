#pragma once

#include <Core/Types.hpp>

namespace fx {
namespace renderer {

enum class eSamplerFilter : uint8
{
    Nearest,
    Linear,
};

enum class eSamplerAddressMode : uint8
{
    Repeat,
    ClampToBorder,
};

enum class eSamplerBorderColor : uint8
{
    IntBlack,
    FloatBlack,
    IntWhite,
    FloatWhite,
    IntTransparent,
    FloatTransparent,
};

enum class eSamplerCompareOp : uint8
{
    None,
    Equal,
    Less,
    Greater,
    LessOrEqual,
    GreaterOrEqual,
};

struct SamplerProps
{
    FX_FORCE_INLINE SamplerProps& SetNearest()
    {
        MinFilter = eSamplerFilter::Nearest;
        MagFilter = eSamplerFilter::Nearest;
        MipFilter = eSamplerFilter::Nearest;
        return *this;
    }

    FX_FORCE_INLINE SamplerProps& SetLinear()
    {
        MinFilter = eSamplerFilter::Linear;
        MagFilter = eSamplerFilter::Linear;
        MipFilter = eSamplerFilter::Linear;
        return *this;
    }

    eSamplerFilter MinFilter = eSamplerFilter::Linear;
    eSamplerFilter MagFilter = eSamplerFilter::Linear;
    eSamplerFilter MipFilter = eSamplerFilter::Linear;

    eSamplerAddressMode AddressMode = eSamplerAddressMode::Repeat;
    eSamplerBorderColor BorderColor = eSamplerBorderColor::IntBlack;
    eSamplerCompareOp CompareOp = eSamplerCompareOp::None;

    float32 MinLOD = 0.0f;
    float32 MaxLOD = 0.0f;
};
} // namespace renderer
} // namespace fx
