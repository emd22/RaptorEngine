#include "Ragdoll.hpp"
#include "RagdollBalance.hpp"
#include "RagdollMath.hpp"

#include <Color.hpp>
#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>

namespace fx::physics {

using namespace RagdollMath;

namespace {

const Color scPartColors[] = {
	Color::FromRGBA(235, 235, 235, 255), Color::FromRGBA(255, 60, 60, 255), Color::FromRGBA(70, 170, 255, 255),
	Color::FromRGBA(80, 230, 90, 255),	 Color::FromRGBA(80, 230, 90, 255),
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
	const float32 t = MathUtil::Clamp(fraction, 0.0f, 1.0f);

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
	renderer::gDebugDraw->Line(FVec3::FromJolt(point - axis_a * size), FVec3::FromJolt(point + axis_a * size), color);
	renderer::gDebugDraw->Line(FVec3::FromJolt(point - axis_b * size), FVec3::FromJolt(point + axis_b * size), color);
}

} // namespace

void Ragdoll::DebugDraw() const
{
	if (mpRagdoll == nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	SizedArray<RDSegmentState> samples;
	samples.InitSize(mSegments.Size, RDSegmentState {});

	for (uint32 i = 0; i < mSegments.Size; i++) {
		SampleSegment(body_interface, i, samples[i]);
	}

	SizedArray<FVec3> centers;
	centers.InitSize(mSegments.Size, FVec3::sZero);

	DrawDebugSegments(samples, centers);
	DrawDebugLinks(centers);
	DrawDebugSupport(samples);

	if (mbBalancing) {
		mBalance.DebugDraw();
	}
}

void Ragdoll::DrawDebugSegments(const SizedArray<RDSegmentState>& samples, SizedArray<FVec3>& out_centers) const
{
	constexpr float32 scKinematicDim = 0.45f;
	constexpr float32 scMinVelocityDraw = 0.5f;
	constexpr float32 scVelocityScale = 0.15f;

	for (uint32 i = 0; i < mSegments.Size; i++) {
		const RDSegmentInfo& info = mSegments[i];
		const RDSegmentState& sample = samples[i];
		const JPH::Quat orientation = sample.Orientation;

		const JPH::Mat44 rotation = JPH::Mat44::sRotation(orientation);

		const JPH::Vec3 axis = rotation * info.LocalAxis;
		const JPH::Vec3 center = sample.Origin + rotation * info.LocalCenter;
		const JPH::Vec3 side = axis.GetNormalizedPerpendicular();
		const JPH::Vec3 front = side.Cross(axis);

		JPH::Vec3 x = side * info.Radius;
		JPH::Vec3 y = axis * (info.Length * 0.5f);
		JPH::Vec3 z = front * info.Radius;


		// Build up a simple AABB to render as a WireBox.

		if (info.ColliderType == eRagdollShapeCollider::Box) {
			const JPH::Mat44 box_rotation = JPH::Mat44::sRotation(orientation * info.BoxRotation);

			x = box_rotation.GetAxisX() * info.BoxHalfExtent.GetX();
			y = box_rotation.GetAxisY() * info.BoxHalfExtent.GetY();
			z = box_rotation.GetAxisZ() * info.BoxHalfExtent.GetZ();
		}

		const FMat4 box(FVec4(FVec3::FromJolt(x), 0.0f), FVec4(FVec3::FromJolt(y), 0.0f),
						FVec4(FVec3::FromJolt(z), 0.0f), FVec4(FVec3::FromJolt(center), 1.0f));

		out_centers[i] = FVec3::FromJolt(center);

		Color color = scPartColors[static_cast<uint32>(info.Part)];

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			color = Dim(color, scKinematicDim);
		}

		renderer::gDebugDraw->WireBox(box, color);

		if (sample.Velocity.Length() > scMinVelocityDraw) {
			renderer::gDebugDraw->Line(out_centers[i], out_centers[i] + FVec3(sample.Velocity) * scVelocityScale,
									   scVelocityColor);
		}
	}
}

void Ragdoll::DrawDebugLinks(const SizedArray<FVec3>& centers) const
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
		const int32 parent = mSegments[i].ParentBody;

		if (parent < 0) {
			continue;
		}

		Color link_color = scLinkColor;

