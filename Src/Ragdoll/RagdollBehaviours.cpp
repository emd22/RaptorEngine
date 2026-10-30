#include "RagdollBehaviours.hpp"

#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <Math/TwoBoneIK.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
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

void DrawCross(const Vec3f point, Color color)
{
	renderer::gDebugDraw->Line(point - Vec3f(scDebugCross, 0.0f, 0.0f), point + Vec3f(scDebugCross, 0.0f, 0.0f), color);
	renderer::gDebugDraw->Line(point - Vec3f(0.0f, scDebugCross, 0.0f), point + Vec3f(0.0f, scDebugCross, 0.0f), color);
	renderer::gDebugDraw->Line(point - Vec3f(0.0f, 0.0f, scDebugCross), point + Vec3f(0.0f, 0.0f, scDebugCross), color);
}

} // namespace

RagdollBehaviours::RagdollBehaviours()
	: mBehaviours { &mFallen, &mStagger, &mArmBalance, &mShot, &mBrace, &mProtectHead, &mHighFall, &mWrithe, &mDying }
{
	std::copy(mBehaviours, mBehaviours + scBehaviourCount, mOrder);
}

bool RagdollBehaviours::Init(Ragdoll& ragdoll)
{
	Reset();

	mpRig = nullptr;

	if (!ragdoll.IsCreated() || !ragdoll.GetRig().bValid) {
		return false;
	}

	mpRig = &ragdoll.GetRig();

	const uint32 body_count = ragdoll.GetBodyCount();

	mContext.pRagdoll = &ragdoll;
	mContext.pRig = mpRig;
	mContext.pBalance = &ragdoll.GetBalanceStatus();
	mContext.Bodies.InitSize(body_count, BehaviourBody {});
	mSamples.InitSize(body_count, Ragdoll::BodyState {});

	mOutput.Init(body_count);

	mMixLayers.InitSize(body_count, Ragdoll::BodyLayer {});
	mMixStrength.InitSize(body_count, 1.0f);

	for (RagdollBehaviour* behaviour : mBehaviours) {
		behaviour->Init(*mpRig, body_count);
	}

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

	ClearMix();
}

