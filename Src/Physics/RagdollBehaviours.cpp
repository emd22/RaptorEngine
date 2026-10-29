#include "RagdollBehaviours.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/TwoBoneIK.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>
#include <cstring>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scGroundNormalUp = 0.5f;
constexpr float32 scLostSupportTime = 0.15f;
constexpr float32 scOnGroundTime = 0.25f;
constexpr float32 scOnGroundMaxSpeed = 1.5f;
constexpr float32 scOnGroundRearmTime = 0.5f;
constexpr float32 scDropRayLength = 50.0f;
constexpr uint32 scImpactSteps = 16;
constexpr float32 scImpactStep = 0.05f;

constexpr float32 scElbowDown = 1.0f;
constexpr float32 scElbowBack = 0.3f;
constexpr float32 scElbowOut = 0.6f;

constexpr float32 scDebugCross = 0.05f;

Vec3f SafeNormalize(const Vec3f v, const Vec3f fallback)
{
	const float32 length = v.Length();

	if (length < 1e-5f) {
		return fallback;
	}

	return v / length;
}

void DrawCross(const Vec3f point, Color color)
{
	renderer::gDebugDraw->Line(point - Vec3f(scDebugCross, 0.0f, 0.0f), point + Vec3f(scDebugCross, 0.0f, 0.0f), color);
	renderer::gDebugDraw->Line(point - Vec3f(0.0f, scDebugCross, 0.0f), point + Vec3f(0.0f, scDebugCross, 0.0f), color);
	renderer::gDebugDraw->Line(point - Vec3f(0.0f, 0.0f, scDebugCross), point + Vec3f(0.0f, 0.0f, scDebugCross), color);
}

} // namespace

