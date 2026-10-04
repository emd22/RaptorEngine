#include "GrabEditor.hpp"

#ifdef FX_IS_EDITOR

#include <ThirdParty/Jolt/Physics/Body/BodyInterface.h>

#include <Color.hpp>
#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>
#include <algorithm>

namespace fx::editor {

static constexpr float32 scGrabRange = 6.0f;
static constexpr float32 scMinHoldDistance = 1.0f;

static constexpr float32 scStiffness = 20.0f;
static constexpr float32 scMaxSpeed = 20.0f;
static constexpr float32 scAngularDamping = 1.5f;

static const Color scGrabColor = Color::FromRGBA(255, 120, 40, 255);

void GrabEditor::Enter() { Release(); }

void GrabEditor::Leave() { Release(); }

void GrabEditor::Begin()
{
	Release();

	PerspectiveCamera& camera = *gWorld->Player.pCamera;

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera.Position,
															   camera.GetForwardVector() * scGrabRange);

	if (!hit.bHit) {
		return;
	}

	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	if (bodies.GetMotionType(hit.Body) != JPH::EMotionType::Dynamic) {
		return;
	}

	const JPH::RMat44 to_local = bodies.GetCenterOfMassTransform(hit.Body).InversedRotationTranslation();

	mBody = hit.Body;
	mLocalPoint = Vec3f(to_local * JPH::Vec3(hit.Point.X, hit.Point.Y, hit.Point.Z));
	mDistance = std::max((hit.Point - camera.Position).Length(), scMinHoldDistance);

	mbHolding = true;
}

void GrabEditor::Update(float32 delta_time)
{
	if (!mbHolding) {
		return;
	}

	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	if (!bodies.IsAdded(mBody) || bodies.GetMotionType(mBody) != JPH::EMotionType::Dynamic) {
		Release();
		return;
	}

	PerspectiveCamera& camera = *gWorld->Player.pCamera;

	const Vec3f target = camera.Position + camera.GetForwardVector() * mDistance;
	const Vec3f held_point = Vec3f(bodies.GetCenterOfMassTransform(mBody) *
								   JPH::Vec3(mLocalPoint.X, mLocalPoint.Y, mLocalPoint.Z));

	Vec3f velocity = (target - held_point) * scStiffness;
	const float32 speed = velocity.Length();

	if (speed > scMaxSpeed) {
		velocity = velocity * (scMaxSpeed / speed);
	}

	bodies.ActivateBody(mBody);
	bodies.SetLinearAndAngularVelocity(mBody, JPH::Vec3(velocity.X, velocity.Y, velocity.Z),
									   bodies.GetAngularVelocity(mBody) * scAngularDamping);

	renderer::gDebugDraw->Line(held_point, target, scGrabColor);
}

void GrabEditor::Finalize() { Release(); }

void GrabEditor::Cancel() { Release(); }

void GrabEditor::Release()
{
	mbHolding = false;
	mBody = JPH::BodyID();
}

} // namespace fx::editor

#endif
