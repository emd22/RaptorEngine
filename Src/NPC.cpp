#include "NPC.hpp"

#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Quat.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>
#include <algorithm>
#include <cstring>

namespace fx {

namespace {

constexpr float32 scWoundCooldown = 0.25f;

float32 GetContactWoundSize(const physics::RagdollContact& contact)
{
	constexpr float32 scMinSize = 0.15f;
	constexpr float32 scMaxSize = 0.4f;
	constexpr float32 scMaxMomentum = 150.0f;

	const float32 strength = MathUtil::Clamp(contact.OtherMass * contact.Speed / scMaxMomentum, 0.0f, 1.0f);

	return scMinSize + (scMaxSize - scMinSize) * strength;
}

} // namespace

/////////////////////////////////////
// Ragdoll actor
/////////////////////////////////////

bool RagdollEntity::Clone(const Object* model, const String& name)
{
	Destroy();

	if (model == nullptr) {
		return false;
	}

	Object* root = model->CloneWithOwnSkeleton(std::string(name.CStr()));

	DynArray<Object*> parts;
	CollectObjectTree(root, parts);

	const auto skinned_part = std::find_if(parts.begin(), parts.end(),
										   [](const Object* part) { return part->pSkeleton.IsValid(); });

	if (skinned_part == parts.end()) {
		LogWarning(LC_ASSET, "'{}': the model has no skeleton", name);
		DestroyObjectTree(root->ID);
		return false;
	}

	Object* skinned = *skinned_part;

	root->SetShadowCaster(true);
	root->SetProbeVisible(false);

	RootID = root->ID;
	SkinnedID = skinned->ID;
	pSkeleton = skinned->pSkeleton;

	return true;
}

bool RagdollEntity::CreateRagdoll()
{
	Object* skinned = GetSkinned();
	const physics::RagdollDef* ragdoll_def = physics::GetHumanoidRagdollDef();

	if (skinned == nullptr || ragdoll_def == nullptr) {
		return false;
	}

	return Ragdoll.Create(pSkeleton, skinned->GetWorldMatrix(), *ragdoll_def);
}

void RagdollEntity::Destroy()
{
	if (pSkeleton.IsValid() && gDecalManager != nullptr) {
		gDecalManager->RemoveSkinnedDecals(&(*pSkeleton));
	}

	Ragdoll.Destroy();

	if (!RootID.IsInvalid()) {
		DestroyObjectTree(RootID);
	}

	RootID = ObjectID::scNull;
	SkinnedID = ObjectID::scNull;
	pSkeleton = nullptr;
	BloodCooldown = 0.0f;
}

Object* RagdollEntity::GetRoot() const { return gObjectManager->GetObject(RootID); }

Object* RagdollEntity::GetSkinned() const { return gObjectManager->GetObject(SkinnedID); }

void RagdollEntity::SetCullable(bool cullable)
{
	Object* root = GetRoot();

	if (root == nullptr) {
		return;
	}

	DynArray<Object*> parts;
	CollectObjectTree(root, parts);

	for (Object* part : parts) {
		part->SetCullable(cullable);
	}
}

bool RagdollEntity::AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size)
{
	Object* skinned = GetSkinned();
	uint32 bone_index = 0;

	if (skinned == nullptr || !pSkeleton.IsValid() || !Ragdoll.FindBoneForBody(body_id, bone_index) ||
		bone_index >= pSkeleton->SkinningMatrices.Size) {
		return false;
	}

	const Mat4f rest_to_world = pSkeleton->SkinningMatrices[bone_index] * skinned->GetWorldMatrix();

	gDecalManager->AddSkinnedBloodSplat(&(*pSkeleton), rest_to_world, point, direction, size);

	return true;
}

void RagdollEntity::AddContactWound(const physics::RagdollContact& contact)
{
	if (BloodCooldown > 0.0f) {
		return;
	}

	if (AddWound(contact.Body, contact.Point, contact.Normal, GetContactWoundSize(contact))) {
		BloodCooldown = scWoundCooldown;
	}
}

void RagdollEntity::TickCooldown(float32 delta_time) { BloodCooldown = std::max(BloodCooldown - delta_time, 0.0f); }

/////////////////////////////////////
// NPC
/////////////////////////////////////

bool NPC::Create(const Object* model, const NPCDesc& desc, NPCManager& manager)
{
	Destroy();

	mpManager = &manager;
	mpTuning = &manager.Tuning;

	if (!mActor.Clone(model, desc.Name)) {
		return false;
	}

	Object* root = mActor.GetRoot();

	root->SetPosition(desc.Position);
	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, desc.Yaw));

	mName = desc.Name;

	MaxHealth = desc.MaxHealth;
	RagdollHealth = desc.RagdollHealth;
	mHealth = desc.MaxHealth;
	Importance = desc.Importance;
	mState = eState::Animated;
	mbDying = false;
	mbTeleported = true;
	mPendingImpactDamage = 0.0f;
	mPushCooldown = 0.0f;
	mLocomotionSpeed = 0.0f;
	mYaw = desc.Yaw;

	mpIdleAnimation = FindAnimation(desc.IdleAnimation);
	mpWalkAnimation = FindAnimation(desc.WalkAnimation);
	mpRunAnimation = FindAnimation(desc.RunAnimation);
	mWalkAnimationSpeed = std::max(desc.WalkAnimationSpeed, 0.1f);
	mRunAnimationSpeed = std::max(desc.RunAnimationSpeed, 0.1f);

	if (mpWalkAnimation == nullptr) {
		mpWalkAnimation = mpIdleAnimation;
	}

	if (mpIdleAnimation != nullptr) {
		mActor.pSkeleton->SetRestAnimation(mpIdleAnimation);
	}

	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!mActor.CreateRagdoll()) {
		LogWarning(LC_PHYSICS, "NPC '{}': no ragdoll could be built for the skeleton, it will stay animated",
				   desc.Name);
	}

	if (ragdoll.IsCreated()) {
		mBehaviours.Init(ragdoll);
	}

	// Kinematic until something hits it, so the animated feet don't drag on the floor
	ragdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	ragdoll.SetRootAssist(mpTuning->RootAssist);
	ragdoll.Drive(mActor.GetSkinned()->GetWorldMatrix(), 0.0f, true);
	mAssistFade = 1.0f;
	mPoweredTime = 0.0f;
	mPhysicsBlend = 1.0f;

	uint32 serial = 0;

	if (ragdoll.IsCreated()) {
		serial = ragdoll.GetSerial();
	}

	mMover.Create(manager.GetCharacterConfig(), desc.Position, serial);
	mBrain.Init(desc.Position, (serial + 1) * 2654435761u);
	mBrain.WanderRadius = mpTuning->WanderRadius;

	gWorld->AttachLoaded(root);

	return true;
}

