#pragma once

#include <Core/Types.hpp>

template <typename Type>
class Vec2Base {
public:
    Vec2Base() = default;

    Vec2Base(Type x, Type y)
        : mX(x), mY(y)
    {
    }

    explicit Vec2Base(Type scalar)
        : mX(scalar), mY(scalar)
    {
    }

    Vec2Base operator + (const Vec2Base &other) const
    {
        Vec2Base result = *this;
        result.mX += other.mX;
        result.mY += other.mY;
        return result;
    }

    Vec2Base operator * (const Vec2Base &other) const
    {
        Vec2Base result = *this;
        result.mX *= other.mX;
        result.mY *= other.mY;
        return result;
    }

    Vec2Base &operator += (const Vec2Base &other)
    {
        mX += other.mX;
        mY += other.mY;

        return *this;
    }

    Vec2Base &operator -= (const Vec2Base &other)
    {
        mX -= other.mX;
        mY -= other.mY;

        return *this;
    }

    Vec2Base &operator *= (const Vec2Base &other)
    {
        mX *= other.mX;
        mY *= other.mY;

        return *this;
    }

    Vec2Base operator * (float scalar) const
    {
        Vec2Base result = *this;
        result.mX *= scalar;
        result.mY *= scalar;
        return result;
    }

    void Set(Type x, Type y) { mX = x; mY = y; }

    Type GetX() const { return mX; }
    Type GetY() const { return mY; }

    Type X() const { return GetX(); }
    Type Y() const { return GetY(); }

    void SetX(Type x) { mX = x; }
    void SetY(Type y) { mY = y; }

    Type Width() const { return GetX(); }
    Type Height() const { return GetY(); }

    static const Vec2Base<Type> Zero()
    {
        return Vec2Base<Type>(0, 0);
    };

private:
    Type mX;
    Type mY;
};

using Vec2f = Vec2Base<float32>;
using Vec2d = Vec2Base<float64>;


using Vec2i = Vec2Base<int32>;
using Vec2u = Vec2Base<uint32>;
