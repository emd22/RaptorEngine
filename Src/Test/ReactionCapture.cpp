#include "ReactionCapture.hpp"

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <NPC.hpp>
#include <Object/Object.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>
#include <format>
#include <string>

FX_SET_MODULE_NAME("ReactionCapture");

namespace fx {

namespace {

constexpr float32 scCameraDistance = 3.0f;
constexpr float32 scCaptureLength = 42.0f;

enum class eCaptureHit : uint8
{
	Chest,
	Shin,
	Shoulder,
	Knockdown,
	Respawn,
};

struct CaptureEvent
{
	float32 Time;
	eCaptureHit Hit;
	float32 Damage;
};

// Each with long enough after it to come to rest. Damage is the debug pistol's, and the rifle's for the burst
constexpr CaptureEvent scEvents[] = {
	{ 2.0f, eCaptureHit::Chest, 34.0f },	 { 8.0f, eCaptureHit::Shin, 34.0f },
	{ 13.0f, eCaptureHit::Shoulder, 34.0f }, { 18.0f, eCaptureHit::Chest, 24.0f },
	{ 18.12f, eCaptureHit::Chest, 24.0f },	 { 18.24f, eCaptureHit::Chest, 24.0f },
	{ 26.0f, eCaptureHit::Respawn, 0.0f },	 { 28.0f, eCaptureHit::Knockdown, 0.0f },
};

} // namespace

bool ReactionCapture::Begin(const ReactionCaptureOptions& options)
{
	mOptions = options;

	const std::string path = std::format("{}/frames.csv", options.OutDir);
	mpOut = std::fopen(path.c_str(), "w");

	if (mpOut == nullptr) {
		LogError(LC_CORE, "Could not open '{}' for the reaction capture", path);
		return false;
	}

	std::fprintf(mpOut, "frame,time,dt,steps,alpha,state,powered,balancing,blend,body,dx,dy,dz,px,py,pz\n");
	gNPCManager->RequestModel();

	return true;
}

void ReactionCapture::SpawnTarget()
{
	FVec3 forward = gWorld->Player.pCamera->GetForwardVector();
	forward.Y = 0.0f;
	forward = forward.Normalize();

	mEye = gWorld->Player.pCamera->GetPosition();

	const physics::RayResult floor = gPhysics->pBackend->RaycastStatic(mEye + forward * scCameraDistance,
																	   FVec3(0.0f, -10.0f, 0.0f));
	mTargetPosition = mEye + forward * scCameraDistance;

	if (floor.bHit) {
		mTargetPosition = floor.Point;
	}

	NPCDesc desc;
	desc.Name = String::Fmt("capture_{}", mNextEvent);
	desc.Position = mTargetPosition;
	desc.Yaw = std::atan2(-forward.X, -forward.Z);
	desc.IdleAnimation = "walk";
	desc.WalkAnimation = "walk";

	mpTarget = gNPCManager->Spawn(gNPCManager->GetModel(), desc);
}

void ReactionCapture::PlaceCamera() { gWorld->Player.LookAt(mTargetPosition + FVec3(0.0f, 1.0f, 0.0f)); }

void ReactionCapture::FireEvents()
{
	while (mNextEvent < sizeof(scEvents) / sizeof(scEvents[0]) && mTime >= scEvents[mNextEvent].Time) {
		const CaptureEvent& event = scEvents[mNextEvent];
		mNextEvent++;

		if (event.Hit == eCaptureHit::Respawn) {
			gNPCManager->Destroy(mpTarget);
			SpawnTarget();
			continue;
		}

		physics::Ragdoll& ragdoll = mpTarget->GetRagdoll();
		const physics::RagdollRig& rig = ragdoll.GetRig();

		uint32 body = rig.Chest;

		if (event.Hit == eCaptureHit::Shin) {
			body = rig.Legs[0].Shin;
		}
		else if (event.Hit == eCaptureHit::Shoulder) {
			body = rig.Arms[0].Upper;
		}

		const FVec3 center = ragdoll.GetBodyPosition(body);
		const FVec3 direction = (center - mEye).Normalize();

		NPCHit hit { .Body = ragdoll.GetBodyID(body),
					 .Point = center - direction * 0.08f,
					 .Impulse = direction * 5.0f,
					 .Damage = event.Damage };

		if (event.Hit == eCaptureHit::Knockdown) {
			hit.Impulse = direction * (ragdoll.GetTotalMass() * 2.5f);
			hit.bIsCollision = true;
		}

		mpTarget->OnHit(hit);
	}
}

void ReactionCapture::PreStep(float32 delta_time)
{
	(void)delta_time;

	if (!mbStarted) {
		if (gNPCManager->GetModel() == nullptr) {
			return;
		}

		SpawnTarget();
		mbStarted = true;
	}

	PlaceCamera();
	FireEvents();
}

void ReactionCapture::WriteFrame(float32 delta_time)
{
	physics::Ragdoll& ragdoll = mpTarget->GetRagdoll();
	Object* skinned = mpTarget->GetActor().GetSkinned();
	const Skeleton& skeleton = *mpTarget->GetSkeleton();

	if (skinned == nullptr) {
		return;
	}

	const Mat4f world = skinned->GetWorldMatrix();
	const uint32 steps = gPhysics->pBackend->GetLastStepCount();
	const float32 alpha = gPhysics->pBackend->GetStepAlpha();
	const bool is_powered = ragdoll.GetBodyMode(0) != physics::eRagdollBodyMode::Kinematic;

	for (uint32 i = 0; i < ragdoll.GetSegmentCount(); i++) {
		const Mat4f drawn_matrix = skeleton.WorldTransforms[ragdoll.GetBodyBone(i)] * world;
		const Vec4f drawn = drawn_matrix.Rows[3];
		const FVec3 origin = ragdoll.GetBodyOrigin(i);

		std::fprintf(mpOut, "%u,%.5f,%.5f,%u,%.4f,%d,%d,%d,%.3f,%u,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n", mFrame, mTime,
					 delta_time, steps, alpha, static_cast<int>(mpTarget->GetState()), static_cast<int>(is_powered),
					 static_cast<int>(ragdoll.IsBalancing()), ragdoll.GetPoseBlend(), i, drawn.X, drawn.Y, drawn.Z,
					 origin.X, origin.Y, origin.Z);
	}
}

void ReactionCapture::PostFrame(float32 delta_time)
{
	if (!mbStarted) {
		return;
	}

	if (mpTarget != nullptr && mpTarget->IsCreated()) {
		WriteFrame(delta_time);
	}

	if (mOptions.ImageStride > 0 && mFrame % mOptions.ImageStride == 0 && mTime >= mOptions.ImageFrom &&
		mTime <= mOptions.ImageTo) {
		const std::string path = std::format("{}/f{:06}.bgra", mOptions.OutDir, mFrame);
		renderer::gGraphics->RequestFrameCapture(path.c_str());
	}

	mTime += delta_time;
	mFrame++;

	if (mTime >= scCaptureLength) {
		std::fclose(mpOut);
		mpOut = nullptr;
		mbFinished = true;
	}
}

} // namespace fx
