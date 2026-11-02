#include "RagdollBench.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>

#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <NPC.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>

FX_SET_MODULE_NAME("RagdollBench");

namespace fx {

namespace {

constexpr float32 scFloorHeight = -200.0f;
/// The same friction as the 0.2 floors in the levels
constexpr float32 scFloorFriction = 0.2f;
constexpr float32 scTrialSpacing = 10.0f;
constexpr uint32 scGridWidth = 4;

/// How far ahead of the NPC the terrain's edge is (m)
constexpr float32 scTerrainDistance = 0.3f;
constexpr float32 scTerrainDepth = 4.0f;
constexpr float32 scTerrainHalfWidth = 2.0f;
/// How far boxes reach into the floor, to stay thicker than their convex radius however low they are (m)
constexpr float32 scTerrainFooting = 0.5f;
constexpr uint32 scStairCount = 5;
constexpr float32 scStairRise = 0.17f;
constexpr float32 scStairTread = 0.28f;
constexpr float32 scRampThickness = 0.5f;
constexpr float32 scRampUpLength = 3.0f;
constexpr float32 scRampDownHeight = 0.6f;
constexpr float32 scTerrainPushSpeeds[] = { 1.0f, 1.5f, 2.0f, 2.5f };
constexpr uint32 scTerrainPhases = 4;

/// What a shove asks for. Root assist brakes the body for the frame before balance starts, about 0.5 m/s, so the report
/// goes by the speed it measures
constexpr float32 scPushSpeeds[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f };
constexpr uint32 scDirectionCount = 8;
constexpr uint32 scWalkPhases = 8;
/// Frames in one loop of ragdoll2's 0.97 s walk clip
constexpr uint32 scWalkFrames = 58;

constexpr int32 scSettleFrames = 60;
constexpr int32 scObserveFrames = 480;
constexpr int32 scPerfActiveFrames = 180;
constexpr uint32 scTraceStride = 2;
constexpr uint32 scTraceValues = 6;

/// How long a foot must be off the ground (frames) before it counts as lifted, so a brief loss of contact isn't a step
constexpr int32 scMinAirFrames = 3;
constexpr float32 scMinSupportUp = 0.5f;
constexpr float32 scLoadedShare = 1.0f / 3.0f;
/// Frames after the hit before spin counts as the body's own doing rather than the shove's
constexpr int32 scLateSpinFrame = 15;

constexpr float32 scRadToDeg = 180.0f / FX_PI;

float32 HorizontalLength(const Vec3f v) { return std::sqrt(v.X * v.X + v.Z * v.Z); }

float32 GetKneeBend(physics::Ragdoll& ragdoll, const physics::RagdollRigLeg& leg)
{
	const Vec3f hip = ragdoll.GetBodyOrigin(leg.Thigh);
	const Vec3f knee = ragdoll.GetBodyOrigin(leg.Shin);
	const Vec3f ankle = ragdoll.GetBodyOrigin(leg.Foot);

	const Vec3f upper = (knee - hip).SafeNormalize(Vec3f::sUp);
	const Vec3f lower = (ankle - knee).SafeNormalize(Vec3f::sUp);

	return std::acos(MathUtil::Clamp(upper.Dot(lower), -1.0f, 1.0f)) * scRadToDeg;
}

/// Where the centre of mass sits along the line between the feet, 1 being right over this one
float32 GetWeightShare(physics::Ragdoll& ragdoll, const physics::RagdollRigLeg& leg,
					   const physics::RagdollRigLeg& other)
{
	Vec3f center = Vec3f::sZero;

	for (uint32 i = 0; i < ragdoll.GetSegmentCount(); i++) {
		center += ragdoll.GetBodyPosition(i) * ragdoll.GetBodyMass(i);
	}

	center = center / ragdoll.GetTotalMass();

	const Vec3f own = ragdoll.GetBodyPosition(leg.Foot);
	const Vec3f span = own - ragdoll.GetBodyPosition(other.Foot);
	const float32 length2 = span.X * span.X + span.Z * span.Z;

	if (length2 < 1e-6f) {
		return 0.5f;
	}

	const Vec3f from_other = center - ragdoll.GetBodyPosition(other.Foot);

	return MathUtil::Clamp((from_other.X * span.X + from_other.Z * span.Z) / length2, 0.0f, 1.0f);
}

Vec3f GetDirection(float32 degrees)
{
	const float32 radians = degrees / scRadToDeg;
	return Vec3f(std::sin(radians), 0.0f, std::cos(radians));
}

uint64 HashBytes(uint64 hash, const void* data, size_t size)
{
	const uint8* bytes = static_cast<const uint8*>(data);

	for (size_t i = 0; i < size; i++) {
		hash = (hash ^ bytes[i]) * 1099511628211ULL;
	}

	return hash;
}

} // namespace

bool RagdollBench::Begin(const RagdollBenchOptions& options)
{
	mOptions = options;

	if (mOptions.OutPath == nullptr) {
		LogError(LC_CORE, "The ragdoll bench needs an output path (-bench-out)");
		return false;
	}

	mpOut = std::fopen(mOptions.OutPath, "w");

	if (mpOut == nullptr) {
		LogError(LC_CORE, "Could not open '{}' for the ragdoll bench", mOptions.OutPath);
		return false;
	}

	BuildTrials();

	if (mTrials.Size == 0) {
		LogError(LC_CORE, "No ragdoll bench trials in suite '{}'", mOptions.Suite);
		return false;
	}

	std::fprintf(mpOut, "{\"meta\":{\"suite\":\"%s\",\"shard\":%u,\"shards\":%u,\"trials\":%u,\"batch\":%u}}\n",
				 mOptions.Suite, mOptions.ShardIndex, mOptions.ShardCount, static_cast<uint32>(mTrials.Size),
				 mBatchSize);

	gNPCManager->RequestModel();

	return true;
}

void RagdollBench::AddTrial(const Trial& trial)
{
	Trial copy = trial;
	mTrials.Insert(std::move(copy));
}

void RagdollBench::AddPushTrials()
{
	for (const float32 speed : scPushSpeeds) {
		for (uint32 direction = 0; direction < scDirectionCount; direction++) {
			for (uint32 phase = 0; phase < scWalkPhases; phase++) {
				Trial trial;
				trial.Speed = speed;
				trial.Direction = static_cast<float32>(direction) * 45.0f;
				trial.Phase = phase;
				trial.bTrace = (phase == 0);
				std::snprintf(trial.Condition, sizeof(trial.Condition), "%.2f", speed);
				std::snprintf(trial.Name, sizeof(trial.Name), "push_v%.2f_d%03u_p%u", speed, direction * 45, phase);
				AddTrial(trial);
			}
		}
	}
}

void RagdollBench::AddChestTrials()
{
	for (const float32 speed : scPushSpeeds) {
		if (speed <= 0.0f) {
			continue;
		}

		for (uint32 direction = 0; direction < scDirectionCount; direction += 2) {
			for (uint32 phase = 0; phase < scWalkPhases; phase++) {
				Trial trial;
				trial.Speed = speed;
				trial.Direction = static_cast<float32>(direction) * 45.0f;
				trial.Phase = phase;
				trial.bLocalHit = true;
				trial.bTrace = (phase == 0);
				std::snprintf(trial.Condition, sizeof(trial.Condition), "%.2f", speed);
				std::snprintf(trial.Name, sizeof(trial.Name), "chest_v%.2f_d%03u_p%u", speed, direction * 45, phase);
				AddTrial(trial);
			}
		}
	}
}

void RagdollBench::AddStillTrials()
{
	// Standing in the bind pose takes the walk clip's split stance out of it, which separates balance from handoff
	for (const float32 speed : scPushSpeeds) {
		for (uint32 direction = 0; direction < scDirectionCount; direction++) {
			Trial trial;
			trial.Speed = speed;
			trial.Direction = static_cast<float32>(direction) * 45.0f;
			trial.bStill = true;
			trial.bTrace = true;
			std::snprintf(trial.Condition, sizeof(trial.Condition), "%.2f", speed);
			std::snprintf(trial.Name, sizeof(trial.Name), "still_v%.2f_d%03u", speed, direction * 45);
			AddTrial(trial);
		}
	}
}

void RagdollBench::AddWeaponTrials()
{
	struct WeaponCase
	{
		const char* Name;
		float32 Impulse;
		uint32 Shots;
		uint32 Interval;
	};

	static constexpr WeaponCase scCases[] = {
		{ "pistol", 5.0f, 1, 0 },
		{ "rifle2", 6.0f, 2, 12 },
		{ "auto8", 6.0f, 8, 6 },
	};

	for (const WeaponCase& weapon : scCases) {
		for (uint32 direction = 0; direction < scDirectionCount; direction++) {
			for (uint32 phase = 0; phase < scWalkPhases; phase++) {
				Trial trial;
				trial.ShotImpulse = weapon.Impulse;
				trial.ShotCount = weapon.Shots;
				trial.ShotInterval = weapon.Interval;
				trial.Direction = static_cast<float32>(direction) * 45.0f;
				trial.Phase = phase;
				trial.bTrace = (phase == 0);
				std::snprintf(trial.Condition, sizeof(trial.Condition), "%s", weapon.Name);
				std::snprintf(trial.Name, sizeof(trial.Name), "%s_d%03u_p%u", weapon.Name, direction * 45, phase);
				AddTrial(trial);
			}
		}
	}
}

void RagdollBench::AddReactionTrials()
{
	struct HitCase
	{
		const char* Name;
		const char* Part;
		float32 Offset;
		float32 Damage;
	};

	// Damage as the debug pistol's on each part, without killing except where the case is about that
	static constexpr HitCase scCases[] = {
		{ "chest", "chest", 0.0f, 34.0f },		   { "chestR", "chest", 0.12f, 34.0f },
		{ "chestL", "chest", -0.12f, 34.0f },	   { "pelvis", "pelvis", 0.0f, 34.0f },
		{ "head", "head", 0.0f, 1.0f },			   { "headkill", "head", 0.0f, 34.0f },
		{ "kill", "chest", 0.0f, 200.0f },		   { "uparm", "upperarm", 0.0f, 34.0f },
		{ "forearm", "forearm", 0.0f, 34.0f },	   { "thigh", "thigh", 0.0f, 34.0f },
		{ "shin", "shin", 0.0f, 34.0f },
	};

	static constexpr uint32 scReactionPhases = 4;

	for (const HitCase& hit_case : scCases) {
		for (uint32 direction = 0; direction < scDirectionCount; direction++) {
			for (uint32 phase = 0; phase < scReactionPhases; phase++) {
				Trial trial;
				trial.ShotImpulse = 5.0f;
				trial.ShotCount = 1;
				trial.Direction = static_cast<float32>(direction) * 45.0f;
				trial.Phase = phase * scWalkPhases / scReactionPhases;
				trial.HitPart = hit_case.Part;
				trial.HitOffset = hit_case.Offset;
				trial.Damage = hit_case.Damage;
				trial.bTrace = (phase == 0);
				std::snprintf(trial.Condition, sizeof(trial.Condition), "%s", hit_case.Name);
				std::snprintf(trial.Name, sizeof(trial.Name), "hit_%s_d%03u_p%u", hit_case.Name, direction * 45, phase);
				AddTrial(trial);
			}
		}
	}
}

void RagdollBench::AddTerrainTrials()
{
	struct TerrainKind
	{
		const char* Name;
		eTerrain Terrain;
		float32 Height;
	};

	constexpr TerrainKind kinds[] = {
		{ "flat", eTerrain::Flat, 0.0f },
		{ "kerb10", eTerrain::Kerb, 0.1f },
		{ "kerb20", eTerrain::Kerb, 0.2f },
		{ "drop10", eTerrain::Drop, 0.1f },
		{ "drop20", eTerrain::Drop, 0.2f },
		{ "stairs_up", eTerrain::StairsUp, 0.0f },
		{ "stairs_down", eTerrain::StairsDown, 0.0f },
		{ "ramp_up10", eTerrain::RampUp, 10.0f },
		{ "ramp_up20", eTerrain::RampUp, 20.0f },
		{ "ramp_down10", eTerrain::RampDown, 10.0f },
		{ "ramp_down20", eTerrain::RampDown, 20.0f },
	};

	for (const TerrainKind& kind : kinds) {
		for (const float32 speed : scTerrainPushSpeeds) {
			for (uint32 direction = 0; direction < scDirectionCount; direction++) {
				for (uint32 phase = 0; phase < scTerrainPhases; phase++) {
					Trial trial;
					trial.Speed = speed;
					trial.Direction = static_cast<float32>(direction) * 45.0f;
					trial.Phase = phase * (scWalkPhases / scTerrainPhases);
					trial.bTrace = (phase == 0);
					trial.Terrain = kind.Terrain;
					trial.TerrainHeight = kind.Height;
					std::snprintf(trial.Condition, sizeof(trial.Condition), "%s", kind.Name);
					std::snprintf(trial.Name, sizeof(trial.Name), "terrain_%s_v%.2f_d%03u_p%u", kind.Name, speed,
								  direction * 45, trial.Phase);
					AddTrial(trial);
				}
			}
		}
	}
}

void RagdollBench::AddPerfTrials()
{
	mBatchSize = 8;

	for (uint32 direction = 0; direction < mBatchSize; direction++) {
		Trial trial;
		trial.Speed = 0.5f;
		trial.Direction = static_cast<float32>(direction) * 45.0f;
		std::snprintf(trial.Condition, sizeof(trial.Condition), "perf");
		std::snprintf(trial.Name, sizeof(trial.Name), "perf_d%03u", direction * 45);
		AddTrial(trial);
	}
}

void RagdollBench::BuildTrials()
{
	const auto is_suite = [this](const char* name)
	{ return std::strcmp(mOptions.Suite, name) == 0 || std::strcmp(mOptions.Suite, "all") == 0; };

	mTrials.InitCapacity(4096);
	mTerrainBodies.InitCapacity(mBatchSize * scStairCount);

	if (is_suite("push")) {
		AddPushTrials();
	}

	if (is_suite("chest")) {
		AddChestTrials();
	}

	if (is_suite("still")) {
		AddStillTrials();
	}

	if (is_suite("weapon")) {
		AddWeaponTrials();
	}

	if (is_suite("reaction")) {
		AddReactionTrials();
	}

	// Older baselines have nothing to compare it with, so it is not part of "all"
	if (std::strcmp(mOptions.Suite, "terrain") == 0) {
		AddTerrainTrials();
	}

	// Timings mean nothing next to other runs, so it is never part of "all"
	if (std::strcmp(mOptions.Suite, "perf") == 0) {
		AddPerfTrials();
	}
}

void RagdollBench::CreateFloor()
{
	JPH::BodyCreationSettings settings(new JPH::BoxShape(JPH::Vec3(100.0f, 0.5f, 100.0f)),
									   JPH::RVec3(0.0f, scFloorHeight - 0.5f, 0.0f), JPH::Quat::sIdentity(),
									   JPH::EMotionType::Static, physics::PhLayer::Static);
	settings.mFriction = scFloorFriction;
	gPhysics->pBackend->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
}

void RagdollBench::AddTerrainBox(const Vec3f position, const Vec3f direction, float32 near_distance,
								 float32 far_distance, float32 top)
{
	const float32 half_depth = (far_distance - near_distance) * 0.5f;
	const float32 half_height = (top + scTerrainFooting) * 0.5f;
	const Vec3f center = position + direction * (near_distance + half_depth);

	JPH::BodyCreationSettings settings(new JPH::BoxShape(JPH::Vec3(scTerrainHalfWidth, half_height, half_depth)),
									   JPH::RVec3(center.X, scFloorHeight + top - half_height, center.Z),
									   JPH::Quat::sRotation(JPH::Vec3::sAxisY(), std::atan2(direction.X, direction.Z)),
									   JPH::EMotionType::Static, physics::PhLayer::Static);
	settings.mFriction = scFloorFriction;

	JPH::BodyID body = gPhysics->pBackend->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
	mTerrainBodies.Insert(std::move(body));
}

void RagdollBench::AddTerrainRamp(const Vec3f position, const Vec3f direction, float32 near_distance,
								  float32 near_height, float32 length, float32 pitch)
{
	const JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), std::atan2(direction.X, direction.Z)) *
							   JPH::Quat::sRotation(JPH::Vec3::sAxisX(), pitch);
	const JPH::Vec3 top_near = Vec3f::ToJolt(position + direction * near_distance + Vec3f(0.0f, near_height, 0.0f));
	const JPH::Vec3 center = top_near + rotation * JPH::Vec3(0.0f, -scRampThickness * 0.5f, length * 0.5f);

	const JPH::Vec3 half_extent(scTerrainHalfWidth, scRampThickness * 0.5f, length * 0.5f);

	JPH::BodyCreationSettings settings(new JPH::BoxShape(half_extent), JPH::RVec3(center), rotation,
									   JPH::EMotionType::Static, physics::PhLayer::Static);
	settings.mFriction = scFloorFriction;

	JPH::BodyID body = gPhysics->pBackend->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
	mTerrainBodies.Insert(std::move(body));
}

