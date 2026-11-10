#include <Core/Defines.hpp>

#ifdef FX_USE_NEON


#include <arm_neon.h>

#include <Core/Log.hpp>
#include <Math/FVec3.hpp>
#include <Math/MathUtil.hpp>
#include <Math/NeonUtil.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec4.hpp>

namespace fx {

const FVec3 FVec3::sZero = FVec3(0.0f, 0.0f, 0.0f);
const FVec3 FVec3::sOne = FVec3(1.0f, 1.0f, 1.0f);

const FVec3 FVec3::sUp = FVec3(0.0f, 1.0f, 0.0f);
const FVec3 FVec3::sRight = FVec3(1.0f, 0.0f, 0.0f);
const FVec3 FVec3::sForward = FVec3(0.0f, 0.0f, 1.0f);


FVec3::FVec3(const JPH::Vec3& other) { FromJoltVec3(other); }
FVec3::FVec3(const Vec4f& other) { mIntrin = other.mIntrin; }


void FVec3::Print() const { LogInfo("FVec3 {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }


FVec3 FVec3::CrossSlow(const FVec3 other) const
{
	const float32 ax = mData[0];
	const float32 bx = other.mData[0];

	const float32 ay = mData[1];
	const float32 by = other.mData[1];

	const float32 az = mData[2];
	const float32 bz = other.mData[2];

	return FVec3(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}


FVec3 FVec3::Rotate(const Quat rotation) const
{
	// v' = q * v * conj(q), with v as the pure quaternion (x, y, z, 0)
	const Quat vec = Quat(vsetq_lane_f32(0.0f, mIntrin, 3));
	const Quat result = rotation * vec * rotation.Conjugate();

	return FVec3(vsetq_lane_f32(0.0f, result.mIntrin, 3));
}


void FVec3::ToJoltVec3(JPH::RVec3& jolt_vec) const { jolt_vec.mValue = mIntrin; }
void FVec3::FromJoltVec3(const JPH::RVec3& jolt_vec) { mIntrin = jolt_vec.mValue; }

bool FVec3::IsCloseTo(const JPH::Vec3& other, const float32 tolerance) const
{
	return IsCloseTo(other.mValue, tolerance);
}


bool FVec3::operator==(const JPH::Vec3& other) const { return (*this) == FVec3(other.mValue); }

} // namespace fx

#endif
