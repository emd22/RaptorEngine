#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"
#include "RagdollBalance.hpp"
#include "RagdollMath.hpp"

#include <Color.hpp>
#include <Engine.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scKinematicDim = 0.45f;
constexpr float32 scMinVelocityDraw = 0.5f;
constexpr float32 scVelocityScale = 0.15f;
constexpr float32 scComCrossSize = 0.25f;
constexpr float32 scComBoxHalfExtent = 0.04f;
constexpr float32 scComGroundCrossSize = 0.12f;
constexpr float32 scMarkerSize = 0.06f;

const Color scPartColors[] = {
	Color::FromRGBA(235, 235, 235, 255),
	Color::FromRGBA(255, 60, 60, 255),
	Color::FromRGBA(70, 170, 255, 255),
	Color::FromRGBA(80, 230, 90, 255),
};
const Color scLinkColor = Color::FromRGBA(255, 220, 60, 255);
const Color scVelocityColor = Color::FromRGBA(255, 70, 220, 255);
const Color scComColor = Color::FromRGBA(255, 140, 0, 255);
const Color scSupportColor = Color::FromRGBA(90, 220, 255, 255);

const Color scCaptureColor = Color::FromRGBA(255, 230, 40, 255);
const Color scCaptureOutsideColor = Color::FromRGBA(255, 60, 40, 255);
const Color scSupportCenterColor = Color::FromRGBA(90, 220, 255, 255);
const Color scSwingColor = Color::FromRGBA(255, 70, 220, 255);

Color GetTorqueColor(float32 fraction)
{
	const float32 t = std::clamp(fraction, 0.0f, 1.0f);

	return Color::FromRGBA(static_cast<uint8>(70.0f + 185.0f * t), static_cast<uint8>(230.0f - 170.0f * t), 90, 255);
}

Color Dim(const Color color, float32 factor)
{
	return Color::FromRGBA(static_cast<uint8>(color.R * factor), static_cast<uint8>(color.G * factor),
						   static_cast<uint8>(color.B * factor), 255);
}

void DrawGroundCross(const JPH::Vec3 point, const JPH::Vec3 axis_a, const JPH::Vec3 axis_b, float32 size,
					 const Color color)
{
	renderer::gDebugDraw->Line(ToVec3f(point - axis_a * size), ToVec3f(point + axis_a * size), color);
	renderer::gDebugDraw->Line(ToVec3f(point - axis_b * size), ToVec3f(point + axis_b * size), color);
}

} // namespace

void Ragdoll::DebugDraw() const
{
	if (mpRagdoll == nullptr) {
		return;
	}

	SizedArray<Vec3f> centers;
	InitFilled(centers, mBodies.Size, Vec3f::sZero);

	DrawDebugBodies(centers);
	DrawDebugLinks(centers);
	DrawDebugSupport();

	if (mbBalancing) {
		mBalance.DebugDraw();
	}
}

void Ragdoll::DrawDebugBodies(SizedArray<Vec3f>& centers) const
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(body_id, position, orientation);

		const JPH::Mat44 rotation = JPH::Mat44::sRotation(orientation);

		const JPH::Vec3 axis = rotation * info.LocalAxis;
		const JPH::Vec3 center = JPH::Vec3(position) + rotation * info.LocalCenter;
		const JPH::Vec3 side = axis.GetNormalizedPerpendicular();
		const JPH::Vec3 front = side.Cross(axis);

		JPH::Vec3 x = side * info.Radius;
		JPH::Vec3 y = axis * (info.Length * 0.5f);
		JPH::Vec3 z = front * info.Radius;

		if (info.bBox) {
			const JPH::Mat44 box_rotation = JPH::Mat44::sRotation(orientation * info.BoxRotation);

			x = box_rotation.GetAxisX() * info.BoxHalfExtent.GetX();
			y = box_rotation.GetAxisY() * info.BoxHalfExtent.GetY();
			z = box_rotation.GetAxisZ() * info.BoxHalfExtent.GetZ();
		}

		const Mat4f box(Vec4f(x.GetX(), x.GetY(), x.GetZ(), 0.0f), Vec4f(y.GetX(), y.GetY(), y.GetZ(), 0.0f),
						Vec4f(z.GetX(), z.GetY(), z.GetZ(), 0.0f),
						Vec4f(center.GetX(), center.GetY(), center.GetZ(), 1.0f));

		centers[i] = ToVec3f(center);

		Color color = scPartColors[static_cast<uint32>(info.Part)];

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			color = Dim(color, scKinematicDim);
		}

		renderer::gDebugDraw->WireBox(box, color);

		const JPH::Vec3 velocity = body_interface.GetLinearVelocity(body_id);

		if (velocity.Length() > scMinVelocityDraw) {
			renderer::gDebugDraw->Line(centers[i], centers[i] + Vec3f(velocity) * scVelocityScale, scVelocityColor);
		}
	}
}