int32 RagdollBehaviours::FindChildBody(const Ragdoll& ragdoll, uint32 parent, eRagdollBodyPart part)
{
	for (uint32 i = parent + 1; i < ragdoll.mBodies.Size; i++) {
		const Ragdoll::BodyInfo& body = ragdoll.mBodies[i];

		if (body.ParentBody == static_cast<int32>(parent) && body.Part == part) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

RagdollBehaviours::RagdollBehaviours()
	: mBehaviours { &mFallen, &mStagger, &mShot, &mBrace, &mProtectHead, &mHighFall, &mWrithe, &mDying }
{
	std::copy(mBehaviours, mBehaviours + scBehaviourCount, mOrder);
}

bool RagdollBehaviours::Init(Ragdoll& ragdoll)
{
	Reset();

	if (!ragdoll.IsCreated() || !BuildRig(ragdoll)) {
		mRig.bValid = false;
		return false;
	}

	const uint32 body_count = ragdoll.GetBodyCount();

	mContext.pRagdoll = &ragdoll;
	mContext.pRig = &mRig;
	mContext.pBalance = &ragdoll.GetBalanceStatus();
	InitFilled(mContext.Bodies, body_count, BehaviourBody {});

	mOutput.Init(body_count);

	InitFilled(mMixPose, body_count, JPH::Quat::sIdentity());
	InitFilled(mMixOffset, body_count, JPH::Quat::sIdentity());
	InitFilled(mMixWeight, body_count, 0.0f);
	InitFilled(mMixStrength, body_count, 1.0f);

	for (RagdollBehaviour* behaviour : mBehaviours) {
		behaviour->Init(mRig, body_count);
	}

	return true;
}

bool RagdollBehaviours::BuildRig(const Ragdoll& ragdoll)
{
	const SizedArray<Ragdoll::BodyInfo>& bodies = ragdoll.mBodies;

	mRig = RagdollRig {};

	if (bodies.Size < 2) {
		return false;
	}

	const JPH::Vec3 up = GetUpFromGravity(gPhysics->pBackend->PhysicsSystem.GetGravity());

	if (ragdoll.mBalance.IsValid()) {
		mRig.RestForward = ragdoll.mBalance.GetRestForward();
	}

	const JPH::Vec3 side = up.Cross(mRig.RestForward);
	const JPH::Vec3 pelvis = bodies[0].RestPosition;

	uint32 arm_count = 0;
	uint32 leg_count = 0;

	for (uint32 i = 1; i < bodies.Size; i++) {
		const Ragdoll::BodyInfo& body = bodies[i];
		const eRagdollBodyPart parent_part = bodies[body.ParentBody].Part;

		if (body.Part == eRagdollBodyPart::Head && mRig.Neck == 0) {
			mRig.Neck = i;
		}

		const float32 side_sign = ((body.RestPosition - pelvis).Dot(side) >= 0.0f) ? 1.0f : -1.0f;

		if (body.Part == eRagdollBodyPart::Arm && parent_part != eRagdollBodyPart::Arm &&
			arm_count < RagdollRig::scLimbCount) {
			const int32 lower = FindChildBody(ragdoll, i, eRagdollBodyPart::Arm);

			if (lower < 0) {
				continue;
			}

			const int32 hand = FindChildBody(ragdoll, lower, eRagdollBodyPart::Arm);

			if (hand < 0) {
				continue;
			}

			mRig.Arms[arm_count++] = RagdollArm {
				.Clavicle = static_cast<uint32>(body.ParentBody),
				.Upper = i,
				.Lower = static_cast<uint32>(lower),
				.Hand = static_cast<uint32>(hand),
				.UpperLength = (bodies[lower].RestPosition - body.RestPosition).Length(),
				.LowerLength = (bodies[hand].RestPosition - bodies[lower].RestPosition).Length(),
				.SideSign = side_sign,
			};
		}

		if (body.Part == eRagdollBodyPart::Leg && parent_part != eRagdollBodyPart::Leg &&
			leg_count < RagdollRig::scLimbCount) {
			const int32 shin = FindChildBody(ragdoll, i, eRagdollBodyPart::Leg);

			if (shin < 0) {
				continue;
			}

			const int32 foot = FindChildBody(ragdoll, shin, eRagdollBodyPart::Leg);

			if (foot < 0) {
				continue;
			}

			mRig.Legs[leg_count++] = RagdollLeg {
				.Thigh = i, .Shin = static_cast<uint32>(shin), .Foot = static_cast<uint32>(foot), .SideSign = side_sign
			};
		}
	}

	if (mRig.Neck == 0 || bodies[mRig.Neck].ParentBody <= 0) {
		LogWarning(LC_PHYSICS, "Ragdoll behaviours: no head with a spine below it, behaviours are off");
		return false;
	}

	mRig.Chest = static_cast<uint32>(bodies[mRig.Neck].ParentBody);

	for (int32 walk = static_cast<int32>(mRig.Chest); walk > 0 && mRig.SpineCount < RagdollRig::scMaxSpine;
		 walk = bodies[walk].ParentBody) {
		mRig.Spine[mRig.SpineCount++] = static_cast<uint32>(walk);
	}

	const bool has_sides = (mRig.Arms[0].SideSign != mRig.Arms[1].SideSign) &&
						   (mRig.Legs[0].SideSign != mRig.Legs[1].SideSign);

	if (arm_count != RagdollRig::scLimbCount || leg_count != RagdollRig::scLimbCount || !has_sides) {
		LogWarning(LC_PHYSICS,
				   "Ragdoll behaviours: expected two arms and two legs ({} arms, {} legs), behaviours are off",
				   arm_count, leg_count);
		return false;
	}

	mRig.bValid = true;

	return true;
}

void RagdollBehaviours::Reset()
{
	for (RagdollBehaviour* behaviour : mBehaviours) {
		behaviour->Reset();
	}

	mContext.bFreePose = false;
	mContext.bAlive = true;
	mContext.Time = 0.0f;
	mContext.AirTime = 0.0f;
	mContext.GroundTime = 0.0f;

	mUnsupportedTime = 0.0f;
	mOffGroundTime = 0.0f;
	mbLostSupportSent = false;
	mbOnGroundSent = false;
	mFrameEvents = 0;
	mMixBalance = eBehaviourBalance::Unset;
	mMixGravity = 1.0f;

	for (uint32 i = 0; i < mMixPose.Size; i++) {
		mMixPose[i] = JPH::Quat::sIdentity();
		mMixOffset[i] = JPH::Quat::sIdentity();
		mMixWeight[i] = 0.0f;
		mMixStrength[i] = 1.0f;
	}

	for (BehaviourGoal& goal : mMixGoals) {
		goal = BehaviourGoal {};
	}
}

void RagdollBehaviours::CopySettings(const RagdollBehaviours& other)
{
	for (uint32 i = 0; i < scBehaviourCount; i++) {
		mBehaviours[i]->CopySettings(*other.mBehaviours[i]);
	}
}

void RagdollBehaviours::SetFreePose(bool free_pose) { mContext.bFreePose = free_pose; }

void RagdollBehaviours::SetAlive(bool alive) { mContext.bAlive = alive; }

void RagdollBehaviours::Sense(Ragdoll& ragdoll, float32 delta_time)
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::Vec3 gravity = gPhysics->pBackend->PhysicsSystem.GetGravity();
	const Vec3f up = ToVec3f(GetUpFromGravity(gravity));

	BehaviourContext& ctx = mContext;

	ctx.DeltaTime = delta_time;
	ctx.Time += delta_time;
	ctx.Up = up;
	ctx.Gravity = gravity.Length();
	ctx.pBalance = &ragdoll.GetBalanceStatus();
	ctx.bBalancing = ragdoll.IsBalancing();

	const bool is_powered = !ragdoll.IsLimp() && ragdoll.GetBodyMode(0) == eRagdollBodyMode::Powered;
	const bool is_awake = ragdoll.mpRagdoll->IsActive();

	Vec3f weighted_center = Vec3f::sZero;
	Vec3f momentum = Vec3f::sZero;
	float32 total_mass = 0.0f;

	bool any_contact = false;
	bool feet_supported = false;
	bool body_on_ground = false;

	for (uint32 i = 0; i < ctx.GetBodyCount(); i++) {
		const Ragdoll::BodyInfo& info = ragdoll.mBodies[i];
		const JPH::BodyID body_id = ragdoll.GetBodyID(i);
		BehaviourBody& body = ctx.Bodies[i];

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(body_id, position, orientation);

		body.Origin = ToVec3f(JPH::Vec3(position));
		body.Orientation = orientation;
		body.Center = ToVec3f(JPH::Vec3(body_interface.GetCenterOfMassPosition(body_id)));
		body.Velocity = ToVec3f(body_interface.GetLinearVelocity(body_id));

		BodyGroundContact contact;
		body.bContact = is_powered && ragdoll.GetBodyGroundContact(i, contact);
		body.bGround = body.bContact && contact.Normal.Dot(up) > scGroundNormalUp;

		any_contact = any_contact || body.bContact;
		feet_supported = feet_supported || (info.bFoot && body.bGround);
		body_on_ground = body_on_ground || (body.bGround && (info.Part == eRagdollBodyPart::Torso ||
															 info.Part == eRagdollBodyPart::Head));

		weighted_center += body.Center * info.Mass;
		momentum += body.Velocity * info.Mass;
		total_mass += info.Mass;
	}

	if (total_mass > 0.0f) {
		ctx.CenterOfMass = weighted_center / total_mass;
		ctx.Velocity = momentum / total_mass;
	}

	ctx.bFeetSupported = feet_supported;
	ctx.bBodyOnGround = body_on_ground;
	ctx.bAirborne = is_powered && is_awake && !any_contact;

	if (ctx.bAirborne) {
		ctx.AirTime += delta_time;
	}
	else {
		ctx.AirTime = 0.0f;
	}

	if (body_on_ground) {
		ctx.GroundTime += delta_time;
	}
	else {
		ctx.GroundTime = 0.0f;
	}

	SenseChestFrame(ragdoll);

	ctx.DropHeight = 0.0f;
	ctx.bImpactPredicted = false;

	if (!is_powered || !ctx.bFreePose) {
		return;
	}

	const RayResult floor = gPhysics->pBackend->RaycastStatic(ctx.CenterOfMass, -up * scDropRayLength);

	if (floor.bHit) {
		ctx.DropHeight = (ctx.CenterOfMass - floor.Point).Dot(up);
	}
	else {
		ctx.DropHeight = scDropRayLength;
	}

	if (!body_on_ground) {
		PredictImpact();
	}
}

void RagdollBehaviours::SenseChestFrame(const Ragdoll& ragdoll)
{
	BehaviourContext& ctx = mContext;

	const JPH::Quat delta = ctx.Bodies[mRig.Chest].Orientation *
							ragdoll.mBodies[mRig.Chest].RestOrientation.Conjugated();
	const JPH::Vec3 up = ToJolt(ctx.Up);

	ctx.Forward = ToVec3f((delta * mRig.RestForward).Normalized());
	ctx.Right = ToVec3f((delta * up.Cross(mRig.RestForward)).NormalizedOr(JPH::Vec3::sAxisX()));
}

void RagdollBehaviours::PredictImpact()
{
	BehaviourContext& ctx = mContext;

	const BehaviourBody& chest = ctx.Bodies[mRig.Chest];
	const Vec3f gravity = ctx.Up * -ctx.Gravity;

	Vec3f point = chest.Center;

	for (uint32 step = 1; step <= scImpactSteps; step++) {
		const float32 time = scImpactStep * static_cast<float32>(step);
		const Vec3f next = chest.Center + chest.Velocity * time + gravity * (0.5f * time * time);
		const Vec3f segment = next - point;
		const float32 segment_length = segment.Length();

		const RayResult hit = gPhysics->pBackend->RaycastStatic(point, segment);

		if (hit.bHit) {
			float32 fraction = 0.0f;

			if (segment_length > 1e-5f) {
				fraction = (hit.Point - point).Length() / segment_length;
			}

			ctx.bImpactPredicted = true;
			ctx.ImpactPoint = hit.Point;
			ctx.ImpactNormal = SafeNormalize(hit.Normal, ctx.Up);
			ctx.ImpactTime = time - scImpactStep * (1.0f - std::clamp(fraction, 0.0f, 1.0f));
			return;
		}

		point = next;
	}
}

void RagdollBehaviours::EmitSupportEvents(float32 delta_time)
{
	const BehaviourContext& ctx = mContext;

	if (ctx.bFeetSupported || ctx.bBodyOnGround || !ctx.bAirborne) {
		mUnsupportedTime = 0.0f;

		if (ctx.bFeetSupported || ctx.bBodyOnGround) {
			mbLostSupportSent = false;
		}
	}
	else {
		mUnsupportedTime += delta_time;

		if (mUnsupportedTime >= scLostSupportTime && !mbLostSupportSent) {
			mbLostSupportSent = true;
			Dispatch(RagdollEvent { .Type = eRagdollEvent::LostSupport, .Magnitude = -ctx.Velocity.Dot(ctx.Up) });
		}
	}

	if (!ctx.bBodyOnGround) {
		mOffGroundTime += delta_time;

		if (mOffGroundTime >= scOnGroundRearmTime) {
			mbOnGroundSent = false;
		}

		return;
	}

	mOffGroundTime = 0.0f;

	if (!mbOnGroundSent && ctx.GroundTime >= scOnGroundTime && ctx.Velocity.Length() < scOnGroundMaxSpeed) {
		mbOnGroundSent = true;
		Dispatch(RagdollEvent { .Type = eRagdollEvent::OnGround, .Point = ctx.CenterOfMass });
	}
}

void RagdollBehaviours::Dispatch(const RagdollEvent& event)
{
	mFrameEvents |= 1u << static_cast<uint32>(event.Type);

	for (RagdollBehaviour* behaviour : mBehaviours) {
		if (behaviour->IsEnabled()) {
			behaviour->OnEvent(mContext, event);
		}
	}
}

void RagdollBehaviours::SortByPriority()
{
	for (uint32 i = 1; i < scBehaviourCount; i++) {
		RagdollBehaviour* behaviour = mOrder[i];
		uint32 j = i;

		while (j > 0 && mOrder[j - 1]->Priority > behaviour->Priority) {
			mOrder[j] = mOrder[j - 1];
			j--;
		}

		mOrder[j] = behaviour;
	}
}

void RagdollBehaviours::HandleEvent(Ragdoll& ragdoll, const RagdollEvent& event)
{
	if (!mRig.bValid || !ragdoll.IsCreated() || ragdoll.IsLimp()) {
		return;
	}

	Sense(ragdoll, 0.0f);
	Dispatch(event);
	SortByPriority();
	Mix();
	Apply(ragdoll);
}

void RagdollBehaviours::Update(Ragdoll& ragdoll, float32 delta_time)
{
	if (!mRig.bValid || !ragdoll.IsCreated() || ragdoll.IsLimp()) {
		return;
	}

	mFrameEvents = 0;

	Sense(ragdoll, delta_time);
	EmitSupportEvents(delta_time);

	for (RagdollBehaviour* behaviour : mBehaviours) {
		if (!behaviour->IsEnabled()) {
			continue;
		}

		behaviour->Update(mContext);
		behaviour->Advance(delta_time);
	}

	SortByPriority();
	Mix();
	Apply(ragdoll);
}

void RagdollBehaviours::Mix()
{
	for (uint32 i = 0; i < mMixPose.Size; i++) {
		mMixPose[i] = JPH::Quat::sIdentity();
		mMixOffset[i] = JPH::Quat::sIdentity();
		mMixWeight[i] = 0.0f;
		mMixStrength[i] = 1.0f;
	}

	for (BehaviourGoal& goal : mMixGoals) {
		goal = BehaviourGoal {};
	}

	mMixBalance = eBehaviourBalance::Unset;
	mMixGravity = 1.0f;

	for (const RagdollBehaviour* behaviour : mOrder) {
		const float32 weight = behaviour->GetWeight();

		if (!behaviour->IsEnabled() || !behaviour->IsRunning() || weight <= 0.0f) {
			continue;
		}

		mOutput.Reset();
		behaviour->Contribute(mContext, mOutput);
		FoldOutput(mOutput, weight);
	}
}

void RagdollBehaviours::FoldOutput(const BehaviourOutput& output, float32 weight)
{
	for (uint32 i = 0; i < mMixPose.Size; i++) {
		const float32 mask = output.Mask[i] * weight;

		if (mask > 0.0f) {
			if (output.bAdditive) {
				mMixOffset[i] = (mMixOffset[i] * JPH::Quat::sIdentity().SLERP(output.Pose[i], mask)).Normalized();
			}
			else {
				FoldOverride(i, output.Pose[i], mask);
			}
		}

		mMixStrength[i] *= 1.0f + (output.Strength[i] - 1.0f) * weight;
	}

	mMixGravity *= 1.0f + (output.GravityScale - 1.0f) * weight;

	for (uint32 arm = 0; arm < RagdollRig::scLimbCount; arm++) {
		const float32 goal_weight = output.Hands[arm].Weight * weight;

		if (goal_weight > 0.0f) {
			SolveArm(arm, output.Hands[arm], goal_weight);
		}
	}

	if (output.Balance != eBehaviourBalance::Unset) {
		mMixBalance = output.Balance;
	}
}

void RagdollBehaviours::FoldOverride(uint32 body, const JPH::Quat& pose, float32 weight)
{
	const float32 previous = mMixWeight[body];
	const float32 total = previous + (1.0f - previous) * weight;

	if (previous <= 0.0f) {
		mMixPose[body] = pose;
	}
	else {
		mMixPose[body] = mMixPose[body].SLERP(pose, weight / total).Normalized();
	}

	mMixWeight[body] = total;
	mMixOffset[body] = mMixOffset[body].SLERP(JPH::Quat::sIdentity(), weight).Normalized();
}

void RagdollBehaviours::SolveArm(uint32 arm, const BehaviourGoal& goal, float32 weight)
{
	const BehaviourContext& ctx = mContext;
	const RagdollArm& chain = mRig.Arms[arm];
	const Ragdoll::BodyInfo& upper = ctx.pRagdoll->mBodies[chain.Upper];
	const Ragdoll::BodyInfo& lower = ctx.pRagdoll->mBodies[chain.Lower];

	const Vec3f chest_up = ctx.Forward.Cross(ctx.Right);
	const Vec3f shoulder = ctx.Bodies[chain.Upper].Origin;
	const Vec3f hint = -chest_up * scElbowDown - ctx.Forward * scElbowBack + ctx.Right * (chain.SideSign * scElbowOut);

	const TwoBoneIKResult ik = SolveTwoBoneIK(shoulder, goal.Target, chain.UpperLength, chain.LowerLength, hint);

	const Vec3f upper_direction = SafeNormalize(ik.Mid - shoulder, -chest_up);
	const Vec3f lower_direction = SafeNormalize(ik.End - ik.Mid, upper_direction);

	Vec3f hinge = upper_direction.Cross(lower_direction);

	if (hinge.Length() < 1e-3f) {
		hinge = upper_direction.Cross(hint);
	}

	hinge = SafeNormalize(hinge, ctx.Right);

	const JPH::Quat upper_world = FrameRotation(upper.LocalAxis, lower.ParentHingeLocal, ToJolt(upper_direction),
												ToJolt(hinge));
	const JPH::Quat lower_world = FrameRotation(lower.LocalAxis, lower.HingeLocal, ToJolt(lower_direction),
												ToJolt(hinge));
	const JPH::Quat parent = ctx.Bodies[chain.Clavicle].Orientation;

	FoldOverride(chain.Upper, (parent.Conjugated() * upper_world).Normalized(), weight);
	FoldOverride(chain.Lower, (upper_world.Conjugated() * lower_world).Normalized(), weight);

	if (weight >= mMixGoals[arm].Weight) {
		mMixGoals[arm] = BehaviourGoal { .Target = goal.Target, .Weight = weight };
	}
}

void RagdollBehaviours::Apply(Ragdoll& ragdoll) const
{
	for (uint32 i = 0; i < ragdoll.mLayers.Size && i < mMixPose.Size; i++) {
		Ragdoll::BodyLayer& layer = ragdoll.mLayers[i];

		layer.Pose = mMixPose[i];
		layer.Offset = mMixOffset[i];
		layer.Weight = mMixWeight[i];
		layer.GravityScale = mMixGravity;
	}
}

RagdollBehaviour* RagdollBehaviours::Find(const char* name)
{
	for (RagdollBehaviour* behaviour : mBehaviours) {
		if (std::strcmp(behaviour->GetName(), name) == 0) {
			return behaviour;
		}
	}

	return nullptr;
}

const RagdollBehaviour* RagdollBehaviours::Find(const char* name) const
{
	for (const RagdollBehaviour* behaviour : mBehaviours) {
		if (std::strcmp(behaviour->GetName(), name) == 0) {
			return behaviour;
		}
	}

	return nullptr;
}

bool RagdollBehaviours::Trigger(Ragdoll& ragdoll, const char* name)
{
	RagdollBehaviour* behaviour = Find(name);

	if (behaviour == nullptr || !mRig.bValid || !ragdoll.IsCreated() || ragdoll.IsLimp()) {
		return false;
	}

	Sense(ragdoll, 0.0f);
	behaviour->Start(mContext);
	SortByPriority();
	Mix();
	Apply(ragdoll);

	return behaviour->IsActive();
}

bool RagdollBehaviours::Stop(const char* name)
{
	RagdollBehaviour* behaviour = Find(name);

	if (behaviour == nullptr) {
		return false;
	}

	behaviour->Stop();

	return true;
}

bool RagdollBehaviours::IsRunning(const char* name) const
{
	const RagdollBehaviour* behaviour = Find(name);
	return behaviour != nullptr && behaviour->IsRunning();
}

bool RagdollBehaviours::IsIdle() const
{
	for (const RagdollBehaviour* behaviour : mBehaviours) {
		if (behaviour->IsRunning()) {
			return false;
		}
	}

	return true;
}

bool RagdollBehaviours::HadEvent(eRagdollEvent type) const
{
	return (mFrameEvents & (1u << static_cast<uint32>(type))) != 0;
}

void RagdollBehaviours::LogState() const
{
	for (const RagdollBehaviour* behaviour : mOrder) {
		behaviour->LogSettings();
	}
}

void RagdollBehaviours::DebugDraw() const
{
	if (!mRig.bValid || mContext.Bodies.IsEmpty()) {
		return;
	}

	if (mContext.bImpactPredicted) {
		DrawCross(mContext.ImpactPoint, Color::FromRGBA(255, 60, 60, 255));
	}

	for (uint32 arm = 0; arm < RagdollRig::scLimbCount; arm++) {
		const BehaviourGoal& goal = mMixGoals[arm];

		if (goal.Weight <= 0.0f) {
			continue;
		}

		const Vec3f shoulder = mContext.Bodies[mRig.Arms[arm].Upper].Origin;
		const uint8 green = static_cast<uint8>(80.0f + 175.0f * std::clamp(goal.Weight, 0.0f, 1.0f));

		renderer::gDebugDraw->Line(shoulder, goal.Target, Color::FromRGBA(40, green, 40, 255));
		DrawCross(goal.Target, Color::FromRGBA(40, green, 40, 255));
	}
}

} // namespace fx::physics