const Animation* NPC::FindAnimation(const String& name) const
{
	if (name.IsEmpty() || !mActor.pSkeleton.IsValid()) {
		return nullptr;
	}

	const Animation* anim = mActor.pSkeleton->FindAnimation(name);

	if (anim == nullptr) {
		LogWarning(LC_ASSET, "NPC '{}': could not find animation '{}'", mName, name);
	}

	return anim;
}

void NPC::Destroy()
{
	mBehaviours.Reset();
	mMover.Destroy();
	mActor.Destroy();

	mState = eState::Animated;
	mbDying = false;
}

void NPC::Update(float32 delta_time, bool draw_debug)
{
	if (!IsCreated()) {
		return;
	}

	mActor.TickCooldown(delta_time);
	mPushCooldown = std::max(mPushCooldown - delta_time, 0.0f);
	mBrain.WanderRadius = mpTuning->WanderRadius;

	if (mPendingImpactDamage > 0.0f) {
		Damage(mPendingImpactDamage);
		mPendingImpactDamage = 0.0f;
	}

	if (mState == eState::Ragdoll) {
		mActor.Ragdoll.WriteToSkeleton();
	}
	else {
		UpdateActive(delta_time);
	}

	if (draw_debug) {
		mActor.Ragdoll.DebugDraw();
		mBehaviours.DebugDraw();
		DrawDebugHealth();
		DrawDebugBrain();
	}
}

void NPC::UpdateActive(float32 delta_time)
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;
	Object* skinned = mActor.GetSkinned();

	if (mState == eState::Animated && skinned != nullptr) {
		UpdateLocomotion(delta_time);

		float32 pose_time = delta_time;

		// Hold the animation still while physics leads, so the ragdoll is not chasing a moving target
		if (ragdoll.IsBalancing() || mbDying) {
			pose_time = 0.0f;
		}

		mActor.pSkeleton->AdvancePose(pose_time);
	}

	UpdateBehaviours(delta_time);

	if (mState == eState::Animated && skinned != nullptr) {
		UpdateBalance(delta_time);
		UpdatePhysicsBlend(delta_time);
	}

	if (mState != eState::Ragdoll) {
		ApplyStrengths();
	}

	if (mState == eState::Animated && skinned != nullptr) {
		ragdoll.Drive(skinned->GetWorldMatrix(), delta_time, mbTeleported);
		mbTeleported = false;
	}

	ragdoll.WriteToSkeleton();
}

void NPC::UpdateLocomotion(float32 delta_time)
{
	Object* root = mActor.GetRoot();

	if (root == nullptr || delta_time <= 0.0f) {
		return;
	}

	// The brain waits while physics leads, so a reaction starts once the NPC is back on its feet
	if (!mMover.IsEnabled()) {
		UpdateLocomotionAnimation(delta_time, 0.0f);
		return;
	}

	const Vec3f previous_position = root->GetPosition();

	NPCBrainOutput out;
	mBrain.Update(delta_time, NPCBrainInput { .Position = previous_position, .Yaw = mYaw }, out);

	mMover.Update(delta_time, out.Velocity);
	mYaw = out.Yaw;

	const Vec3f position = mMover.GetPosition();
	const Vec3f moved = position - previous_position;

	root->SetPosition(position);
	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, mYaw));

	const float32 speed = Vec3f(moved.X, 0.0f, moved.Z).Length() / delta_time;

	UpdateLocomotionAnimation(delta_time, speed);
}

void NPC::UpdateLocomotionAnimation(float32 delta_time, float32 speed)
{
	constexpr float32 scSpeedSmoothing = 8.0f;
	constexpr float32 scIdleSpeed = 0.15f;
	constexpr float32 scBlendTime = 0.25f;

	Skeleton& skeleton = *mActor.pSkeleton;

	mLocomotionSpeed += (speed - mLocomotionSpeed) * std::min(delta_time * scSpeedSmoothing, 1.0f);

	const Animation* wanted = mpIdleAnimation;
	float32 rate = 1.0f;

	if (mLocomotionSpeed >= scIdleSpeed && mpWalkAnimation != nullptr) {
		const bool is_running = mLocomotionSpeed > 0.5f * (mWalkAnimationSpeed + mRunAnimationSpeed);

		if (is_running && mpRunAnimation != nullptr) {
			wanted = mpRunAnimation;
			rate = mLocomotionSpeed / mRunAnimationSpeed;
		}
		else {
			wanted = mpWalkAnimation;
			rate = mLocomotionSpeed / mWalkAnimationSpeed;
		}
	}

	if (wanted == nullptr) {
		return;
	}

	if (skeleton.RestPlayback.pAnimation != wanted) {
		skeleton.SetRestAnimation(wanted, rate, scBlendTime);
		return;
	}

	skeleton.RestPlayback.Speed = rate;
}

void NPC::EnableMover()
{
	Object* root = mActor.GetRoot();

	if (root == nullptr || !mMover.IsCreated() || mState != eState::Animated || mbDying) {
		return;
	}

	mMover.Teleport(root->GetPosition());
	mMover.SetEnabled(true);
}

void NPC::PlaceRootUnderPelvis()
{
	constexpr float32 scGroundProbe = 2.0f;

	physics::Ragdoll& ragdoll = mActor.Ragdoll;
	Object* root = mActor.GetRoot();

	if (root == nullptr || !ragdoll.IsCreated()) {
		return;
	}

	const Vec3f pelvis = ragdoll.GetBodyOrigin(0);
	const Vec3f root_position = root->GetPosition();
	Vec3f target = ragdoll.GetBodyTargetOrigin(0);

	// Balancing lets the body turn as it stumbles, so turn the root to match or the animation twists it back
	const Vec3f facing = ragdoll.GetPelvisHeading(false);
	const Vec3f animated_facing = ragdoll.GetPelvisHeading(true);

	if (facing.Length() > 0.0f && animated_facing.Length() > 0.0f) {
		const float32 turn = NPCBrain::WrapAngle(NPCBrain::DirectionToYaw(facing) -
												 NPCBrain::DirectionToYaw(animated_facing));
		const float32 cos_turn = std::cos(turn);
		const float32 sin_turn = std::sin(turn);
		const Vec3f arm = target - root_position;

		target = root_position + Vec3f(arm.X * cos_turn + arm.Z * sin_turn, arm.Y, arm.Z * cos_turn - arm.X * sin_turn);

		mYaw = NPCBrain::WrapAngle(mYaw + turn);
		root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, mYaw));
	}

	const Vec3f offset = pelvis - target;

	Vec3f position = root_position + Vec3f(offset.X, 0.0f, offset.Z);

	const physics::RayResult ground = gPhysics->pBackend->RaycastStatic(Vec3f(position.X, pelvis.Y, position.Z),
																		Vec3f(0.0f, -scGroundProbe, 0.0f));

	if (ground.bHit) {
		position.Y = ground.Point.Y;
	}

	root->SetPosition(position);
}

