#pragma once

#include <Core/Types.hpp>

template <typename Type>
class Vec2Base {
public:
    Vec2Base() = default;

    Vec2Base(Type x, Type y)
        : X(x), Y(y)
    {
    }

    explicit Vec2Base(Type scalar)
        : X(scalar), Y(scalar)
    {
    }

    Vec2Base operator + (const Vec2Base &other) const
    {
        Vec2Base result = *this;
        result += other;
        return result;
    }

    Vec2Base operator - (const Vec2Base &other) const
    {
        Vec2Base result = *this;
        result -= other;
        return result;
    }

    Vec2Base operator * (const Vec2Base &other) const
    {
        Vec2Base result = *this;
        result *= other;
        return result;
    }

    Vec2Base &operator += (const Vec2Base &other)
    {
        X += other.X;
        Y += other.Y;

        return *this;
    }

    Vec2Base &operator -= (const Vec2Base &other)
    {
        X -= other.X;
        Y -= other.Y;

        return *this;
    }

    Vec2Base &operator *= (const Vec2Base &other)
    {
        X *= other.X;
        Y *= other.Y;

        return *this;
    }

    Vec2Base operator * (float scalar) const
    {
        Vec2Base result = *this;
        result.X *= scalar;
        result.Y *= scalar;
        return result;
    }

    Vec2Base<Type> &operator = (const Vec2Base<Type> &other)
    {
        X = other.X;
        Y = other.Y;
        return *this;
    }

    void Set(Type x, Type y) { X = x; Y = y; }

    Type GetX() const { return X; }
    Type GetY() const { return Y; }

    void SetX(Type x) { X = x; }
    void SetY(Type y) { Y = y; }

    Type Width() const { return GetX(); }
    Type Height() const { return GetY(); }

    static const Vec2Base<Type> Zero;

public:
    Type X;
    Type Y;
};

// Declaration of Vec2::Zero
template <typename Type>
const Vec2Base<Type> Vec2Base<Type>::Zero = Vec2Base<Type>(0, 0);

using Vec2f = Vec2Base<float32>;
using Vec2d = Vec2Base<float64>;

using Vec2i = Vec2Base<int32>;
using Vec2u = Vec2Base<uint32>;