float32 RagdollBench::CreateTerrain(const Trial& trial, const Vec3f position)
{
	const Vec3f direction = GetDirection(trial.Direction);
	const float32 edge = scTerrainDistance;

	switch (trial.Terrain) {
		case eTerrain::Flat:
			return 0.0f;

		case eTerrain::Kerb:
			AddTerrainBox(position, direction, edge, edge + scTerrainDepth, trial.TerrainHeight);
			return 0.0f;

		case eTerrain::Drop:
			AddTerrainBox(position, direction, edge - scTerrainDepth, edge, trial.TerrainHeight);
			return trial.TerrainHeight;

		case eTerrain::StairsUp:
			for (uint32 i = 0; i < scStairCount; i++) {
				const float32 near_distance = edge + static_cast<float32>(i) * scStairTread;

				AddTerrainBox(position, direction, near_distance, edge + scTerrainDepth,
							  static_cast<float32>(i + 1) * scStairRise);
			}

			return 0.0f;

		case eTerrain::StairsDown: {
			const float32 height = static_cast<float32>(scStairCount) * scStairRise;

			AddTerrainBox(position, direction, edge - scTerrainDepth, edge, height);

			for (uint32 i = 1; i < scStairCount; i++) {
				const float32 near_distance = edge + static_cast<float32>(i - 1) * scStairTread;

				AddTerrainBox(position, direction, near_distance, near_distance + scStairTread,
							  height - static_cast<float32>(i) * scStairRise);
			}

			return height;
		}

		case eTerrain::RampUp:
			AddTerrainRamp(position, direction, edge, 0.0f, scRampUpLength, -trial.TerrainHeight / scRadToDeg);
			return 0.0f;

		case eTerrain::RampDown: {
			const float32 pitch = trial.TerrainHeight / scRadToDeg;

			AddTerrainBox(position, direction, edge - scTerrainDepth, edge, scRampDownHeight);
			AddTerrainRamp(position, direction, edge, scRampDownHeight, scRampDownHeight / std::sin(pitch), pitch);
			return scRampDownHeight;
		}
	}

	return 0.0f;
}