void NPC::UpdateBehaviours(float32 delta_time)
{
	mBehaviours.Update(mActor.Ragdoll, delta_time);

	if (mState == eState::Animated && IsPowered() && mBehaviours.HadEvent(physics::eRagdollEvent::LostSupport)) {
		EnterFallen();
	}

	if (mbDying && (mBehaviours.IsDeathComplete() || !mBehaviours.IsDying())) {
		EnterRagdoll();
	}
}

void NPC::DrawDebugHealth() const
{
	constexpr float32 scBarHeight = 2.2f;
	constexpr float32 scBarWidth = 1.0f;

	const Object* root = mActor.GetRoot();

	if (root == nullptr || MaxHealth <= 0.0f) {
		return;
	}

	const Vec3f right = gWorld->Player.pCamera->GetRightVector();
	const Vec3f left_end = root->GetPosition() + Vec3f(0.0f, scBarHeight, 0.0f) - right * (scBarWidth * 0.5f);

	const float32 fraction = MathUtil::Clamp(mHealth / MaxHealth, 0.0f, 1.0f);

	renderer::gDebugDraw->Line(left_end, left_end + right * scBarWidth, Color::FromRGBA(60, 60, 60, 255));

	Color fill = Color::FromRGBA(static_cast<uint8>(255.0f * (1.0f - fraction)), static_cast<uint8>(255.0f * fraction),
								 40, 255);

	if (mState == eState::Ragdoll) {
		fill = Color::FromRGBA(150, 150, 150, 255);
	}
	else if (mbDying) {
		fill = Color::FromRGBA(120, 30, 30, 255);
	}

	renderer::gDebugDraw->Line(left_end, left_end + right * (scBarWidth * fraction), fill);
}

void NPC::DrawDebugBrain() const
{
	const Object* root = mActor.GetRoot();
	const eNPCBrainState state = mBrain.GetState();

	if (root == nullptr || mState != eState::Animated) {
		return;
	}

	const Vec3f lift(0.0f, 0.05f, 0.0f);
	const Vec3f position = root->GetPosition() + lift;

	renderer::gDebugDraw->Line(position, position + NPCBrain::YawToDirection(mYaw) * 0.6f,
							   Color::FromRGBA(80, 160, 255, 255));

	if (state == eNPCBrainState::WalkTo || state == eNPCBrainState::Flee) {
		Color color = Color::FromRGBA(240, 220, 60, 255);

		if (state == eNPCBrainState::Flee) {
			color = Color::FromRGBA(255, 70, 40, 255);
		}

		renderer::gDebugDraw->Line(position, mBrain.GetTarget() + lift, color);
	}
}

void NPC::Damage(float32 amount) { ApplyDamage(amount, -1); }

void NPC::ApplyDamage(float32 amount, int32 body)
{
	if (!CanTakeDamage()) {
		return;
	}

	mHealth = std::max(mHealth - amount, 0.0f);

	if (mHealth <= RagdollHealth) {
		Kill(body);
	}
}

NPC::eBodyPart NPC::GetBodyPart(JPH::BodyID body_id) const
{
	const int32 body_index = mActor.Ragdoll.FindBodyIndex(body_id);

	if (body_index < 0) {
		return eBodyPart::Torso;
	}

	return mActor.Ragdoll.GetBodyPart(static_cast<uint32>(body_index));
}

float32 NPC::GetDamageScale(JPH::BodyID body_id) const
{
	switch (GetBodyPart(body_id)) {
	case eBodyPart::Head:
		return HeadDamageScale;
	case eBodyPart::Arm:
	case eBodyPart::Leg:
		return LimbDamageScale;
	case eBodyPart::Torso:
		return TorsoDamageScale;
	}

	return TorsoDamageScale;
}

void NPC::Damage(float32 amount, JPH::BodyID body_id)
{
	if (!CanTakeDamage()) {
		return;
	}

	const int32 body = mActor.Ragdoll.FindBodyIndex(body_id);

	if (GetBodyPart(body_id) == eBodyPart::Head && amount >= HeadKillDamage) {
		ApplyDamage(mHealth, body);
		return;
	}

	ApplyDamage(amount * GetDamageScale(body_id), body);
}

void NPC::Kill(int32 body)
{
	if (!CanTakeDamage()) {
		return;
	}

	mHealth = std::min(mHealth, RagdollHealth);

	if (!mActor.Ragdoll.IsCreated() || !mBehaviours.IsValid()) {
		EnterRagdoll();
		return;
	}

	// Over the active budget there is no dying behaviour, the NPC just drops
	if (!WakePhysics()) {
		EnterRagdoll();
		return;
	}

	mActor.SetCullable(false);

	mbDying = true;
	mBehaviours.SetAlive(false);
	mBehaviours.HandleEvent(mActor.Ragdoll,
							physics::RagdollEvent { .Type = physics::eRagdollEvent::Died, .Body = body });

	if (!mBehaviours.IsDying()) {
		EnterRagdoll();
		return;
	}

	ApplyStrengths();
}

void NPC::EnterRagdoll()
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!IsCreated() || mState == eState::Ragdoll || !ragdoll.IsCreated()) {
		return;
	}

	mBehaviours.Reset();
	ragdoll.ClearBodyLayers();

	Object* skinned = mActor.GetSkinned();

	if (mActor.GetRoot() == nullptr || skinned == nullptr) {
		return;
	}

	// The object origin stays put while the ragdoll moves, so culling would drop it
	mActor.SetCullable(false);

	mMover.SetEnabled(false);
	ragdoll.GoLimp(skinned->GetWorldMatrix());
	mState = eState::Ragdoll;
}

