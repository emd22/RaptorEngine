#include "RagdollMath.hpp"

#include <cmath>
#include <limits>

namespace fx::physics::RagdollMath {

namespace {

float32 Cross2(const Vec2f origin, const Vec2f a, const Vec2f b) { return a.CrossOffset(b, origin); }

float32 Distance2(const Vec2f a, const Vec2f b) { return std::hypot(a.X - b.X, a.Y - b.Y); }

Vec2f ClosestOnSegment(const Vec2f a, const Vec2f b, const Vec2f p)
{
	const float32 dx = b.X - a.X;
	const float32 dy = b.Y - a.Y;
	const float32 length_sq = dx * dx + dy * dy;

	if (length_sq < 1e-10f) {
		return a;
	}

	const float32 t = std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length_sq, 0.0f, 1.0f);

	return Vec2f { a.X + dx * t, a.Y + dy * t };
}

} // namespace

JPH::Mat44 RotationFromMatrix(const Mat4f& m)
{
	const JPH::Vec3 x = RowAsVec3(m, 0).Normalized();

	JPH::Vec3 y = RowAsVec3(m, 1);
	y = (y - x * y.Dot(x)).Normalized();

	const JPH::Vec3 z = x.Cross(y);

	return JPH::Mat44(JPH::Vec4(x, 0.0f), JPH::Vec4(y, 0.0f), JPH::Vec4(z, 0.0f), JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

Mat4f MatrixFromRotationTranslation(const JPH::Mat44& rotation, const JPH::Vec3& position)
{
	const JPH::Vec3 x = rotation.GetAxisX();
	const JPH::Vec3 y = rotation.GetAxisY();
	const JPH::Vec3 z = rotation.GetAxisZ();

	return Mat4f(Vec4f(x.GetX(), x.GetY(), x.GetZ(), 0.0f), Vec4f(y.GetX(), y.GetY(), y.GetZ(), 0.0f),
				 Vec4f(z.GetX(), z.GetY(), z.GetZ(), 0.0f),
				 Vec4f(position.GetX(), position.GetY(), position.GetZ(), 1.0f));
}

void NormalizeRotationRows(Mat4f& m)
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

void ScaleRotationRows(Mat4f& m, const JPH::Vec3& scale)
{
	for (uint32 row = 0; row < 3; row++) {
		float32* r = &m.RawData[row * 4];
		const float32 row_scale = scale[row];

		r[0] *= row_scale;
		r[1] *= row_scale;
		r[2] *= row_scale;
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

uint32 BuildHull(Vec2f* points, uint32 count, Vec2f* out_hull)
{
	if (count < 3) {
		std::copy(points, points + count, out_hull);
		return count;
	}

	std::sort(points, points + count,
			  [](const Vec2f& a, const Vec2f& b) { return (a.X < b.X) || (a.X == b.X && a.Y < b.Y); });

	uint32 size = 0;

	// Pops hull vertices until appending `point` makes a strict counter-clockwise turn, never shrinking
	// the hull below `min_size`, then appends it.
	auto push = [&](const Vec2f& point, uint32 min_size)
	{
		while (size >= min_size && Cross2(out_hull[size - 2], out_hull[size - 1], point) <= 0.0f) {
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

Vec2f ClosestOnHull(const Vec2f* hull, uint32 count, const Vec2f p, float32& out_signed_distance)
{
	if (count == 0) {
		out_signed_distance = 0.0f;
		return p;
	}

	if (count == 1) {
		out_signed_distance = Distance2(p, hull[0]);
		return hull[0];
	}

	bool inside = (count >= 3);
	Vec2f closest = hull[0];
	float32 closest_distance = std::numeric_limits<float32>::max();
	const uint32 edge_count = (count == 2) ? 1 : count;

	for (uint32 i = 0; i < edge_count; i++) {
		const Vec2f a = hull[i];
		const Vec2f b = hull[(i + 1) % count];

		if (Cross2(a, b, p) < 0.0f) {
			inside = false;
		}

		const Vec2f on_edge = ClosestOnSegment(a, b, p);
		const float32 distance = Distance2(p, on_edge);

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

Vec2f GetHullCentroid(const Vec2f* hull, uint32 count)
{
	Vec2f average = Vec2f::scZero;

	for (uint32 i = 0; i < count; i++) {
		average.X += hull[i].X / static_cast<float32>(count);
		average.Y += hull[i].Y / static_cast<float32>(count);
	}

	float32 area = 0.0f;
	Vec2f weighted = Vec2f::scZero;

	for (uint32 i = 0; i < count; i++) {
		const Vec2f a { hull[i].X - average.X, hull[i].Y - average.Y };
		const Vec2f b { hull[(i + 1) % count].X - average.X, hull[(i + 1) % count].Y - average.Y };
		const float32 cross = a.X * b.Y - a.Y * b.X;

		area += cross;
		weighted.X += (a.X + b.X) * cross;
		weighted.Y += (a.Y + b.Y) * cross;
	}

	if (std::abs(area) < 1e-8f) {
		return average;
	}

	return Vec2f { average.X + weighted.X / (3.0f * area), average.Y + weighted.Y / (3.0f * area) };
}

} // namespace fx::physics::RagdollMath
