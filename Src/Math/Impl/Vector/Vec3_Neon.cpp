#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <arm_neon.h>

#include <Core/Log.hpp>
#include <Math/MathUtil.hpp>
#include <Math/NeonUtil.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx {

const Vec3f Vec3f::sZero = Vec3f(0.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sOne = Vec3f(1.0f, 1.0f, 1.0f);

const Vec3f Vec3f::sUp = Vec3f(0.0f, 1.0f, 0.0f);
const Vec3f Vec3f::sRight = Vec3f(1.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sForward = Vec3f(0.0f, 0.0f, 1.0f);


Vec3f::Vec3f(const Vec4f& other) { mIntrin = other.mIntrin; }


void Vec3f::Print() const { LogInfo("Vec3f {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }


Vec3f Vec3f::CrossSlow(const Vec3f& other) const
{
	const float32 ax = mData[0];
	const float32 bx = other.mData[0];

	const float32 ay = mData[1];
	const float32 by = other.mData[1];

	const float32 az = mData[2];
	const float32 bz = other.mData[2];

	return Vec3f(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}


Vec3f Vec3f::Rotate(const Quat& rotation) const
{
	// v' = q * v * conj(q), with v as the pure quaternion (x, y, z, 0)
	const Quat vec = Quat(vsetq_lane_f32(0.0f, mIntrin, 3));
	const Quat result = rotation * vec * rotation.Conjugate();

	// W is zero in exact arithmetic; clear any rounding residue so the Vec3f's W lane stays 0
	return Vec3f(vsetq_lane_f32(0.0f, result.mIntrin, 3));
}


Vec3f Vec3f::Cross(const Vec3f& other) const { return Vec3f(Neon::Cross(mIntrin, other.mIntrin)); }





} // namespace fx

#endif
