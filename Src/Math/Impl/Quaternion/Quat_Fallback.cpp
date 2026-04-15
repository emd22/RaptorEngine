#include <Core/Defines.hpp>

#ifdef FX_NO_SIMD

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Quat.h>
#include <math.h>

#include <Math/MathUtil.hpp>
#include <Math/Quat.hpp>

Quat::Quat(const JPH::Quat& other)
{
    X = other.GetX();
    Y = other.GetY();
    Z = other.GetZ();
    W = other.GetW();
}


bool Quat::IsCloseTo(const JPH::Quat& other, const float32 tolerance) const
{
    return IsCloseTo(Quat(other.GetX(), other.GetY(), other.GetZ(), other.GetW()));
}

#endif
