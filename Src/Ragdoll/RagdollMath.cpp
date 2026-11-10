#include "RagdollMath.hpp"

#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <cmath>
#include <limits>

namespace fx::physics::RagdollMath {

namespace {

FVec2 ClosestOnSegment(const FVec2 a, const FVec2 b, const FVec2 p)
{
	const FVec2 edge = b - a;
	const float32 length_sq = edge.LengthSquared();

	if (length_sq < 1e-10f) {
		return a;
	}

	const float32 t = MathUtil::Clamp((p - a).Dot(edge) / length_sq, 0.0f, 1.0f);

	return a + edge * t;
}

} // namespace

GravityFrame GravityFrame::FromGravity(const JPH::Vec3 gravity)
{
	GravityFrame frame;

	frame.Up = GetUpFromGravity(gravity);
	frame.AxisU = frame.Up.GetNormalizedPerpendicular();
	frame.AxisV = frame.Up.Cross(frame.AxisU);
	frame.Gravity = gravity.Length();

	return frame;
}

GravityFrame GravityFrame::FromPhysics() { return FromGravity(gPhysics->pBackend->PhysicsSystem.GetGravity()); }

JPH::Mat44 RotationFromMatrix(const FMat4& m)
{
	const JPH::Vec3 x = RowAsVec3(m, 0).Normalized();

	JPH::Vec3 y = RowAsVec3(m, 1);
	y = (y - x * y.Dot(x)).Normalized();

	const JPH::Vec3 z = x.Cross(y);

	return JPH::Mat44(JPH::Vec4(x, 0.0f), JPH::Vec4(y, 0.0f), JPH::Vec4(z, 0.0f), JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

FMat4 MatrixFromRotationTranslation(const JPH::Mat44& rotation, const JPH::Vec3& position)
{
	const JPH::Vec3 x = rotation.GetAxisX();
	const JPH::Vec3 y = rotation.GetAxisY();
	const JPH::Vec3 z = rotation.GetAxisZ();

	return FMat4(FVec4(FVec3::FromJolt(x), 0.0f), FVec4(FVec3::FromJolt(y), 0.0f), FVec4(FVec3::FromJolt(z), 0.0f),
				 FVec4(FVec3::FromJolt(position), 1.0f));
}

void NormalizeRotationRows(FMat4& m)
{
	for (uint32 row = 0; row < 3; row++) {
		float32* r = &m.RawData[row * 4];
		const float32 length = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);

		if (length > 1e-6f) {
			r[0] /= length;
			r[1] /= length;
			r[2] /= length;
		}

		r[3] = 0.0f;
	}
}

void ScaleRotationRows(FMat4& m, const JPH::Vec3& scale)
{
	for (uint32 row = 0; row < 3; row++) {
		m.Rows[row] *= scale[row];
	}
}

JPH::Vec3 GetRotationVector(const JPH::Quat& rotation)
{
	JPH::Quat shortest = rotation;

	if (shortest.GetW() < 0.0f) {
		shortest = -shortest;
	}

	JPH::Vec3 axis;
	float32 angle = 0.0f;
	shortest.GetAxisAngle(axis, angle);

	return axis * angle;
}

JPH::Quat IntegrateRotation(const JPH::Quat& rotation, const JPH::Vec3 angular_velocity, float32 time)
{
	const JPH::Vec3 delta = angular_velocity * time;
	const float32 angle = delta.Length();

	if (angle < 1e-6f) {
		return rotation;
	}

	return (JPH::Quat::sRotation(delta / angle, angle) * rotation).Normalized();
}

JPH::Quat FrameRotation(const JPH::Vec3 local_axis, const JPH::Vec3 local_hinge, const JPH::Vec3 world_axis,
						const JPH::Vec3 world_hinge)
{
	const JPH::Vec3 axis_l = local_axis.Normalized();
	const JPH::Vec3 hinge_l = (local_hinge - axis_l * local_hinge.Dot(axis_l))
								  .NormalizedOr(axis_l.GetNormalizedPerpendicular());
	const JPH::Vec3 axis_w = world_axis.Normalized();
	const JPH::Vec3 hinge_w = (world_hinge - axis_w * world_hinge.Dot(axis_w))
								  .NormalizedOr(axis_w.GetNormalizedPerpendicular());

	const JPH::Mat44 local(JPH::Vec4(axis_l, 0.0f), JPH::Vec4(hinge_l, 0.0f), JPH::Vec4(axis_l.Cross(hinge_l), 0.0f),
						   JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
	const JPH::Mat44 world(JPH::Vec4(axis_w, 0.0f), JPH::Vec4(hinge_w, 0.0f), JPH::Vec4(axis_w.Cross(hinge_w), 0.0f),
						   JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));

	return (world * local.Transposed3x3()).GetQuaternion().Normalized();
}

uint32 BuildHull(FVec2* points, uint32 count, FVec2* out_hull)
{
	if (count < 3) {
		std::copy(points, points + count, out_hull);
		return count;
	}

	std::sort(points, points + count,
			  [](const FVec2& a, const FVec2& b) { return (a.X < b.X) || (a.X == b.X && a.Y < b.Y); });

	uint32 size = 0;

	// Pops hull vertices until appending `point` makes a strict counter-clockwise turn, never shrinking
	// the hull below `min_size`, then appends it.
	auto push = [&](const FVec2& point, uint32 min_size)
	{
		while (size >= min_size && out_hull[size - 1].CrossOffset(point, out_hull[size - 2]) <= 0.0f) {
			size--;
		}
		out_hull[size++] = point;
	};

	// Lower hull, left to right.
	for (uint32 i = 0; i < count; i++) {
		push(points[i], 2);
	}

	// Upper hull, right to left. The rightmost point already ends the lower hull, so start one before it,
	// and keep min_size above the lower hull so this pass can't pop into it.
	const uint32 upper_min_size = size + 1;

	for (uint32 i = count - 1; i > 0; i--) {
		push(points[i - 1], upper_min_size);
	}

	// The upper pass finishes by appending points[0] again, which duplicates the first vertex.
	return size - 1;
}

FVec2 ClosestOnHull(const FVec2* hull, uint32 count, const FVec2 p, float32& out_signed_distance)
{
	if (count == 0) {
		out_signed_distance = 0.0f;
		return p;
	}

	if (count == 1) {
		out_signed_distance = p.Distance(hull[0]);
		return hull[0];
	}

	bool inside = (count >= 3);
	FVec2 closest = hull[0];
	float32 closest_distance = std::numeric_limits<float32>::max();
	uint32 edge_count = count;

	// A two point hull is a single segment, so don't walk it back to the start
	if (count == 2) {
		edge_count = 1;
	}

	for (uint32 i = 0; i < edge_count; i++) {
		const FVec2 a = hull[i];
		const FVec2 b = hull[(i + 1) % count];

		if (b.CrossOffset(p, a) < 0.0f) {
			inside = false;
		}

		const FVec2 on_edge = ClosestOnSegment(a, b, p);
		const float32 distance = p.Distance(on_edge);

		if (distance < closest_distance) {
			closest_distance = distance;
			closest = on_edge;
		}
	}

	if (inside) {
		out_signed_distance = -closest_distance;
		return p;
	}

	out_signed_distance = closest_distance;
	return closest;
}

FVec2 GetHullCentroid(const FVec2* hull, uint32 count)
{
	FVec2 average = FVec2::scZero;

	for (uint32 i = 0; i < count; i++) {
		average += hull[i] / static_cast<float32>(count);
	}

	// Area-weighted centroid of the triangle fan around the vertex average, which is unbiased by vertex density
	float32 area = 0.0f;
	FVec2 weighted = FVec2::scZero;

	for (uint32 i = 0; i < count; i++) {
		const FVec2 a = hull[i] - average;
		const FVec2 b = hull[(i + 1) % count] - average;
		const float32 cross = a.Cross(b);

		area += cross;
		weighted += (a + b) * cross;
	}

	if (std::abs(area) < 1e-8f) {
		return average;
	}

	return average + weighted / (3.0f * area);
}

} // namespace fx::physics::RagdollMath