		if (mSegments[i].Mode == eRagdollBodyMode::Powered) {
			link_color = GetTorqueColor(GetMotorTorqueFraction(i));
		}

		renderer::gDebugDraw->Line(centers[i], centers[parent], link_color);
	}
}

void Ragdoll::DrawDebugSupport(const SizedArray<RDSegmentState>& samples) const
{
	constexpr float32 scComCrossSize = 0.25f;
	constexpr float32 scComBoxHalfExtent = 0.04f;
	constexpr float32 scComGroundCrossSize = 0.12f;

	const GravityFrame frame = GravityFrame::FromPhysics();
	const JPH::Vec3 up = frame.Up;

	JPH::Vec3 com;
	JPH::Vec3 velocity;

	if (!GetMassAverage(samples, com, velocity)) {
		return;
	}

	const FVec3 center_of_mass = FVec3::FromJolt(com);

	renderer::gDebugDraw->Line(center_of_mass - FVec3::sRight * scComCrossSize,
							   center_of_mass + FVec3::sRight * scComCrossSize, scComColor);
	renderer::gDebugDraw->Line(center_of_mass - FVec3::sUp * scComCrossSize,
							   center_of_mass + FVec3::sUp * scComCrossSize, scComColor);
	renderer::gDebugDraw->Line(center_of_mass - FVec3::sForward * scComCrossSize,
							   center_of_mass + FVec3::sForward * scComCrossSize, scComColor);
	renderer::gDebugDraw->WireBox(center_of_mass, FVec3(scComBoxHalfExtent), FQuat::scIdentity, scComColor);

	SizedArray<FVec2> support_points;
	support_points.InitCapacity(mSegments.Size);
	float32 support_height_sum = 0.0f;

	for (uint32 i = 0; i < mSegments.Size; i++) {
		BodyGroundContact contact;

		if (!GetBodyGroundContact(i, contact) || !IsSupportNormal(contact, up, scSupportNormalMinUp)) {
			continue;
		}

		const JPH::Vec3 point = FVec3::ToJolt(contact.Point);

		support_points.Insert(frame.ToPlane(point));
		support_height_sum += point.Dot(up);
	}

	if (support_points.IsEmpty()) {
		return;
	}

	const float32 support_height = support_height_sum / static_cast<float32>(support_points.Size);
	const JPH::Vec3 com_on_support = com - up * (com.Dot(up) - support_height);

	renderer::gDebugDraw->Line(center_of_mass, FVec3::FromJolt(com_on_support), scComColor);
	DrawGroundCross(com_on_support, frame.AxisU, frame.AxisV, scComGroundCrossSize, scComColor);

	SizedArray<FVec2> hull;
	hull.InitSize(support_points.Size + 1);

	const uint32 hull_count = BuildHull(support_points.pData, static_cast<uint32>(support_points.Size), hull.pData);

	if (hull_count < 2) {
		return;
	}

	const auto to_world = [&](const FVec2 point) { return FVec3::FromJolt(frame.FromPlane(point, support_height)); };

	for (uint32 i = 0; i < hull_count; i++) {
		renderer::gDebugDraw->Line(to_world(hull[i]), to_world(hull[(i + 1) % hull_count]), scSupportColor);
	}
}

void RagdollBalance::DebugDraw() const
{
	constexpr float32 scMarkerSize = 0.06f;

	if (!mbValid || mStatus.State == eBalanceState::Off) {
		return;
	}

	const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward);

	Color capture_color = scCaptureColor;

	if (mCaptureOutside > 0.0f) {
		capture_color = scCaptureOutsideColor;
	}

	DrawGroundCross(mCapturePoint, mForward, side, scMarkerSize, capture_color);
	DrawGroundCross(mSupportCenter, mForward, side, scMarkerSize, scSupportCenterColor);
	renderer::gDebugDraw->Line(FVec3::FromJolt(ToGround(mCenterOfMass)), FVec3::FromJolt(mCapturePoint), capture_color);

	if (mStatus.State == eBalanceState::Stepping) {
		DrawGroundCross(mSwing.Landing, mForward, side, scMarkerSize, scSwingColor);
		renderer::gDebugDraw->Line(FVec3::FromJolt(mLegs[mSwing.Leg].AnklePosition),
								   FVec3::FromJolt(mSwing.AnkleTarget), scSwingColor);
	}
}

} // namespace fx::physics