void NPC::EnterFallen()
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (mState != eState::Animated || !ragdoll.IsCreated()) {
		return;
	}

	ragdoll.SetBalancing(false);
	ragdoll.SetRootAssist(0.0f);
	mMover.SetEnabled(false);

	if (mBehaviours.IsValid()) {
		ragdoll.SetAllBodyModes(physics::eRagdollBodyMode::Powered);
	}
	else {
		ragdoll.SetAllBodyModes(physics::eRagdollBodyMode::Limp);
	}

	ragdoll.SetAllBodyBlends(1.0f);

	if (mActor.pSkeleton.IsValid()) {
		mActor.pSkeleton->SetExternalPose(true);
	}

	mActor.SetCullable(false);

	mState = eState::Fallen;

	const physics::BehaviourContext& ctx = mBehaviours.GetContext();

	mBehaviours.SetFreePose(true);
	mBehaviours.Trigger(ragdoll, "fallen");
	mBehaviours.HandleEvent(ragdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::LostSupport,
															 .Magnitude = -ctx.Velocity.Dot(ctx.Up) });
	ApplyStrengths();
}

void NPC::UpdateBalance(float32 delta_time)
{
	constexpr float32 scAssistFadeTime = 0.5f;

	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (mState != eState::Animated) {
		return;
	}

	if (!mpTuning->bBalance) {
		ragdoll.SetBalancing(false);
		mAssistFade = 1.0f;
		return;
	}

	const bool wants_balance = (mBehaviours.GetBalance() == physics::eBehaviourBalance::On);

	if (!ragdoll.IsBalancing()) {
		mAssistFade = std::min(mAssistFade + delta_time / scAssistFadeTime, 1.0f);

		if (wants_balance) {
			ActivateBalance();
		}

		return;
	}

	if (ragdoll.GetBalanceStatus().State == physics::eBalanceState::Fallen) {
		// Dying NPCs skip the fallen behaviours and keep tracking their held pose while their strength fades
		if (mbDying) {
			ReturnToAnimation();
		}
		else {
			EnterFallen();
		}

		return;
	}

	if (!wants_balance) {
		ReturnToAnimation();
	}
}

void NPC::ActivateBalance()
{
	if (!mpTuning->bBalance || mState != eState::Animated || !mActor.Ragdoll.IsCreated()) {
		return;
	}

	mActor.Ragdoll.SetBalancing(true);

	if (mActor.Ragdoll.IsBalancing()) {
		mAssistFade = 0.0f;
	}
}

void NPC::ReturnToAnimation()
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	// Move the animated root to where balancing left the pelvis, so the animation picks up from there
	PlaceRootUnderPelvis();

	ragdoll.RebaseTargets();
	ragdoll.SetBalancing(false);
	mAssistFade = 0.0f;
}

bool NPC::IsPowered() const
{
	const physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!ragdoll.IsCreated() || ragdoll.GetBodyCount() == 0) {
		return false;
	}

	return ragdoll.GetBodyMode(0) == physics::eRagdollBodyMode::Powered;
}

void NPC::SetPowered(bool powered)
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!ragdoll.IsCreated() || IsPowered() == powered) {
		return;
	}

	if (powered) {
		ragdoll.SetAllBodyModes(physics::eRagdollBodyMode::Powered);
	}
	else {
		ragdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	}

	mPhysicsBlend = 1.0f;
	ragdoll.SetAllBodyBlends(mPhysicsBlend);

	if (powered) {
		mMover.SetEnabled(false);
	}
	else {
		EnableMover();
	}
}

bool NPC::WakePhysics()
{
	if (!mActor.Ragdoll.IsCreated()) {
		return false;
	}

	if (!IsPowered() && mpManager != nullptr && !mpManager->RequestActive(*this)) {
		return false;
	}

	SetPowered(true);
	mPoweredTime = 0.0f;

	return true;
}

bool NPC::IsPhysicsActive() const
{
	if (!mActor.Ragdoll.IsCreated() || mState == eState::Ragdoll) {
		return false;
	}

	return IsPowered() || mState == eState::Fallen || mbDying;
}

bool NPC::CanDeactivate() const { return mState == eState::Animated && !mbDying && IsPowered(); }

void NPC::Deactivate()
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!CanDeactivate()) {
		return;
	}

	if (ragdoll.IsBalancing()) {
		ReturnToAnimation();
	}

	mBehaviours.Reset();
	ragdoll.ClearBodyLayers();
	ragdoll.RebaseTargets();

	mAssistFade = 1.0f;
	SetPowered(false);
	ApplyStrengths();
}

void NPC::UpdatePhysicsBlend(float32 delta_time)
{
	constexpr float32 scMinPoweredTime = 1.0f;
	constexpr float32 scBlendOutTime = 0.5f;

	if (mState != eState::Animated || !IsPowered()) {
		return;
	}

	mPoweredTime += delta_time;

	const bool is_settled = !mActor.Ragdoll.IsBalancing() && !mbDying && mPoweredTime >= scMinPoweredTime &&
							mAssistFade >= 1.0f && mBehaviours.IsIdle();

	const float32 blend_step = delta_time / scBlendOutTime;

	if (is_settled) {
		mPhysicsBlend = std::max(mPhysicsBlend - blend_step, 0.0f);
	}
	else {
		mPhysicsBlend = std::min(mPhysicsBlend + blend_step, 1.0f);
	}

	if (mPhysicsBlend <= 0.0f) {
		SetPowered(false);
		return;
	}

	// Eased so the hand-back to the animation starts and lands gently instead of sliding at a constant rate
	mActor.Ragdoll.SetAllBodyBlends(MathUtil::SmoothStep(mPhysicsBlend));
}

void NPC::ApplyStrengths()
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!ragdoll.IsCreated()) {
		return;
	}

	for (uint32 i = 0; i < ragdoll.GetBodyCount(); i++) {
		ragdoll.SetBodyStrength(i, mpTuning->Strength * mBehaviours.GetStrength(i));
	}

	float32 assist = 0.0f;

	if (mState == eState::Animated && !mbDying) {
		assist = mpTuning->RootAssist * std::min(mBehaviours.GetStrength(0), 1.0f) * mAssistFade;
	}

	ragdoll.SetRootAssist(assist);
	ragdoll.SetBalanceAssist(mpTuning->BalanceAssist);
}

void NPC::ApplyBehaviourOutput()
{
	// NPC::Update runs after the physics step, so push the new balance request and strengths now or the step that
	// receives the impulse would still run on the old ones
	UpdateBalance(0.0f);
	ApplyStrengths();
}