void Ragdoll::DrawDebugLinks(const SizedArray<Vec3f>& centers) const
{
	for (uint32 i = 0; i < mBodies.Size; i++) {
		const int32 parent = mBodies[i].ParentBody;

		if (parent < 0) {
			continue;
		}

		Color link_color = scLinkColor;

		if (mBodies[i].Mode == eRagdollBodyMode::Powered) {
			link_color = GetTorqueColor(GetMotorTorqueFraction(i));
		}

		renderer::gDebugDraw->Line(centers[i], centers[parent], link_color);
	}
}

void Ragdoll::DrawDebugSupport() const
{
	const JPH::Vec3 up = GetUpFromGravity(gPhysics->pBackend->PhysicsSystem.GetGravity());
	const JPH::Vec3 axis_u = up.GetNormalizedPerpendicular();
	const JPH::Vec3 axis_v = up.Cross(axis_u);

	const Vec3f center_of_mass = GetCenterOfMass();

	renderer::gDebugDraw->Line(center_of_mass - Vec3f::sRight * scComCrossSize,
							   center_of_mass + Vec3f::sRight * scComCrossSize, scComColor);
	renderer::gDebugDraw->Line(center_of_mass - Vec3f::sUp * scComCrossSize,
							   center_of_mass + Vec3f::sUp * scComCrossSize, scComColor);
	renderer::gDebugDraw->Line(center_of_mass - Vec3f::sForward * scComCrossSize,
							   center_of_mass + Vec3f::sForward * scComCrossSize, scComColor);
	renderer::gDebugDraw->WireBox(center_of_mass, Vec3f(scComBoxHalfExtent), Quat::scIdentity, scComColor);

	SizedArray<Vec2f> support_points;
	support_points.InitCapacity(mBodies.Size);
	float32 support_height_sum = 0.0f;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		BodyGroundContact contact;

		if (!GetBodyGroundContact(i, contact) || ToJolt(contact.Normal).Dot(up) < scSupportNormalMinUp) {
			continue;
		}

		const JPH::Vec3 point = ToJolt(contact.Point);

		support_points.Insert(Vec2f { point.Dot(axis_u), point.Dot(axis_v) });
		support_height_sum += point.Dot(up);
	}

	if (support_points.IsEmpty()) {
		return;
	}

	const float32 support_height = support_height_sum / static_cast<float32>(support_points.Size);
	const JPH::Vec3 com = ToJolt(center_of_mass);
	const JPH::Vec3 com_on_support = com - up * (com.Dot(up) - support_height);

	renderer::gDebugDraw->Line(center_of_mass, ToVec3f(com_on_support), scComColor);
	DrawGroundCross(com_on_support, axis_u, axis_v, scComGroundCrossSize, scComColor);

	SizedArray<Vec2f> hull;
	hull.InitSize(support_points.Size + 1);

	const uint32 hull_count = BuildHull(support_points.pData, static_cast<uint32>(support_points.Size), hull.pData);

	if (hull_count < 2) {
		return;
	}

	const auto to_world = [&](const Vec2f point)
	{ return ToVec3f(axis_u * point.X + axis_v * point.Y + up * support_height); };

	for (uint32 i = 0; i < hull_count; i++) {
		renderer::gDebugDraw->Line(to_world(hull[i]), to_world(hull[(i + 1) % hull_count]), scSupportColor);
	}
}

void RagdollBalance::DebugDraw() const
{
	if (!mbValid || mStatus.State == eBalanceState::Off) {
		return;
	}

	const JPH::Vec3 side = mUp.Cross(mForward);

	Color capture_color = scCaptureColor;

	if (mCaptureOutside > 0.0f) {
		capture_color = scCaptureOutsideColor;
	}

	DrawGroundCross(mCapturePoint, mForward, side, scMarkerSize, capture_color);
	DrawGroundCross(mSupportCenter, mForward, side, scMarkerSize, scSupportCenterColor);
	renderer::gDebugDraw->Line(ToVec3f(ToGround(mCenterOfMass)), ToVec3f(mCapturePoint), capture_color);

	if (mStatus.State == eBalanceState::Stepping) {
		DrawGroundCross(mSwing.Landing, mForward, side, scMarkerSize, scSwingColor);
		renderer::gDebugDraw->Line(ToVec3f(mLegs[mSwing.Leg].AnklePosition), ToVec3f(mSwing.AnkleTarget), scSwingColor);
	}
}

} // namespace fx::physics