void RagdollBench::RemoveTerrain()
{
	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	for (const JPH::BodyID body : mTerrainBodies) {
		bodies.RemoveBody(body);
		bodies.DestroyBody(body);
	}

	mTerrainBodies.Clear();
}

bool RagdollBench::StartNextBatch()
{
	const uint32 batch_count = (static_cast<uint32>(mTrials.Size) + mBatchSize - 1) / mBatchSize;

	// Which batch a trial is in never depends on the shard count, so splitting a run up doesn't change who it shares
	// the physics world with
	while (mNextBatch < batch_count && mNextBatch % mOptions.ShardCount != mOptions.ShardIndex) {
		mNextBatch++;
	}

	if (mNextBatch >= batch_count) {
		return false;
	}

	StartBatch(mNextBatch);
	mNextBatch++;

	return true;
}

void RagdollBench::StartBatch(uint32 batch_index)
{
	Object* model = gNPCManager->GetModel();

	SeedFastRand(batch_index + 1);

	const uint32 first = batch_index * mBatchSize;
	const uint32 count = std::min(mBatchSize, static_cast<uint32>(mTrials.Size) - first);

	mRuns.Free();
	mRuns.InitSize(count);

	mFrame = 0;
	mEndFrame = 0;
	mFirstHitFrame = INT32_MAX;

	for (uint32 i = 0; i < count; i++) {
		const Trial& trial = mTrials[first + i];
		TrialRun& run = mRuns[i];

		NPCDesc desc;
		desc.Name = String(trial.Name);
		desc.Position = Vec3f((static_cast<float32>(i % scGridWidth) - 1.5f) * scTrialSpacing, scFloorHeight,
							  (static_cast<float32>(i / scGridWidth) - 1.5f) * scTrialSpacing);
		desc.Position.Y += CreateTerrain(trial, desc.Position);

		if (!trial.bStill) {
			desc.IdleAnimation = "walk";
			desc.WalkAnimation = "walk";
		}

		run.pTrial = &trial;
		run.pNPC = gNPCManager->Spawn(model, desc);
		run.HitFrame = scSettleFrames + static_cast<int32>(trial.Phase * scWalkFrames / scWalkPhases);

		mFirstHitFrame = std::min(mFirstHitFrame, run.HitFrame);
		mEndFrame = std::max(mEndFrame, run.HitFrame + scObserveFrames);

		if (run.pNPC == nullptr) {
			LogError(LC_CORE, "Could not spawn ragdoll bench trial '{}'", trial.Name);
		}
	}
}