void NPC::OnHit(const NPCHit& hit)
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!IsCreated()) {
		return;
	}

	if (hit.Damage > 0.0f) {
		Damage(hit.Damage, hit.Body);
	}

	const int32 body = ragdoll.FindBodyIndex(hit.Body);

	if (mState == eState::Ragdoll || body < 0) {
		ragdoll.AddImpulse(hit.Body, hit.Impulse, hit.Point);
		return;
	}

	if (mState == eState::Animated && !mbDying && gWorld != nullptr) {
		mBrain.React(gWorld->Player.Position, hit.Damage > 0.0f);
	}

	// Over the active budget the NPC takes the damage but stays animated
	if (!WakePhysics()) {
		return;
	}

	const float32 length = hit.Impulse.Length();
	Vec3f direction = Vec3f::sZero;

	if (length > 1e-5f) {
		direction = hit.Impulse / length;
	}

	mBehaviours.HandleEvent(ragdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Hit,
															 .Body = body,
															 .Point = hit.Point,
															 .Direction = direction,
															 .Magnitude = length,
															 .Damage = hit.Damage });
	ApplyBehaviourOutput();

	if (length <= 1e-5f) {
		return;
	}

	if (mState != eState::Animated) {
		ragdoll.AddImpulse(hit.Body, hit.Impulse, hit.Point);
		return;
	}

	// Shove first, so the speed it tops up to is not already counted from the struck limbs' push
	Shove(hit.Impulse * mpTuning->HitShoveScale, mpTuning->MinShoveSpeed);

	PushHitChain(body, hit.Impulse, hit.Point);
}

void NPC::PushHitChain(int32 body, const Vec3f impulse, const Vec3f point)
{
	constexpr uint32 scChainLength = 3;
	constexpr float32 scChainFalloff[scChainLength] = { 1.0f, 0.6f, 0.3f };
	constexpr float32 scMaxSpeed = 2.0f;

	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	int32 chain[scChainLength];
	uint32 chain_length = 0;
	float32 weighted_mass = 0.0f;

	for (int32 i = body; i >= 0 && chain_length < scChainLength; i = ragdoll.GetParentBody(i)) {
		chain[chain_length] = i;
		weighted_mass += scChainFalloff[chain_length] * ragdoll.GetBodyMass(i);
		chain_length++;
	}

	const float32 length = impulse.Length();

	if (weighted_mass <= 0.0f || length <= 1e-5f) {
		return;
	}

	// The struck body leads and its parents follow, so a light hand is not flicked away and snapped straight back by
	// its own muscles. Only the struck body takes the hit off centre and spins.
	const float32 speed = std::min(length / weighted_mass, scMaxSpeed);
	const Vec3f velocity = impulse * (speed / length);

	for (uint32 i = 0; i < chain_length; i++) {
		const uint32 index = static_cast<uint32>(chain[i]);
		const Vec3f chain_impulse = velocity * (scChainFalloff[i] * ragdoll.GetBodyMass(index));

		if (i == 0) {
			ragdoll.AddImpulse(ragdoll.GetBodyID(index), chain_impulse, point);
		}
		else {
			ragdoll.AddImpulse(ragdoll.GetBodyID(index), chain_impulse, ragdoll.GetBodyPosition(index));
		}
	}
}

void NPC::Shove(const Vec3f impulse, float32 min_speed)
{
	physics::Ragdoll& ragdoll = mActor.Ragdoll;
	const physics::RagdollRig& rig = ragdoll.GetRig();

	const Vec3f horizontal(impulse.X, 0.0f, impulse.Z);
	const float32 length = horizontal.Length();
	const float32 total_mass = ragdoll.GetTotalMass();

	if (!rig.bCanBalance || length <= 1e-5f || total_mass <= 0.0f) {
		return;
	}

	float32 moved_mass = 0.0f;

	for (uint32 i = 0; i < ragdoll.GetBodyCount(); i++) {
		if (!ragdoll.IsFoot(i)) {
			moved_mass += ragdoll.GetBodyMass(i);
		}
	}

	if (moved_mass <= 0.0f) {
		return;
	}

	const float32 speed = MathUtil::Clamp(length / total_mass, min_speed, mpTuning->MaxShoveSpeed);
	const Vec3f direction = horizontal / length;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	float32 momentum = 0.0f;

	for (uint32 i = 0; i < ragdoll.GetBodyCount(); i++) {
		if (!ragdoll.IsFoot(i)) {
			const Vec3f body_velocity = Vec3f(body_interface.GetLinearVelocity(ragdoll.GetBodyID(i)));
			momentum += ragdoll.GetBodyMass(i) * body_velocity.Dot(direction);
		}
	}

	// A hit while the body is still moving from the last one only tops its speed back up, so a burst knocks harder
	// than one shot without each hit adding a full shove
	const float32 target_speed = speed * total_mass / moved_mass;
	const float32 current_speed = std::max(momentum / moved_mass, 0.0f);
	const float32 added_speed = std::max(target_speed - current_speed, target_speed * mpTuning->RepeatShoveFraction);

	// Everything above the feet moves together, so the body tips over its planted feet without bending at the hips
	const Vec3f velocity = direction * added_speed;

	for (uint32 i = 0; i < ragdoll.GetBodyCount(); i++) {
		if (!ragdoll.IsFoot(i)) {
			ragdoll.AddImpulse(ragdoll.GetBodyID(i), velocity * ragdoll.GetBodyMass(i), ragdoll.GetBodyPosition(i));
		}
	}

	// Balance lets the body keep drifting with the shove for a moment, so it steps with it rather than stopping dead
	Vec3f stagger = ragdoll.GetStaggerVelocity() + horizontal * (speed / length);
	const float32 stagger_speed = stagger.Length();

	if (stagger_speed > mpTuning->MaxStaggerSpeed) {
		stagger = stagger * (mpTuning->MaxStaggerSpeed / stagger_speed);
	}

	ragdoll.SetStaggerVelocity(stagger);
}

bool NPC::TriggerBehaviour(const char* name)
{
	if (!IsCreated() || mState == eState::Ragdoll || !mBehaviours.IsValid() || name == nullptr) {
		return false;
	}

	if (std::strcmp(name, "dying") == 0) {
		Kill();
		return mbDying || mState == eState::Ragdoll;
	}

	const physics::RagdollBehaviour* behaviour = mBehaviours.Find(name);

	if (behaviour == nullptr || !behaviour->IsEnabled()) {
		return false;
	}

	if (mState == eState::Animated) {
		if (!WakePhysics()) {
			return false;
		}

		if (behaviour->NeedsFreePose()) {
			EnterFallen();
		}
	}

	const bool started = mBehaviours.Trigger(mActor.Ragdoll, name);

	ApplyBehaviourOutput();

	return started;
}