void RagdollBehaviours::ClearMix()
{
	for (Ragdoll::BodyLayer& layer : mMixLayers) {
		layer = Ragdoll::BodyLayer {};
	}

	for (float32& strength : mMixStrength) {
		strength = 1.0f;
	}

	for (BehaviourGoal& goal : mMixGoals) {
		goal = BehaviourGoal {};
	}

	mMixBalance = eBehaviourBalance::Unset;
	mMixGravity = 1.0f;
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
	const GravityFrame frame = GravityFrame::FromPhysics();
	const Vec3f up = Vec3f::FromJolt(frame.Up);

	BehaviourContext& ctx = mContext;

	ctx.DeltaTime = delta_time;
	ctx.Time += delta_time;
	ctx.Up = up;
	ctx.Gravity = frame.Gravity;
	ctx.pBalance = &ragdoll.GetBalanceStatus();
	ctx.bBalancing = ragdoll.IsBalancing();

	const bool is_powered = !ragdoll.IsLimp() && ragdoll.GetBodyMode(0) == eRagdollBodyMode::Powered;
	const bool is_awake = ragdoll.IsAwake();

	bool any_contact = false;
	bool feet_supported = false;
	bool body_on_ground = false;

	for (uint32 i = 0; i < ctx.GetBodyCount(); i++) {
		Ragdoll::BodyState& sample = mSamples[i];
		BehaviourBody& body = ctx.Bodies[i];

		ragdoll.SampleBody(body_interface, i, sample);

		body.Origin = Vec3f::FromJolt(sample.Origin);
		body.Orientation = sample.Orientation;
		body.Center = Vec3f::FromJolt(sample.Center);
		body.Velocity = Vec3f::FromJolt(sample.Velocity);

		BodyGroundContact contact;
		body.bContact = is_powered && ragdoll.GetBodyGroundContact(i, contact);
		body.bGround = body.bContact && Ragdoll::IsSupportNormal(contact, frame.Up, scGroundNormalUp);

		any_contact = any_contact || body.bContact;
		const eRagdollBodyPart part = ragdoll.GetBodyPart(i);

		feet_supported = feet_supported || (ragdoll.IsFoot(i) && body.bGround);
		body_on_ground = body_on_ground ||
						 (body.bGround && (part == eRagdollBodyPart::Torso || part == eRagdollBodyPart::Head));
	}

	JPH::Vec3 center_of_mass;
	JPH::Vec3 velocity;

	if (ragdoll.GetMassAverage(mSamples, center_of_mass, velocity)) {
		ctx.CenterOfMass = Vec3f::FromJolt(center_of_mass);
		ctx.Velocity = Vec3f::FromJolt(velocity);
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

	const JPH::Quat delta = ctx.Bodies[mpRig->Chest].Orientation *
							ragdoll.GetRestOrientation(mpRig->Chest).Conjugated();
	const JPH::Vec3 up = Vec3f::ToJolt(ctx.Up);

	ctx.Forward = Vec3f::FromJolt((delta * mpRig->RestForward).Normalized());
	ctx.Right = Vec3f::FromJolt((delta * up.Cross(mpRig->RestForward)).NormalizedOr(JPH::Vec3::sAxisX()));
}

void RagdollBehaviours::PredictImpact()
{
	BehaviourContext& ctx = mContext;

	const BehaviourBody& chest = ctx.Bodies[mpRig->Chest];
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
			ctx.ImpactNormal = hit.Normal.SafeNormalize(ctx.Up);
			ctx.ImpactTime = time - scImpactStep * (1.0f - MathUtil::Clamp(fraction, 0.0f, 1.0f));

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
			Dispatch(RagdollEvent {
				.Type = eRagdollEvent::LostSupport,
				.Magnitude = -ctx.Velocity.Dot(ctx.Up),
			});
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

bool RagdollBehaviours::CanRun(const Ragdoll& ragdoll) const
{
	return IsValid() && ragdoll.IsCreated() && !ragdoll.IsLimp();
}

void RagdollBehaviours::Resolve(Ragdoll& ragdoll)
{
	SortByPriority();
	Mix();
	Apply(ragdoll);
}

void RagdollBehaviours::HandleEvent(Ragdoll& ragdoll, const RagdollEvent& event)
{
	if (!CanRun(ragdoll)) {
		return;
	}

	Sense(ragdoll, 0.0f);
	Dispatch(event);
	Resolve(ragdoll);
}

void RagdollBehaviours::Update(Ragdoll& ragdoll, float32 delta_time)
{
	if (!CanRun(ragdoll)) {
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

	Resolve(ragdoll);
}

void RagdollBehaviours::Mix()
{
	ClearMix();

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
	for (uint32 i = 0; i < mMixLayers.Size; i++) {
		const float32 mask = output.Mask[i] * weight;

		if (mask > 0.0f) {
			if (output.bAdditive) {
				Ragdoll::BodyLayer& layer = mMixLayers[i];
				layer.Offset = (layer.Offset * JPH::Quat::sIdentity().SLERP(output.Pose[i], mask)).Normalized();
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
	Ragdoll::BodyLayer& layer = mMixLayers[body];

	const float32 previous = layer.Weight;
	const float32 total = previous + (1.0f - previous) * weight;

	if (previous <= 0.0f) {
		layer.Pose = pose;
	}
	else {
		layer.Pose = layer.Pose.SLERP(pose, weight / total).Normalized();
	}

	// An override also fades out the additive offsets folded in below it
	layer.Weight = total;
	layer.Offset = layer.Offset.SLERP(JPH::Quat::sIdentity(), weight).Normalized();
}

void RagdollBehaviours::SolveArm(uint32 arm, const BehaviourGoal& goal, float32 weight)
{
	const BehaviourContext& ctx = mContext;
	const RagdollRigArm& chain = mpRig->Arms[arm];

	const Vec3f chest_up = ctx.GetChestUp();
	const Vec3f shoulder = ctx.Bodies[chain.Upper].Origin;
	const Vec3f hint = -chest_up * scElbowDown - ctx.Forward * scElbowBack + ctx.Right * (chain.SideSign * scElbowOut);

	const TwoBoneIKResult ik = SolveTwoBoneIK(shoulder, goal.Target, chain.UpperLength, chain.LowerLength, hint);

	const Vec3f upper_direction = (ik.Mid - shoulder).SafeNormalize(-chest_up);
	const Vec3f lower_direction = (ik.End - ik.Mid).SafeNormalize(upper_direction);

	Vec3f hinge = upper_direction.Cross(lower_direction);

	if (hinge.Length() < 1e-3f) {
		hinge = upper_direction.Cross(hint);
	}

	hinge = hinge.SafeNormalize(ctx.Right);

	JPH::Quat upper_world;
	JPH::Quat lower_world;
	ctx.pRagdoll->GetHingeChainRotations(chain.Upper, chain.Lower, Vec3f::ToJolt(upper_direction),
										 Vec3f::ToJolt(lower_direction), Vec3f::ToJolt(hinge), upper_world,
										 lower_world);

	FoldOverride(chain.Upper, RelativeRotation(ctx.Bodies[chain.Clavicle].Orientation, upper_world), weight);
	FoldOverride(chain.Lower, RelativeRotation(upper_world, lower_world), weight);

	if (weight >= mMixGoals[arm].Weight) {
		mMixGoals[arm] = BehaviourGoal { .Target = goal.Target, .Weight = weight };
	}
}

void RagdollBehaviours::Apply(Ragdoll& ragdoll) const { ragdoll.SetBodyLayers(mMixLayers, mMixGravity); }

int32 RagdollBehaviours::FindIndex(const char* name) const
{
	for (uint32 i = 0; i < scBehaviourCount; i++) {
		if (std::strcmp(mBehaviours[i]->GetName(), name) == 0) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

RagdollBehaviour* RagdollBehaviours::Find(const char* name)
{
	const int32 index = FindIndex(name);

	if (index < 0) {
		return nullptr;
	}

	return mBehaviours[index];
}

const RagdollBehaviour* RagdollBehaviours::Find(const char* name) const
{
	const int32 index = FindIndex(name);

	if (index < 0) {
		return nullptr;
	}

	return mBehaviours[index];
}

bool RagdollBehaviours::Trigger(Ragdoll& ragdoll, const char* name)
{
	RagdollBehaviour* behaviour = Find(name);

	if (behaviour == nullptr || !CanRun(ragdoll)) {
		return false;
	}

	Sense(ragdoll, 0.0f);
	behaviour->Start(mContext);
	Resolve(ragdoll);

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
	if (!IsValid() || mContext.Bodies.IsEmpty()) {
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

		const Vec3f shoulder = mContext.Bodies[mpRig->Arms[arm].Upper].Origin;
		const uint8 green = static_cast<uint8>(80.0f + 175.0f * MathUtil::Clamp(goal.Weight, 0.0f, 1.0f));

		renderer::gDebugDraw->Line(shoulder, goal.Target, Color::FromRGBA(40, green, 40, 255));
		DrawCross(goal.Target, Color::FromRGBA(40, green, 40, 255));
	}
}

} // namespace fx::physics
