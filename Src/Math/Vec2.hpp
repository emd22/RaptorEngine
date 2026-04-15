#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx {


template <typename Type>
class Vec2Base
{
public:
    constexpr Vec2Base() = default;

    constexpr Vec2Base(Type x, Type y) : X(x), Y(y) {}

    constexpr explicit Vec2Base(Type scalar) : X(scalar), Y(scalar) {}

    Vec2Base operator+(const Vec2Base& other) const
    {
        Vec2Base result = *this;
        result += other;
        return result;
    }

    Vec2Base operator-(const Vec2Base& other) const
    {
        Vec2Base result = *this;
        result -= other;
        return result;
    }

    Vec2Base operator*(const Vec2Base& other) const
    {
        Vec2Base result = *this;
        result *= other;
        return result;
    }

    Vec2Base& operator+=(const Vec2Base& other)
    {
        X += other.X;
        Y += other.Y;

        return *this;
    }

    Vec2Base& operator-=(const Vec2Base& other)
    {
        X -= other.X;
        Y -= other.Y;

        return *this;
    }

    Vec2Base& operator*=(const Vec2Base& other)
    {
        X *= other.X;
        Y *= other.Y;

        return *this;
    }

    Vec2Base operator*(float scalar) const
    {
        Vec2Base result = *this;
        result.X *= scalar;
        result.Y *= scalar;
        return result;
    }

    /**
     * @brief Checks if a vector is **exactly** equal to another vector. This does not account for floating point error.
     */
    bool operator==(const Vec2Base<Type>& other) const { return (X == other.X && Y == other.Y); }

    Vec2Base<Type>& operator=(const Vec2Base<Type>& other)
    {
        X = other.X;
        Y = other.Y;
        return *this;
    }

    void Set(Type x, Type y)
    {
        X = x;
        Y = y;
    }

    Type GetX() const { return X; }
    Type GetY() const { return Y; }

    void SetX(Type x) { X = x; }
    void SetY(Type y) { Y = y; }

    Type Width() const { return GetX(); }
    Type Height() const { return GetY(); }

    static const Vec2Base<Type> sZero;

public:
    union alignas(16)
    {
        float32 mData[2];

        struct
        {
            Type X, Y;
        };
    };
};

// Declaration of Vec2::sZero
template <typename Type>
const Vec2Base<Type> Vec2Base<Type>::sZero = Vec2Base<Type>(0, 0);

using Vec2f = Vec2Base<float32>;
using Vec2d = Vec2Base<float64>;

using Vec2i = Vec2Base<int32>;
using Vec2u = Vec2Base<uint32>;

} // namespace fx

template <>
struct std::formatter<fx::Vec2i>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec2i& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", obj.X, obj.Y);
    }
};

template <>
struct std::formatter<fx::Vec2u>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec2u& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", obj.X, obj.Y);
    }
};

template <>
struct std::formatter<fx::Vec2f>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec2f& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04})", obj.X, obj.Y);
    }
};


template <>
struct std::formatter<fx::Vec2d>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Vec2d& obj, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "({:.04}, {:.04})", obj.X, obj.Y);
    }
};