bool NPC::StopBehaviour(const char* name)
{
	if (name == nullptr) {
		return false;
	}

	return mBehaviours.Stop(name);
}

float32 NPC::GetContactDamage(const physics::RagdollContact& contact) const
{
	const float32 excess = contact.Speed - mpTuning->SafeImpactSpeed;

	if (excess <= 0.0f) {
		return 0.0f;
	}

	const float32 own_mass = mActor.Ragdoll.GetTotalMass();
	float32 mass = own_mass;

	// Reduced mass of the pair, so a light prop hurts less than a wall at the same speed
	if (contact.OtherMass > 0.0f) {
		mass = (own_mass * contact.OtherMass) / (own_mass + contact.OtherMass);
	}

	return mpTuning->ImpactDamageScale * mass * excess * GetDamageScale(contact.Body);
}

void NPC::OnContact(const physics::RagdollContact& contact)
{
	constexpr float32 scMaxKnockMass = 80.0f;

	if (!IsCreated()) {
		return;
	}

	const bool is_struck = contact.IsStruck(mpTuning->MinImpactSpeed);

	if (is_struck) {
		mActor.AddContactWound(contact);
	}

	if (mState == eState::Ragdoll) {
		return;
	}

	mPendingImpactDamage = std::max(mPendingImpactDamage, GetContactDamage(contact));

	if (is_struck) {
		float32 knock_mass = scMaxKnockMass;

		if (contact.OtherMass > 0.0f) {
			knock_mass = std::min(contact.OtherMass, scMaxKnockMass);
		}

		OnHit(NPCHit {
			.Body = contact.Body, .Point = contact.Point, .Impulse = contact.Normal * (knock_mass * contact.Speed) });
		return;
	}

	if (!contact.bOtherStatic || contact.Speed < mpTuning->MinImpactSpeed) {
		return;
	}

	mBehaviours.HandleEvent(mActor.Ragdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Impact,
																	.Body = mActor.Ragdoll.FindBodyIndex(contact.Body),
																	.Point = contact.Point,
																	.Direction = contact.Normal,
																	.Magnitude = contact.Speed });
}

void NPC::OnPush(const NPCPush& push)
{
	constexpr float32 scPushCooldown = 0.5f;

	physics::Ragdoll& ragdoll = mActor.Ragdoll;

	if (!CanTakeDamage() || mState != eState::Animated || mPushCooldown > 0.0f) {
		return;
	}

	mPushCooldown = scPushCooldown;
	mBrain.React(push.Source, false);

	const float32 length = push.Impulse.Length();

	if (length <= 1e-5f || !mBehaviours.IsValid() || !WakePhysics()) {
		return;
	}

	// Pushed at the chest, like a shove, rather than wherever the two capsules touched
	const uint32 chest = ragdoll.GetRig().Chest;
	const Vec3f point = ragdoll.GetBodyPosition(chest);

	mBehaviours.HandleEvent(ragdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Push,
															 .Body = static_cast<int32>(chest),
															 .Point = point,
															 .Direction = push.Impulse / length,
															 .Magnitude = length });
	ApplyBehaviourOutput();

	ragdoll.AddImpulse(ragdoll.GetBodyID(chest), push.Impulse, point);

	Shove(push.Impulse, 0.0f);
}

void NPC::AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size)
{
	mActor.AddWound(body_id, point, direction, size);
}

void NPC::SetPosition(const Vec3f position)
{
	Object* root = mActor.GetRoot();

	if (root == nullptr || mState != eState::Animated) {
		return;
	}

	root->SetPosition(position);
	mMover.Teleport(position);
	mBrain.SetHome(position);
	mbTeleported = true;
}

void NPC::SetYaw(float32 yaw)
{
	Object* root = mActor.GetRoot();

	if (root == nullptr || mState != eState::Animated) {
		return;
	}

	mYaw = yaw;
	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, yaw));
	mbTeleported = true;
}

void NPC::SetIdleAnimation(const String& name, float32 speed, float32 blend_time)
{
	const Ref<Skeleton>& skeleton = mActor.pSkeleton;

	if (!skeleton.IsValid()) {
		return;
	}

	const Animation* anim = skeleton->FindAnimation(name);

	if (anim == nullptr) {
		LogWarning(LC_ASSET, "NPC: could not find idle animation '{}'", name);
		return;
	}

	if (mpWalkAnimation == mpIdleAnimation) {
		mpWalkAnimation = anim;
	}

	mpIdleAnimation = anim;
	skeleton->SetRestAnimation(anim, speed, blend_time);
}

bool NPC::PlayAnimation(const String& name, eAnimationEnd on_end, float32 speed, float32 blend_time)
{
	if (!mActor.pSkeleton.IsValid() || mState == eState::Ragdoll) {
		return false;
	}

	return mActor.pSkeleton->PushAnimation(name, on_end, speed, blend_time);
}

/////////////////////////////////////
// NPC manager
/////////////////////////////////////

NPC* NPCManager::Spawn(const Object* model, const NPCDesc& desc)
{
	std::unique_ptr<NPC> npc = std::make_unique<NPC>();

	if (!npc->Create(model, desc, *this)) {
		return nullptr;
	}

	npc->GetBehaviours().CopySettings(mDefaultBehaviours);

	NPC* spawned = npc.get();
	mNPCs.Insert(std::move(npc));

	return spawned;
}

void NPCManager::Destroy(NPC* npc)
{
	for (uint32 i = 0; i < mNPCs.Size; i++) {
		if (mNPCs[i].get() == npc) {
			mNPCs.RemoveAt(i);
			return;
		}
	}
}

void NPCManager::Clear() { mNPCs.Clear(); }

void NPCManager::ClearRagdolls()
{
	for (uint32 i = mNPCs.Size; i > 0; i--) {
		if (mNPCs[i - 1]->IsRagdoll()) {
			mNPCs.RemoveAt(i - 1);
		}
	}
}

bool NPCManager::SpawnDummy(const Object* model, const String& name, const Vec3f position, const Vec3f velocity)
{
	std::unique_ptr<RagdollEntity> dummy = std::make_unique<RagdollEntity>();

	if (!dummy->Clone(model, name)) {
		return false;
	}

	Object* root = dummy->GetRoot();

	root->SetPosition(position);
	dummy->SetCullable(false);

	if (!dummy->CreateRagdoll()) {
		return false;
	}

	gWorld->AttachLoaded(root);
	dummy->Ragdoll.Activate(velocity);

	mDummies.Insert(std::move(dummy));

	return true;
}

