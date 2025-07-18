#pragma once

#include <Core/Types.hpp>

template <typename Type>
class FxVec2Base {
public:
    FxVec2Base() = default;

    FxVec2Base(Type x, Type y)
        : X(x), Y(y)
    {
    }

    explicit FxVec2Base(Type scalar)
        : X(scalar), Y(scalar)
    {
    }

    FxVec2Base operator + (const FxVec2Base &other) const
    {
        FxVec2Base result = *this;
        result += other;
        return result;
    }

    FxVec2Base operator - (const FxVec2Base &other) const
    {
        FxVec2Base result = *this;
        result -= other;
        return result;
    }

    FxVec2Base operator * (const FxVec2Base &other) const
    {
        FxVec2Base result = *this;
        result *= other;
        return result;
    }

    FxVec2Base &operator += (const FxVec2Base &other)
    {
        X += other.X;
        Y += other.Y;

        return *this;
    }

    FxVec2Base &operator -= (const FxVec2Base &other)
    {
        X -= other.X;
        Y -= other.Y;

        return *this;
    }

    FxVec2Base &operator *= (const FxVec2Base &other)
    {
        X *= other.X;
        Y *= other.Y;

        return *this;
    }

    FxVec2Base operator * (float scalar) const
    {
        FxVec2Base result = *this;
        result.X *= scalar;
        result.Y *= scalar;
        return result;
    }

    FxVec2Base<Type> &operator = (const FxVec2Base<Type> &other)
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

    static const FxVec2Base<Type> Zero;

public:
    Type X;
    Type Y;
};

// Declaration of Vec2::Zero
template <typename Type>
const FxVec2Base<Type> FxVec2Base<Type>::Zero = FxVec2Base<Type>(0, 0);

using FxVec2f = FxVec2Base<float32>;
using FxVec2d = FxVec2Base<float64>;

using FxVec2i = FxVec2Base<int32>;
using FxVec2u = FxVec2Base<uint32>;