void RagdollBench::FireShots(TrialRun& run)
{
	const Trial& trial = *run.pTrial;

	if (run.pNPC == nullptr || run.ShotsFired >= trial.ShotCount) {
		return;
	}

	const int32 shot_frame = run.HitFrame + static_cast<int32>(run.ShotsFired * trial.ShotInterval);

	if (mFrame != shot_frame) {
		return;
	}

	run.ShotsFired++;

	physics::Ragdoll& ragdoll = run.pNPC->GetRagdoll();
	const uint32 chest = ragdoll.GetRig().Chest;
	const Vec3f direction = GetDirection(trial.Direction);

	NPCHit hit { .Body = ragdoll.GetBodyID(chest), .Point = ragdoll.GetBodyPosition(chest) };

	if (trial.HitPart != nullptr) {
		const uint32 body = static_cast<uint32>(FindHitBody(run));
		const Vec3f right = Vec3f::sUp.Cross(direction);

		hit.Body = ragdoll.GetBodyID(body);
		hit.Point = ragdoll.GetBodyPosition(body) - direction * 0.08f + right * trial.HitOffset;
		hit.Damage = trial.Damage;

		Quality& look = run.Look;
		const Vec3f lever = hit.Point - ragdoll.GetBodyPosition(0);

		look.ExpectedYaw = lever.Z * direction.X - lever.X * direction.Z;
		look.LeverHeight = hit.Point.Y - ragdoll.GetBodyOrigin(ragdoll.GetRig().Legs[0].Foot).Y;
	}

	// A shove goes through the collision path, where the speed it gives the body is the momentum over its mass. A shot
	// goes through the weapon path
	if (trial.ShotImpulse > 0.0f) {
		hit.Impulse = direction * trial.ShotImpulse;
	}
	else {
		hit.Impulse = direction * (trial.Speed * ragdoll.GetTotalMass());
		hit.bIsCollision = true;
		hit.bIsWholeBody = !trial.bLocalHit;
	}

	run.pNPC->OnHit(hit);
}

int32 RagdollBench::FindHitBody(const TrialRun& run) const
{
	const physics::RagdollRig& rig = run.pNPC->GetRagdoll().GetRig();
	const char* part = run.pTrial->HitPart;

	if (part == nullptr || std::strcmp(part, "chest") == 0) {
		return static_cast<int32>(rig.Chest);
	}

	if (std::strcmp(part, "pelvis") == 0) {
		return 0;
	}

	if (std::strcmp(part, "head") == 0) {
		return static_cast<int32>(rig.Neck);
	}

	if (std::strcmp(part, "upperarm") == 0) {
		return static_cast<int32>(rig.Arms[0].Upper);
	}

	if (std::strcmp(part, "forearm") == 0) {
		return static_cast<int32>(rig.Arms[0].Lower);
	}

	if (std::strcmp(part, "thigh") == 0) {
		return static_cast<int32>(rig.Legs[0].Thigh);
	}

	return static_cast<int32>(rig.Legs[0].Shin);
}

