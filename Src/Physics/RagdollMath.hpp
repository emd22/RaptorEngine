#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Mat44.h>
#include <ThirdParty/Jolt/Math/Quat.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <algorithm>

namespace fx::physics::RagdollMath {

constexpr float32 scSupportNormalMinUp = 0.7f;

constexpr uint32 scSoleCorners = 4;
constexpr float32 scSoleCornerSigns[scSoleCorners][2] = {
	{ -1.0f, -1.0f },
	{ 1.0f, -1.0f },
	{ 1.0f, 1.0f },
	{ -1.0f, 1.0f },
};

FX_FORCE_INLINE JPH::Vec3 RowAsVec3(const Mat4f& m, uint32 row) { return JPH::Vec3(m.Rows[row].mIntrin); }

FX_FORCE_INLINE JPH::Vec3 GetUpFromGravity(const JPH::Vec3 gravity)
{
	return (-gravity).NormalizedOr(JPH::Vec3::sAxisY());
}

JPH::Mat44 RotationFromMatrix(const Mat4f& m);
Mat4f MatrixFromRotationTranslation(const JPH::Mat44& rotation, const JPH::Vec3& position);
void NormalizeRotationRows(Mat4f& m);
void ScaleRotationRows(Mat4f& m, const JPH::Vec3& scale);

JPH::Vec3 GetRotationVector(const JPH::Quat& rotation);
JPH::Quat IntegrateRotation(const JPH::Quat& rotation, const JPH::Vec3 angular_velocity, float32 time);
JPH::Quat FrameRotation(const JPH::Vec3 local_axis, const JPH::Vec3 local_hinge, const JPH::Vec3 world_axis,
						const JPH::Vec3 world_hinge);

/**
 * @brief Convex hull of vertices via monotone chain. Writes the hull to `out_hull` counter-clockwise  with no repeated
 * closing vertex.
 * @returns The vertex count
 */
uint32 BuildHull(Vec2f* points, uint32 count, Vec2f* out_hull);
Vec2f ClosestOnHull(const Vec2f* hull, uint32 count, const Vec2f p, float32& out_signed_distance);
Vec2f GetHullCentroid(const Vec2f* hull, uint32 count);

} // namespace fx::physics::RagdollMath
