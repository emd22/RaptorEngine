#include "GrabEditor.hpp"

#ifdef FX_IS_EDITOR

#include "RaptorEditor.hpp"

#include <ThirdParty/Jolt/Physics/Body/BodyInterface.h>

#include <Color.hpp>
#include <Controls.hpp>
#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>

namespace fx::editor {

static constexpr float32 scGrabRange = 6.0f;
static constexpr float32 scMinHoldDistance = 1.0f;

static constexpr float32 scStiffness = 20.0f;
static constexpr float32 scMaxSpeed = 20.0f;
static constexpr float32 scMaxAcceleration = 60.0f;
static constexpr float32 scAngularDamping = 8.0f;
static constexpr float32 scLaunchSpeed = 25.0f;


void GrabEditor::Update(float32 delta_time)
{
	const bool key_down = ControlManager::IsKeyDown(eKey::FX_KEY_Y);

	if (!mbHolding) {
		if (key_down && ControlManager::IsKeyPressed(eKey::FX_KEY_Y)) {
			Grab();
		}

		return;
	}

	if (!key_down) {
		Release();
		return;
	}

	if (ControlManager::IsKeyPressed(eKey::FX_MOUSE_LEFT)) {
		Launch();
		return;
	}

	Hold(delta_time);
}

void GrabEditor::Grab()
{
	Release();

	PerspectiveCamera2& camera = *gWorld->Player.pCamera;

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera.GetPosition(),
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
	mDistance = std::max((hit.Point - camera.GetPosition()).Length(), scMinHoldDistance);

	mbHolding = true;
}

void GrabEditor::Launch()
{
	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	if (bodies.IsAdded(mBody) && bodies.GetMotionType(mBody) == JPH::EMotionType::Dynamic) {
		const Vec3f launch = gWorld->Player.pCamera->GetForwardVector() * scLaunchSpeed;

		bodies.ActivateBody(mBody);
		bodies.SetLinearVelocity(mBody, JPH::Vec3(launch.X, launch.Y, launch.Z));
	}

	Release();
}

void GrabEditor::Hold(float32 delta_time)
{
	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	if (!bodies.IsAdded(mBody) || bodies.GetMotionType(mBody) != JPH::EMotionType::Dynamic) {
		Release();
		return;
	}

	PerspectiveCamera2& camera = *gWorld->Player.pCamera;

	const Vec3f target = camera.GetPosition() + camera.GetForwardVector() * mDistance;
	const Vec3f held_point = Vec3f(bodies.GetCenterOfMassTransform(mBody) *
								   JPH::Vec3(mLocalPoint.X, mLocalPoint.Y, mLocalPoint.Z));

	Vec3f velocity = (target - held_point) * scStiffness;
	const float32 speed = velocity.Length();

	if (speed > scMaxSpeed) {
		velocity = velocity * (scMaxSpeed / speed);
	}

	const Vec3f current = Vec3f(bodies.GetLinearVelocity(mBody));
	Vec3f change = velocity - current;
	const float32 max_change = scMaxAcceleration * delta_time;
	const float32 change_length = change.Length();

	if (change_length > max_change) {
		change = change * (max_change / change_length);
	}

	velocity = current + change;

	bodies.ActivateBody(mBody);
	bodies.SetLinearAndAngularVelocity(mBody, JPH::Vec3(velocity.X, velocity.Y, velocity.Z),
									   bodies.GetAngularVelocity(mBody) * std::exp(-scAngularDamping * delta_time));

	renderer::gDebugDraw->Line(held_point, target, gEditor->GetTheme().Viewport.Grab);
}

void GrabEditor::Release()
{
	mbHolding = false;
	mBody = JPH::BodyID();
}

} // namespace fx::editor

#endif