void RagdollBench::SampleQuality(TrialRun& run, int32 since_hit)
{
	constexpr float32 scTwitchSpeed = 300.0f;
	constexpr float32 scLimitSlack = 3.0f;
	constexpr int32 scHitSettleFrames = 6;

	physics::Ragdoll& ragdoll = run.pNPC->GetRagdoll();
	const physics::RagdollRig& rig = ragdoll.GetRig();
	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();
	const uint32 segments = ragdoll.GetSegmentCount();
	Quality& look = run.Look;

	if (look.Spin.Size == 0) {
		look.Spin.InitSize(segments, JPH::Vec3::sZero());
		look.PreviousSpin.InitSize(segments, JPH::Vec3::sZero());
		look.BodyJitter.InitSize(segments, 0.0f);
		look.BodyLimit.InitSize(segments, 0.0f);
	}

	Vec3f center = Vec3f::sZero;

	for (uint32 i = 0; i < segments; i++) {
		center += ragdoll.GetBodyPosition(i) * ragdoll.GetBodyMass(i);
	}

	center = center / ragdoll.GetTotalMass();

	const Vec3f direction = GetDirection(run.pTrial->Direction);
	const int32 hit_body = FindHitBody(run);
	const Vec3f chest_lean = ragdoll.GetBodyPosition(rig.Chest) - ragdoll.GetBodyPosition(0);

	const Vec3f forward = run.pNPC->GetBehaviours().GetContext().Forward;
	const float32 heading = std::atan2(forward.X, forward.Z);

	if (since_hit == 0) {
		look.HitBodyStart = ragdoll.GetBodyPosition(hit_body);
		look.CenterStart = center;
		look.ChestLeanStart = chest_lean;
		look.YawStart = heading;
	}

	if (since_hit == 3) {
		look.HitBodyMove = (ragdoll.GetBodyPosition(hit_body) - look.HitBodyStart).Dot(direction);
	}

	if (since_hit == 15) {
		look.CenterMove[0] = (center - look.CenterStart).Dot(direction);
		look.ChestLean = (chest_lean - look.ChestLeanStart).Dot(direction);
	}

	if (since_hit == 10) {
		float32 turn = heading - look.YawStart;

		while (turn > FX_PI) {
			turn -= FX_2PI;
		}

		while (turn < -FX_PI) {
			turn += FX_2PI;
		}

		look.YawResponse = turn * scRadToDeg;
	}

	if (since_hit == 30) {
		look.CenterMove[1] = (center - look.CenterStart).Dot(direction);
	}
	else if (since_hit == 60) {
		look.CenterMove[2] = (center - look.CenterStart).Dot(direction);
	}

	// Back on its animation the pose is the clip's, whatever the clip does, so only physics-driven frames count
	const bool is_physical = ragdoll.GetBodyMode(0) != physics::eRagdollBodyMode::Kinematic;
	const bool in_shot = !is_physical || since_hit < scHitSettleFrames +
														 static_cast<int32>(run.pTrial->ShotCount - 1) *
															 static_cast<int32>(run.pTrial->ShotInterval);
	bool any_twitch = false;

	for (uint32 i = 1; i < segments; i++) {
		const JPH::Vec3 own = bodies.GetAngularVelocity(ragdoll.GetBodyID(i));
		const JPH::Vec3 parent = bodies.GetAngularVelocity(ragdoll.GetBodyID(ragdoll.GetParentBody(i)));
		const JPH::Vec3 spin = own - parent;

		const float32 jitter = (spin - look.Spin[i] * 2.0f + look.PreviousSpin[i]).Length() * scRadToDeg;

		look.PreviousSpin[i] = look.Spin[i];
		look.Spin[i] = spin;

		if (since_hit < 2 || in_shot) {
			continue;
		}

		uint32 group = 1;

		switch (ragdoll.GetBodyPart(i)) {
		case physics::eRDBodyPart::Head:
			group = 0;
			break;
		case physics::eRDBodyPart::Arm:
			group = 2;
			break;
		case physics::eRDBodyPart::Leg:
		case physics::eRDBodyPart::Foot:
			group = 3;
			break;
		default:
			break;
		}

		look.JitterSum[group] += jitter;
		look.BodyJitter[i] += jitter;
		any_twitch = any_twitch || (jitter > scTwitchSpeed);
	}

	if (!in_shot && since_hit >= 2) {
		look.JitterCount++;

		if (any_twitch) {
			look.TwitchFrames++;
		}
	}

	// How much the pelvis jumps from one frame to the next, beyond what a steady speed would do
	const Vec3f pelvis = ragdoll.GetBodyOrigin(0);

	if (since_hit >= 2 && !in_shot) {
		const Vec3f second = pelvis - look.PelvisHistory[0] * 2.0f + look.PelvisHistory[1];
		look.PeakRootJitter = std::max(look.PeakRootJitter, second.Length() * 1000.0f);
	}

	look.PelvisHistory[1] = look.PelvisHistory[0];
	look.PelvisHistory[0] = pelvis;

	if (!is_physical) {
		return;
	}

	{
		constexpr float32 scMinHeadSwing = 6.0f;

		const physics::BehaviourContext& ctx = run.pNPC->GetBehaviours().GetContext();
		const Vec3f head = (ragdoll.GetBodyPosition(rig.Neck) - ragdoll.GetBodyOrigin(rig.Neck)).SafeNormalize(ctx.Up);
		const float32 bend[2] = { head.Dot(ctx.Right) * scRadToDeg, head.Dot(ctx.Forward) * scRadToDeg };
		const uint32 stage = (run.FallFrame >= 0) ? 1 : 0;

		look.HeadFrames[stage]++;

		for (uint32 axis = 0; axis < 2; axis++) {
			if (look.HeadDirection[axis] == 0) {
				look.HeadExtreme[axis] = bend[axis];
				look.HeadDirection[axis] = 1;
			}
			else if ((bend[axis] - look.HeadExtreme[axis]) * static_cast<float32>(look.HeadDirection[axis]) > 0.0f) {
				look.HeadExtreme[axis] = bend[axis];
			}
			else if (std::abs(bend[axis] - look.HeadExtreme[axis]) >= scMinHeadSwing) {
				look.HeadSwings[stage]++;
				look.HeadExtreme[axis] = bend[axis];
				look.HeadDirection[axis] = -look.HeadDirection[axis];
			}
		}
	}

	float32 worst_excess = 0.0f;

	for (uint32 i = 1; i < segments; i++) {
		const float32 excess = ragdoll.GetJointLimitExcess(i);

		worst_excess = std::max(worst_excess, excess);
		look.BodyLimit[i] = std::max(look.BodyLimit[i], excess);
	}

	look.PeakLimitExcess = std::max(look.PeakLimitExcess, worst_excess);

	if (worst_excess > scLimitSlack) {
		look.LimitFrames++;
	}

	const float32 gap = ragdoll.GetWorstJointError();

	if (gap > look.PeakJointGap) {
		look.PeakJointGap = gap;
		look.WorstGapFrame = since_hit;
	}
}

void RagdollBench::FinishStance(TrialRun& run, FootTrack& foot)
{
	run.Stances++;
	run.SlideSum += foot.Slide;
	run.PeakSlide = std::max(run.PeakSlide, foot.Slide);
	run.PeakLoadedSlide = std::max(run.PeakLoadedSlide, foot.LoadedSlide);
	foot.Slide = 0.0f;
	foot.LoadedSlide = 0.0f;
	foot.bLoaded = false;
}

void RagdollBench::SampleFeet(TrialRun& run)
{
	physics::Ragdoll& ragdoll = run.pNPC->GetRagdoll();
	const physics::RagdollRig& rig = ragdoll.GetRig();
	const JPH::Vec3 up = JPH::Vec3::sAxisY();

	for (uint32 i = 0; i < 2; i++) {
		const physics::RagdollRigLeg& leg = rig.Legs[i];
		FootTrack& foot = run.Feet[i];

		const Vec3f position = ragdoll.GetBodyPosition(leg.Foot);
		const bool touching = ragdoll.IsBodyOnGround(leg.Foot, up, scMinSupportUp);
		const float32 knee = GetKneeBend(ragdoll, leg);

		if (!foot.bStarted) {
			foot.bStarted = true;
			foot.bPlanted = touching;
			foot.LastPosition = position;
			foot.StanceHeight = position.Y;
		}

		if (touching) {
			if (!foot.bPlanted) {
				const float32 step_time = static_cast<float32>(mFrame - foot.TakeoffFrame) * scDeltaTime;

				run.Steps++;
				run.StepLengthSum += HorizontalLength(position - foot.TakeoffPosition);
				run.StepTimeSum += step_time;

				if (run.MinStepTime == 0.0f || step_time < run.MinStepTime) {
					run.MinStepTime = step_time;
				}

				foot.bPlanted = true;
				foot.StanceHeight = position.Y;
			}
			else {
				foot.Slide += HorizontalLength(position - foot.LastPosition);

				const bool loaded = GetWeightShare(ragdoll, leg, rig.Legs[1 - i]) >= scLoadedShare;

				if (loaded && !foot.bLoaded) {
					foot.LoadedSlip = Vec3f::sZero;
				}

				foot.bLoaded = loaded;

				physics::BodyGroundContact contact;

				if (loaded && ragdoll.GetBodyGroundContact(leg.Foot, contact)) {
					JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();
					const JPH::BodyID foot_id = ragdoll.GetBodyID(leg.Foot);
					const JPH::Vec3 lever = Vec3f::ToJolt(contact.Point - position);
					const JPH::Vec3 contact_velocity = bodies.GetLinearVelocity(foot_id) +
													   bodies.GetAngularVelocity(foot_id).Cross(lever);

					foot.LoadedSlip += Vec3f::FromJolt(contact_velocity) * scDeltaTime;
					foot.LoadedSlide = std::max(foot.LoadedSlide, HorizontalLength(foot.LoadedSlip));
				}
			}

			foot.AirFrames = 0;
		}
		else {
			foot.AirFrames++;

			if (foot.bPlanted && foot.AirFrames >= scMinAirFrames) {
				foot.bPlanted = false;
				foot.TakeoffFrame = mFrame - foot.AirFrames;
				foot.TakeoffPosition = foot.LastPosition;
				FinishStance(run, foot);

				if (run.LastTakeoffFrame >= 0) {
					const float32 gap = static_cast<float32>(foot.TakeoffFrame - run.LastTakeoffFrame) * scDeltaTime;

					if (run.MinStepGap == 0.0f || gap < run.MinStepGap) {
						run.MinStepGap = gap;
					}
				}

				run.LastTakeoffFrame = foot.TakeoffFrame;
			}
		}

		if (foot.bPlanted) {
			run.KneeStanceSum += knee;
			run.KneeStanceCount++;
			run.PlantedFootSpinSum +=
				gPhysics->pBackend->GetBodyInterface().GetAngularVelocity(ragdoll.GetBodyID(leg.Foot)).Length() *
				scRadToDeg;
			run.PlantedFootSpinCount++;
		}
		else {
			constexpr float32 scMinReversalSpeed = 0.2f;

			run.PeakLift = std::max(run.PeakLift, position.Y - foot.StanceHeight);
			run.PeakSwingKnee = std::max(run.PeakSwingKnee, knee);

			Vec3f air_velocity = (position - foot.LastPosition) / scDeltaTime;
			air_velocity.Y = 0.0f;

			if (air_velocity.Length() > scMinReversalSpeed) {
				if (air_velocity.Dot(foot.AirVelocity) < 0.0f) {
					run.FootReversals++;
				}

				foot.AirVelocity = air_velocity;
			}

			run.FootAirFrames++;
		}

		foot.LastPosition = position;
	}
}