void NPCManager::DestroyDummy(uint32 index)
{
	if (index < mDummies.Size) {
		mDummies.RemoveAt(index);
	}
}

void NPCManager::ClearDummies()
{
	for (uint32 i = mDummies.Size; i > 0; i--) {
		mDummies.RemoveAt(i - 1);
	}
}

RagdollEntity* NPCManager::FindContactOwner(const physics::RagdollContact& contact, bool& out_is_npc)
{
	out_is_npc = false;

	if (NPC* npc = FindByRagdollSerial(contact.RagdollSerial)) {
		out_is_npc = true;
		return &npc->GetActor();
	}

	for (std::unique_ptr<RagdollEntity>& dummy : mDummies) {
		if (dummy->OwnsContact(contact)) {
			return dummy.get();
		}
	}

	return nullptr;
}

void NPCManager::SpawnWorldBlood(RagdollEntity& actor, const physics::RagdollContact& contact) const
{
	constexpr float32 scMinSize = 0.4f;
	constexpr float32 scMaxSize = 1.0f;
	constexpr float32 scSpeedRange = 8.0f;

	if (actor.BloodCooldown > 0.0f) {
		return;
	}

	actor.BloodCooldown = scWoundCooldown;

	const float32 strength = MathUtil::Clamp((contact.Speed - Tuning.BloodSpeed) / scSpeedRange, 0.0f, 1.0f);
	const float32 size = scMinSize + (scMaxSize - scMinSize) * strength;

	gDecalManager->AddBloodSplat(contact.Point, contact.Normal, size);

	Vec3f reference = Vec3f::sRight;

	if (std::abs(contact.Normal.Y) < 0.9f) {
		reference = Vec3f::sUp;
	}

	const Vec3f tangent = reference.Cross(contact.Normal).Normalize();
	const Vec3f bitangent = contact.Normal.Cross(tangent);

	// A few smaller splats scattered around the main one
	const uint32 satellites = 2 + static_cast<uint32>(strength * 2.0f);

	for (uint32 i = 0; i < satellites; i++) {
		const float32 angle = FastRandUnit() * 2.0f * static_cast<float32>(M_PI);
		const float32 distance = size * (0.4f + 0.6f * FastRandUnit());

		const Vec3f offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * distance;

		gDecalManager->AddBloodSplat(contact.Point + offset, contact.Normal, size * (0.25f + 0.25f * FastRandUnit()));
	}
}

const physics::CharacterConfig& NPCManager::GetCharacterConfig()
{
	if (!mbCharacterConfigLoaded) {
		mCharacterConfig = physics::CharacterConfig::LoadPlayer();
		mbCharacterConfigLoaded = true;
	}

	return mCharacterConfig;
}

float32 NPCManager::GetActivePriority(const NPC& npc) const
{
	constexpr float32 scInViewScale = 2.0f;
	constexpr float32 scInViewCos = 0.5f;

	const Object* root = gObjectManager->GetObject(npc.GetRootID());

	if (root == nullptr || gWorld == nullptr || !gWorld->Player.pCamera) {
		return npc.Importance;
	}

	const Vec3f eye = gWorld->Player.pCamera->GetPosition();
	const Vec3f offset = root->GetPosition() - eye;
	const float32 distance = offset.Length();

	float32 priority = npc.Importance / std::max(distance, 1.0f);

	if (distance > 1e-3f && gWorld->Player.pCamera->GetForwardVector().Dot(offset / distance) > scInViewCos) {
		priority *= scInViewScale;
	}

	return priority;
}

uint32 NPCManager::GetActiveCount() const
{
	uint32 count = 0;

	for (const std::unique_ptr<NPC>& npc : mNPCs) {
		if (npc->IsPhysicsActive()) {
			count++;
		}
	}

	return count;
}

NPC* NPCManager::FindLowestPriorityActive(const NPC* exclude, float32& out_priority)
{
	NPC* lowest = nullptr;
	out_priority = 0.0f;

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		if (npc.get() == exclude || !npc->CanDeactivate()) {
			continue;
		}

		const float32 priority = GetActivePriority(*npc);

		if (lowest == nullptr || priority < out_priority) {
			lowest = npc.get();
			out_priority = priority;
		}
	}

	return lowest;
}

bool NPCManager::RequestActive(const NPC& npc)
{
	const Object* root = gObjectManager->GetObject(npc.GetRootID());

	if (Tuning.ActiveDistance > 0.0f && root != nullptr && gWorld != nullptr && gWorld->Player.pCamera) {
		const float32 distance = (root->GetPosition() - gWorld->Player.pCamera->GetPosition()).Length();

		if (distance > Tuning.ActiveDistance) {
			mDeniedRequests++;
			return false;
		}
	}

	if (Tuning.MaxActive == 0 || GetActiveCount() < Tuning.MaxActive) {
		return true;
	}

	float32 lowest_priority = 0.0f;
	NPC* lowest = FindLowestPriorityActive(&npc, lowest_priority);

	if (lowest == nullptr || lowest_priority >= GetActivePriority(npc)) {
		mDeniedRequests++;
		return false;
	}

	lowest->Deactivate();
	mEvictions++;

	return true;
}

void NPCManager::EnforceBudget()
{
	if (Tuning.ActiveDistance > 0.0f && gWorld != nullptr && gWorld->Player.pCamera) {
		const Vec3f eye = gWorld->Player.pCamera->GetPosition();

		for (std::unique_ptr<NPC>& npc : mNPCs) {
			const Object* root = gObjectManager->GetObject(npc->GetRootID());

			if (root != nullptr && npc->CanDeactivate() &&
				(root->GetPosition() - eye).Length() > Tuning.ActiveDistance) {
				npc->Deactivate();
				mEvictions++;
			}
		}
	}

	if (Tuning.MaxActive == 0) {
		return;
	}

	// Lowering the budget sends the least important standing NPCs back to animation
	for (uint32 active = GetActiveCount(); active > Tuning.MaxActive; active--) {
		float32 priority = 0.0f;
		NPC* lowest = FindLowestPriorityActive(nullptr, priority);

		if (lowest == nullptr) {
			break;
		}

		lowest->Deactivate();
		mEvictions++;
	}
}

