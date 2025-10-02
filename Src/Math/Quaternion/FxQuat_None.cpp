#include <Core/FxDefines.hpp>

#ifdef FX_USE_NEON

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Quat.h>
#include <math.h>

#include <Math/FxMathUtil.hpp>
#include <Math/FxQuat.hpp>

bool FxQuat::IsCloseTo(const JPH::Quat& other, const float32 tolerance) const
{
    return IsCloseTo(FxQuat(other.GetX(), other.GetY(), other.GetZ(), other.GetW()));
}

#endif