void RagdollBench::Sample(TrialRun& run)
{
	if (run.pNPC == nullptr) {
		return;
	}

	NPC& npc = *run.pNPC;
	physics::Ragdoll& ragdoll = npc.GetRagdoll();
	const physics::RagdollRig& rig = ragdoll.GetRig();
	const uint32 segments = ragdoll.GetSegmentCount();

	Vec3f center = Vec3f::sZero;

	if (run.pTrial->bTrace && run.Trace.Capacity == 0) {
		run.Trace.InitCapacity(static_cast<size_t>(mEndFrame / scTraceStride + 1) * segments * scTraceValues);
	}

	const bool trace_frame = run.pTrial->bTrace && (mFrame % scTraceStride == 0);

	for (uint32 i = 0; i < segments; i++) {
		const Vec3f position = ragdoll.GetBodyPosition(i);
		const Vec3f origin = ragdoll.GetBodyOrigin(i);

		center += position * ragdoll.GetBodyMass(i);

		const float32 values[scTraceValues] = { origin.X, origin.Y, origin.Z, position.X, position.Y, position.Z };
		run.Hash = HashBytes(run.Hash, values, sizeof(values));

		if (trace_frame) {
			for (const float32 value : values) {
				float32 copy = value;
				run.Trace.Insert(std::move(copy));
			}
		}
	}

	if (trace_frame) {
		run.TraceFrames++;
	}

	center = center / ragdoll.GetTotalMass();

	Vec3f velocity = Vec3f::sZero;

	if (mFrame > 0) {
		velocity = (center - run.PreviousCenter) / scDeltaTime;
	}

	run.PreviousCenter = center;

	const int32 since_hit = mFrame - run.HitFrame;

	if (since_hit < 0) {
		run.PreHitVelocity = velocity;
		run.HitOrigin = ragdoll.GetBodyOrigin(0);
		return;
	}

	if (since_hit == 2) {
		run.DeltaV = HorizontalLength(velocity - run.PreHitVelocity);
	}

	SampleQuality(run, since_hit);

	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < segments; i++) {
		run.PeakBodySpeed = std::max(run.PeakBodySpeed, bodies.GetLinearVelocity(ragdoll.GetBodyID(i)).Length());
	}

	const Vec3f direction = GetDirection(run.pTrial->Direction);
	const float32 knock = (ragdoll.GetBodyOrigin(0) - run.HitOrigin).Dot(direction);

	if (since_hit == 30) {
		run.Knock05 = knock;
	}
	else if (since_hit == 60) {
		run.Knock10 = knock;
	}

	if (run.FallFrame < 0 && npc.IsFallen()) {
		run.FallFrame = since_hit;
		run.FallReason = static_cast<int32>(ragdoll.GetBalanceStatus().FallReason);
	}

	if (run.ReturnFrame < 0 && since_hit > 2 && !npc.IsPhysicsActive()) {
		run.ReturnFrame = since_hit;
	}

	// Only the balancer's own time counts: once the body has gone down or is tracking its walk animation again, its
	// feet and limbs say nothing about balance
	if (run.FallFrame >= 0 || !ragdoll.IsBalancing()) {
		run.Feet[0].bStarted = false;
		run.Feet[1].bStarted = false;
		return;
	}

	run.BalanceFrames++;
	run.HangTime = ragdoll.GetBalanceStatus().HangingTime;
	run.ContactFlips = ragdoll.GetBalanceStatus().ContactFlips;
	run.SwingOvertime = ragdoll.GetBalanceStatus().SwingOvertime;
	run.SwingRetries = ragdoll.GetBalanceStatus().SwingRetries;
	run.PeakSwingTargetStep = ragdoll.GetBalanceStatus().PeakSwingTargetStep;
	run.SwingLandingTravel = ragdoll.GetBalanceStatus().SwingLandingTravel;
	run.PelvisDrop = std::max(run.PelvisDrop, run.HitOrigin.Y - ragdoll.GetBodyOrigin(0).Y);
	run.PelvisRise = ragdoll.GetBodyOrigin(0).Y - run.HitOrigin.Y;

	{
		constexpr float32 scMinBob = 0.01f;

		const float32 height = ragdoll.GetBodyOrigin(0).Y;

		if (run.PelvisDirection == 0) {
			run.PelvisExtreme = height;
			run.PelvisDirection = 1;
		}
		else if ((height - run.PelvisExtreme) * static_cast<float32>(run.PelvisDirection) > 0.0f) {
			run.PelvisExtreme = height;
		}
		else if (std::abs(height - run.PelvisExtreme) >= scMinBob) {
			run.PelvisBobs++;
			run.PelvisExtreme = height;
			run.PelvisDirection = -run.PelvisDirection;
		}
	}

	run.PeakSpeed = std::max(run.PeakSpeed, HorizontalLength(velocity));

	const Vec3f chest_up = npc.GetBehaviours().GetContext().GetChestUp();
	const float32 tilt = std::acos(MathUtil::Clamp(chest_up.Dot(Vec3f::sUp), -1.0f, 1.0f)) * scRadToDeg;

	run.PeakTilt = std::max(run.PeakTilt, tilt);
	run.AssistImpulse += ragdoll.GetBalanceStatus().AssistForce * scDeltaTime;

	for (uint32 i = 0; i < segments; i++) {
		if (ragdoll.GetBodyPart(i) == physics::eRDBodyPart::Head) {
			run.PeakHeadSpin = std::max(run.PeakHeadSpin,
										bodies.GetAngularVelocity(ragdoll.GetBodyID(i)).Length() * scRadToDeg);
		}
	}

	const JPH::Vec3 trunk_spin = bodies.GetAngularVelocity(ragdoll.GetBodyID(rig.Chest));
	run.PeakTrunkSpin = std::max(
		run.PeakTrunkSpin,
		std::sqrt(trunk_spin.GetX() * trunk_spin.GetX() + trunk_spin.GetZ() * trunk_spin.GetZ()) * scRadToDeg);

	if (since_hit >= scLateSpinFrame) {
		run.LateTrunkSpinSum += std::sqrt(trunk_spin.GetX() * trunk_spin.GetX() +
										  trunk_spin.GetZ() * trunk_spin.GetZ()) *
								scRadToDeg;

		for (uint32 i = 0; i < segments; i++) {
			if (ragdoll.GetBodyPart(i) == physics::eRDBodyPart::Head) {
				const JPH::Vec3 head_spin = bodies.GetAngularVelocity(ragdoll.GetBodyID(i));

				run.LateHeadSpinSum += std::sqrt(head_spin.GetX() * head_spin.GetX() +
												 head_spin.GetZ() * head_spin.GetZ()) *
									   scRadToDeg;
			}
		}

		run.LateSpinCount++;
	}

	for (const physics::RagdollRigArm& arm : rig.Arms) {
		run.PeakHandSpin = std::max(run.PeakHandSpin,
									bodies.GetAngularVelocity(ragdoll.GetBodyID(arm.Hand)).Length() * scRadToDeg);
	}

	SampleFeet(run);
}