void NPCManager::ProcessPlayerBumps()
{
	if (gWorld == nullptr) {
		return;
	}

	const physics::PhysicsPlayer& player = gWorld->Player.Physics;

	for (const physics::CharacterBump& bump : player.GetBumps()) {
		if (!NPCCharacterUserData::Matches(bump.OtherUserData)) {
			continue;
		}

		const float32 closing = bump.GetClosingSpeed();

		if (closing < Tuning.BumpSpeed) {
			continue;
		}

		NPC* npc = FindByRagdollSerial(NPCCharacterUserData::GetSerial(bump.OtherUserData));

		if (npc == nullptr) {
			continue;
		}

		Vec3f direction = Vec3f(bump.Normal.X, 0.0f, bump.Normal.Z);
		const float32 length = direction.Length();

		if (length < 1e-3f) {
			continue;
		}

		direction = direction / length;

		const float32 impulse = std::min(Tuning.BumpImpulse * closing, Tuning.MaxBumpImpulse);

		npc->OnPush(NPCPush { .Point = bump.Point, .Impulse = direction * impulse, .Source = gWorld->Player.Position });
	}
}

void NPCManager::UpdateStats(float32 delta_time)
{
	constexpr float32 scAverageTime = 1.0f;

	const float32 blend = std::min(delta_time / scAverageTime, 1.0f);

	mAveragePhysicsMs += (gPhysics->pBackend->GetLastUpdateMs() - mAveragePhysicsMs) * blend;
	mAverageActive += (static_cast<float32>(GetActiveCount()) - mAverageActive) * blend;
}

void NPCManager::LogStats() const
{
	uint32 kinematic = 0;
	uint32 limp = 0;
	uint32 moving = 0;

	for (const std::unique_ptr<NPC>& npc : mNPCs) {
		if (npc->IsRagdoll()) {
			limp++;
		}
		else if (!npc->IsPhysicsActive()) {
			kinematic++;
		}

		if (npc->GetMover().IsEnabled() && !npc->GetMover().IsResting()) {
			moving++;
		}
	}

	LogInfo(LC_PHYSICS,
			"NPCs: {} total, {} active (budget {}, {:.1f} avg), {} kinematic, {} limp, {} capsules moving, {} dummies",
			mNPCs.Size, GetActiveCount(), Tuning.MaxActive, mAverageActive, kinematic, limp, moving, mDummies.Size);
	LogInfo(LC_PHYSICS,
			"NPCs: physics {:.2f} ms per frame (1 s average), {} requests denied, {} sent back to kinematic",
			mAveragePhysicsMs, mDeniedRequests, mEvictions);

	for (const std::unique_ptr<NPC>& npc : mNPCs) {
		const char* physics = "kinematic";

		if (npc->IsRagdoll()) {
			physics = "limp";
		}
		else if (npc->IsPhysicsActive()) {
			physics = "active";
		}

		LogInfo(LC_PHYSICS, "  {}: brain {}, {}, priority {:.3f}", npc->GetName(),
				NPCBrain::GetStateName(npc->GetBrain().GetState()), physics, GetActivePriority(*npc));
	}
}

void NPCManager::Update(float32 delta_time, bool draw_debug)
{
	EnforceBudget();
	ProcessPlayerBumps();
	UpdateStats(delta_time);

	gPhysics->pBackend->SetMinRagdollContactSpeed(
		std::min({ Tuning.BloodSpeed, Tuning.MinImpactSpeed, Tuning.SafeImpactSpeed }));

	gPhysics->pBackend->DrainRagdollContacts(mContacts);

	for (const physics::RagdollContact& contact : mContacts) {
		if (NPC* npc = FindByRagdollSerial(contact.RagdollSerial)) {
			npc->OnContact(contact);
		}
	}

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		npc->Update(delta_time, draw_debug);
	}

	for (std::unique_ptr<RagdollEntity>& dummy : mDummies) {
		dummy->TickCooldown(delta_time);
	}

	// Blood runs after the NPC updates so it sees this frame's wound cooldowns
	for (const physics::RagdollContact& contact : mContacts) {
		bool is_npc = false;
		RagdollEntity* owner = FindContactOwner(contact, is_npc);

		if (owner == nullptr) {
			continue;
		}

		if (contact.IsStruck(Tuning.MinImpactSpeed)) {
			// NPCs add their own wound in OnContact
			if (!is_npc) {
				owner->AddContactWound(contact);
			}

			continue;
		}

		if (contact.bOtherStatic && contact.Speed >= Tuning.BloodSpeed) {
			SpawnWorldBlood(*owner, contact);
		}
	}

	for (std::unique_ptr<RagdollEntity>& dummy : mDummies) {
		dummy->Ragdoll.WriteToSkeleton();

		if (draw_debug) {
			dummy->Ragdoll.DebugDraw();
		}
	}
}

NPC* NPCManager::FindByName(const char* name)
{
	if (name == nullptr) {
		return nullptr;
	}

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		if (std::strcmp(npc->GetName().CStr(), name) == 0) {
			return npc.get();
		}
	}

	return nullptr;
}

bool NPCManager::SetBehaviourSetting(uint32 serial, const char* behaviour, const char* setting, float32 value)
{
	if (behaviour == nullptr || setting == nullptr) {
		return false;
	}

	if (serial != 0) {
		NPC* npc = FindByRagdollSerial(serial);

		if (npc == nullptr) {
			return false;
		}

		physics::RagdollBehaviour* target = npc->GetBehaviours().Find(behaviour);

		return target != nullptr && target->SetSetting(setting, value);
	}

	physics::RagdollBehaviour* defaults = mDefaultBehaviours.Find(behaviour);

	if (defaults == nullptr || !defaults->SetSetting(setting, value)) {
		return false;
	}

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		if (physics::RagdollBehaviour* target = npc->GetBehaviours().Find(behaviour)) {
			target->SetSetting(setting, value);
		}
	}

	return true;
}

bool NPCManager::GetBehaviourSetting(uint32 serial, const char* behaviour, const char* setting, float32& out_value)
{
	if (behaviour == nullptr || setting == nullptr) {
		return false;
	}

	physics::RagdollBehaviours* behaviours = &mDefaultBehaviours;

	if (serial != 0) {
		NPC* npc = FindByRagdollSerial(serial);

		if (npc == nullptr) {
			return false;
		}

		behaviours = &npc->GetBehaviours();
	}

	const physics::RagdollBehaviour* target = behaviours->Find(behaviour);

	return target != nullptr && target->GetSetting(setting, out_value);
}

NPC* NPCManager::FindByRagdollSerial(uint32 serial)
{
	for (std::unique_ptr<NPC>& npc : mNPCs) {
		if (npc->GetRagdoll().IsCreated() && npc->GetRagdollSerial() == serial) {
			return npc.get();
		}
	}

	return nullptr;
}

} // namespace fx