void RagdollBench::WriteTrial(const TrialRun& run)
{
	const Trial& trial = *run.pTrial;

	float32 step_length = 0.0f;
	float32 step_time = 0.0f;

	if (run.Steps > 0) {
		step_length = run.StepLengthSum / static_cast<float32>(run.Steps);
		step_time = run.StepTimeSum / static_cast<float32>(run.Steps);
	}

	float32 stance_knee = 0.0f;

	if (run.KneeStanceCount > 0) {
		stance_knee = run.KneeStanceSum / static_cast<float32>(run.KneeStanceCount);
	}

	float32 slide = 0.0f;

	if (run.Stances > 0) {
		slide = run.SlideSum / static_cast<float32>(run.Stances);
	}

	float32 late_trunk_spin = 0.0f;
	float32 late_head_spin = 0.0f;
	float32 head_swings[2] = {};

	for (uint32 stage = 0; stage < 2; stage++) {
		if (run.Look.HeadFrames[stage] > 0) {
			head_swings[stage] = static_cast<float32>(run.Look.HeadSwings[stage]) /
								 (static_cast<float32>(run.Look.HeadFrames[stage]) * scDeltaTime);
		}
	}


	if (run.LateSpinCount > 0) {
		late_trunk_spin = run.LateTrunkSpinSum / static_cast<float32>(run.LateSpinCount);
		late_head_spin = run.LateHeadSpinSum / static_cast<float32>(run.LateSpinCount);
	}

	float32 foot_spin = 0.0f;

	if (run.PlantedFootSpinCount > 0) {
		foot_spin = run.PlantedFootSpinSum / static_cast<float32>(run.PlantedFootSpinCount);
	}

	float32 fall_time = -1.0f;

	if (run.FallFrame >= 0) {
		fall_time = static_cast<float32>(run.FallFrame) * scDeltaTime;
	}

	float32 return_time = -1.0f;

	if (run.ReturnFrame >= 0) {
		return_time = static_cast<float32>(run.ReturnFrame) * scDeltaTime;
	}

	const float32 jitter_count = static_cast<float32>(std::max(run.Look.JitterCount, 1u));

	std::fprintf(
		mpOut,
		"{\"name\":\"%s\",\"condition\":\"%s\",\"still\":%d,\"dir\":%.0f,\"phase\":%u,\"spawned\":%d,"
		"\"fell\":%d,\"fall_time\":%.3f,\"fall_reason\":%d,\"return_time\":%.3f,\"balance_time\":%.3f,\"delta_v\":%.3f,"
		"\"knock05\":%.3f,\"knock10\":%.3f,\"peak_speed\":%.3f,\"peak_tilt\":%.1f,\"head_spin\":%.1f,"
		"\"hand_spin\":%.1f,\"trunk_spin\":%.1f,\"late_trunk_spin\":%.1f,\"late_head_spin\":%.1f,\"assist_impulse\":%."
		"2f,\"stance_knee\":%.1f,\"swing_knee\":%.1f,\"steps\":%u,"
		"\"step_length\":%.3f,\"step_time\":%.3f,\"min_step_time\":%.3f,\"min_step_gap\":%.3f,\"lift\":%.3f,"
		"\"stances\":%u,\"slide\":%.4f,\"peak_slide\":%.4f,\"peak_loaded_slide\":%.4f,\"peak_body_speed\":%.2f,\"foot_"
		"spin\":%.1f,\"jitter_head\":%.2f,\"jitter_trunk\":%.2f,\"jitter_arm\":%.2f,\"jitter_leg\":%.2f,"
		"\"twitch_frames\":%u,\"limit_excess\":%.2f,\"limit_frames\":%u,\"joint_gap\":%.4f,\"root_jitter\":%.2f,"
		"\"hang_time\":%.3f,\"contact_flips\":%u,\"swing_overtime\":%.3f,\"swing_retries\":%u,\"swing_target_step\":%.4f,\"landing_travel\":%.3f,\"foot_reversals\":%u,\"foot_air_time\":%.3f,\"pelvis_low\":%.3f,\"pelvis_rise\":%.3f,\"pelvis_bobs\":%u,\"head_swings\":%.2f,\"fall_head_swings\":%.2f,\"hit_move\":%.4f,\"com_025\":%.4f,\"com_05\":%.4f,\"com_10\":%.4f,\"chest_lean\":%.4f,\"yaw_response\":%."
		"2f,\"yaw_expected\":%.4f,\"lever\":%.3f,\"hash\":\"%"
		"016llx\"}\n",
		trial.Name, trial.Condition, static_cast<int>(trial.bStill), trial.Direction, trial.Phase,
		static_cast<int>(run.pNPC != nullptr), static_cast<int>(run.FallFrame >= 0), fall_time, run.FallReason,
		return_time, static_cast<float32>(run.BalanceFrames) * scDeltaTime, run.DeltaV, run.Knock05, run.Knock10,
		run.PeakSpeed, run.PeakTilt, run.PeakHeadSpin, run.PeakHandSpin, run.PeakTrunkSpin, late_trunk_spin,
		late_head_spin, run.AssistImpulse, stance_knee, run.PeakSwingKnee, run.Steps, step_length, step_time,
		run.MinStepTime, run.MinStepGap, run.PeakLift, run.Stances, slide, run.PeakSlide, run.PeakLoadedSlide,
		run.PeakBodySpeed, foot_spin, run.Look.JitterSum[0] / jitter_count, run.Look.JitterSum[1] / jitter_count,
		run.Look.JitterSum[2] / jitter_count, run.Look.JitterSum[3] / jitter_count, run.Look.TwitchFrames,
		run.Look.PeakLimitExcess, run.Look.LimitFrames, run.Look.PeakJointGap, run.Look.PeakRootJitter,
		run.HangTime, run.ContactFlips, run.SwingOvertime, run.SwingRetries, run.PeakSwingTargetStep,
		run.SwingLandingTravel, run.FootReversals, static_cast<float32>(run.FootAirFrames) * scDeltaTime, run.PelvisDrop, run.PelvisRise, run.PelvisBobs, head_swings[0], head_swings[1], run.Look.HitBodyMove, run.Look.CenterMove[0], run.Look.CenterMove[1], run.Look.CenterMove[2], run.Look.ChestLean,
		run.Look.YawResponse, run.Look.ExpectedYaw, run.Look.LeverHeight, static_cast<unsigned long long>(run.Hash));
}

void RagdollBench::WriteTrace(const TrialRun& run)
{
	if (run.pTrial->HitPart != nullptr && run.Look.BodyJitter.Size > 0) {
		std::fprintf(mpOut, "{\"bodies\":\"%s\",\"gap_frame\":%d,\"jitter\":[", run.pTrial->Name, run.Look.WorstGapFrame);

		for (uint32 i = 0; i < run.Look.BodyJitter.Size; i++) {
			std::fprintf(mpOut, "%s%.1f", (i == 0) ? "" : ",", run.Look.BodyJitter[i] / std::max(run.Look.JitterCount, 1u));
		}

		std::fprintf(mpOut, "],\"limit\":[");

		for (uint32 i = 0; i < run.Look.BodyLimit.Size; i++) {
			std::fprintf(mpOut, "%s%.1f", (i == 0) ? "" : ",", run.Look.BodyLimit[i]);
		}

		std::fprintf(mpOut, "]}\n");
	}

	if (mOptions.TraceDir == nullptr || !run.pTrial->bTrace || run.pNPC == nullptr) {
		return;
	}

	const std::string path = std::format("{}/{}.trace", mOptions.TraceDir, run.pTrial->Name);
	std::FILE* file = std::fopen(path.c_str(), "wb");

	if (file == nullptr) {
		LogError(LC_CORE, "Could not write ragdoll bench trace '{}'", path);
		return;
	}

	const physics::Ragdoll& ragdoll = run.pNPC->GetRagdoll();
	const int32 header[] = { static_cast<int32>(ragdoll.GetSegmentCount()),
							 static_cast<int32>(run.TraceFrames),
							 static_cast<int32>(scTraceStride),
							 run.HitFrame,
							 run.FallFrame,
							 static_cast<int32>(run.pTrial->Direction) };

	std::fwrite(header, sizeof(int32), sizeof(header) / sizeof(header[0]), file);

	for (uint32 i = 0; i < ragdoll.GetSegmentCount(); i++) {
		const int32 parent = ragdoll.GetParentBody(i);
		std::fwrite(&parent, sizeof(int32), 1, file);
	}

	std::fwrite(run.Trace.pData, sizeof(float32), run.Trace.Size, file);
	std::fclose(file);
}

void RagdollBench::FinishBatch()
{
	for (const TrialRun& run : mRuns) {
		WriteTrial(run);
		WriteTrace(run);
	}

	if (mActiveFrames > 0 && mIdleFrames > 0) {
		std::fprintf(mpOut, "{\"perf\":{\"idle_ms\":%.4f,\"active_ms\":%.4f,\"active_npcs\":%.2f,\"npcs\":%u}}\n",
					 mIdleMs / mIdleFrames, mActiveMs / mActiveFrames,
					 static_cast<float64>(mActiveNPCSum) / mActiveFrames, static_cast<uint32>(mRuns.Size));
	}

	std::fflush(mpOut);

	mIdleMs = 0.0;
	mIdleFrames = 0;
	mActiveMs = 0.0;
	mActiveFrames = 0;
	mActiveNPCSum = 0;

	mRuns.Free();
	gNPCManager->Clear();
	RemoveTerrain();
}

bool RagdollBench::PreStep()
{
	// Posing a new skeleton reads this, so it has to be what the game's Tick() would have set
	renderer::gGraphics->DeltaTime = scDeltaTime;

	if (mStage == eStage::WaitForModel) {
		if (gNPCManager->GetModel() == nullptr) {
			return false;
		}

		CreateFloor();
		mStage = eStage::NextBatch;
	}

	// Always at the start of a frame, since a skeleton created after the frame's NPC update would get its first advance
	// from the renderer, or not at all without one
	if (mStage == eStage::NextBatch) {
		if (StartNextBatch()) {
			mStage = eStage::Running;
		}
		else {
			std::fprintf(mpOut, "{\"done\":true}\n");
			std::fclose(mpOut);
			mpOut = nullptr;
			mStage = eStage::Finished;
		}

		return false;
	}

	if (mStage != eStage::Running) {
		return false;
	}

	for (TrialRun& run : mRuns) {
		FireShots(run);
	}

	return true;
}

void RagdollBench::Tick()
{
	// The same order as the game's Tick(), which steps and counts the frame whether or not a trial has started yet
	const bool stepping = PreStep();

	const auto start = std::chrono::steady_clock::now();

	gPhysics->pBackend->Update(scDeltaTime);
	gNPCManager->Update(scDeltaTime, false);

	const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;

	if (stepping) {
		if (mFrame < mFirstHitFrame) {
			mIdleMs += elapsed.count();
			mIdleFrames++;
		}
		else if (mFrame < mFirstHitFrame + scPerfActiveFrames) {
			mActiveMs += elapsed.count();
			mActiveFrames++;
			mActiveNPCSum += gNPCManager->GetActiveCount();
		}

		PostStep();
	}

	renderer::gGraphics->CountFrameWithoutDrawing();
}

void RagdollBench::PostStep()
{
	for (TrialRun& run : mRuns) {
		Sample(run);
	}

	mFrame++;

	if (mFrame < mEndFrame) {
		return;
	}

	FinishBatch();
	mStage = eStage::NextBatch;
}

} // namespace fx
