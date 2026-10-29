#include "NPC.hpp"

#include <Core/Log.hpp>
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

bool NPC::Create(const Object* model, const NPCDesc& desc)
{
	Destroy();

	if (model == nullptr) {
		return false;
	}

	Object* root = model->CloneWithOwnSkeleton(std::string(desc.Name.CStr()));

	DynArray<Object*> parts;
	CollectObjectTree(root, parts);

	const auto skinned_part = std::find_if(parts.begin(), parts.end(),
										   [](const Object* part) { return part->pSkeleton.IsValid(); });

	if (skinned_part == parts.end()) {
		LogWarning("NPC '{}': the model has no skeleton", desc.Name);
		DestroyObjectTree(root->ID);
		return false;
	}

	Object* skinned = *skinned_part;

	root->SetPosition(desc.Position);
	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, desc.Yaw));
	root->SetShadowCaster(true);
	root->SetProbeVisible(false);

	mRootID = root->ID;
	mSkinnedID = skinned->ID;
	mpSkeleton = skinned->pSkeleton;
	mName = desc.Name;

	MaxHealth = desc.MaxHealth;
	RagdollHealth = desc.RagdollHealth;
	mHealth = desc.MaxHealth;
	mState = eState::Animated;
	mbDying = false;
	mbTeleported = true;
	mPendingImpactDamage = 0.0f;

	if (!desc.IdleAnimation.IsEmpty()) {
		SetIdleAnimation(desc.IdleAnimation);
	}

	const physics::RagdollDef* ragdoll_def = physics::GetHumanoidRagdollDef();

	if (ragdoll_def == nullptr || !mRagdoll.Create(mpSkeleton, skinned->GetWorldMatrix(), *ragdoll_def)) {
		LogWarning("NPC '{}': no ragdoll could be built for the skeleton, it will stay animated", desc.Name);
	}

	if (mRagdoll.IsCreated()) {
		mBehaviours.Init(mRagdoll);
	}

	mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	mRagdoll.SetRootAssist(RootAssist);
	mRagdoll.Drive(skinned->GetWorldMatrix(), 0.0f, true);
	mAssistFade = 1.0f;
	mPoweredTime = 0.0f;
	mPhysicsBlend = 1.0f;

	mMass = 0.0f;

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mMass += mRagdoll.GetBodyMass(i);
	}

	gWorld->AttachLoaded(root);

	return true;
}

void NPC::Destroy()
{
	if (mpSkeleton.IsValid() && gDecalManager != nullptr) {
		gDecalManager->RemoveSkinnedDecals(&(*mpSkeleton));
	}

	mBehaviours.Reset();
	mRagdoll.Destroy();

	if (!mRootID.IsInvalid()) {
		DestroyObjectTree(mRootID);
	}

	mRootID = ObjectID::scNull;
	mSkinnedID = ObjectID::scNull;
	mpSkeleton = nullptr;
	mState = eState::Animated;
	mbDying = false;
}

Object* NPC::GetSkinnedObject() const { return gObjectManager->GetObject(mSkinnedID); }

void NPC::Update(float32 delta_time, bool draw_debug)
{
	if (!IsCreated()) {
		return;
	}

	BloodCooldown = std::max(BloodCooldown - delta_time, 0.0f);

	if (mPendingImpactDamage > 0.0f) {
		Damage(mPendingImpactDamage);
		mPendingImpactDamage = 0.0f;
	}

	if (mState == eState::Ragdoll) {
		mRagdoll.WriteToSkeleton();
	}
	else {
		UpdateActive(delta_time);
	}

	if (draw_debug) {
		mRagdoll.DebugDraw();
		mBehaviours.DebugDraw();
		DrawDebugHealth();
	}
}

void NPC::UpdateActive(float32 delta_time)
{
	Object* skinned = GetSkinnedObject();

	if (mState == eState::Animated && skinned != nullptr) {
		float32 pose_time = delta_time;

		if (mRagdoll.IsBalancing() || mbDying) {
			pose_time = 0.0f;
		}

		mpSkeleton->AdvancePose(pose_time);
	}

	UpdateBehaviours(delta_time);

	if (mState == eState::Animated && skinned != nullptr) {
		UpdateBalance(delta_time);
		UpdatePhysicsBlend(delta_time);
		ApplyStrengths();

		mRagdoll.Drive(skinned->GetWorldMatrix(), delta_time, mbTeleported);
		mbTeleported = false;
	}

	mRagdoll.WriteToSkeleton();
}

void NPC::UpdateBehaviours(float32 delta_time)
{
	mBehaviours.Update(mRagdoll, delta_time);

	if (mState == eState::Animated && IsPowered() && mBehaviours.HadEvent(physics::eRagdollEvent::LostSupport)) {
		EnterFallen();
	}

	if (mbDying && (mBehaviours.IsDeathComplete() || !mBehaviours.IsDying())) {
		EnterRagdoll();
		return;
	}

	ApplyStrengths();
}

void NPC::DrawDebugHealth() const
{
	constexpr float32 scBarHeight = 2.2f;
	constexpr float32 scBarWidth = 1.0f;

	const Object* root = gObjectManager->GetObject(mRootID);

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

void NPC::Damage(float32 amount) { ApplyDamage(amount, -1); }

void NPC::ApplyDamage(float32 amount, int32 body)
{
	if (!IsCreated() || mState == eState::Ragdoll || mbDying) {
		return;
	}

	mHealth = std::max(mHealth - amount, 0.0f);

	if (mHealth <= RagdollHealth) {
		Kill(body);
	}
}

NPC::eBodyPart NPC::GetBodyPart(JPH::BodyID body_id) const
{
	const int32 body_index = mRagdoll.FindBodyIndex(body_id);

	if (body_index < 0) {
		return eBodyPart::Torso;
	}

	return mRagdoll.GetBodyPart(static_cast<uint32>(body_index));
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
	if (!IsCreated() || mState == eState::Ragdoll || mbDying) {
		return;
	}

	const int32 body = mRagdoll.FindBodyIndex(body_id);

	if (GetBodyPart(body_id) == eBodyPart::Head && amount >= HeadKillDamage) {
		ApplyDamage(mHealth, body);
		return;
	}

	ApplyDamage(amount * GetDamageScale(body_id), body);
}

void NPC::Kill(int32 body)
{
	if (!IsCreated() || mState == eState::Ragdoll || mbDying) {
		return;
	}

	mHealth = std::min(mHealth, RagdollHealth);

	if (!mRagdoll.IsCreated() || !mBehaviours.IsValid()) {
		EnterRagdoll();
		return;
	}

	SetPowered(true);
	mPoweredTime = 0.0f;
	SetCullable(false);

	mbDying = true;
	mBehaviours.SetAlive(false);
	mBehaviours.HandleEvent(mRagdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Died, .Body = body });

	if (!mBehaviours.IsDying()) {
		EnterRagdoll();
		return;
	}

	ApplyStrengths();
}

void NPC::EnterRagdoll()
{
	if (!IsCreated() || mState == eState::Ragdoll || !mRagdoll.IsCreated()) {
		return;
	}

	mBehaviours.Reset();
	mRagdoll.ClearBodyLayers();

	Object* root = gObjectManager->GetObject(mRootID);
	Object* skinned = GetSkinnedObject();

	if (root == nullptr || skinned == nullptr) {
		return;
	}

	SetCullable(false);

	mRagdoll.GoLimp(skinned->GetWorldMatrix());
	mState = eState::Ragdoll;
}

void NPC::EnterFallen()
{
	if (mState != eState::Animated || !mRagdoll.IsCreated()) {
		return;
	}

	mRagdoll.SetBalancing(false);
	mRagdoll.SetRootAssist(0.0f);

	if (mBehaviours.IsValid()) {
		mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Powered);
	}
	else {
		mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Limp);
	}

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mRagdoll.SetBodyBlend(i, 1.0f);
	}

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}

	SetCullable(false);

	mState = eState::Fallen;

	const physics::BehaviourContext& ctx = mBehaviours.GetContext();

	mBehaviours.SetFreePose(true);
	mBehaviours.Trigger(mRagdoll, "fallen");
	mBehaviours.HandleEvent(mRagdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::LostSupport,
															  .Magnitude = -ctx.Velocity.Dot(ctx.Up) });
	ApplyStrengths();
}

void NPC::SetCullable(bool cullable)
{
	Object* root = gObjectManager->GetObject(mRootID);

	if (root == nullptr) {
		return;
	}

	DynArray<Object*> parts;
	CollectObjectTree(root, parts);

	for (Object* part : parts) {
		part->SetCullable(cullable);
	}
}

void NPC::UpdateBalance(float32 delta_time)
{
	constexpr float32 scAssistFadeTime = 0.5f;

	if (mState != eState::Animated) {
		return;
	}

	if (!bBalance) {
		mRagdoll.SetBalancing(false);
		mAssistFade = 1.0f;
		return;
	}

	const bool wants_balance = (mBehaviours.GetBalance() == physics::eBehaviourBalance::On);

	if (!mRagdoll.IsBalancing()) {
		mAssistFade = std::min(mAssistFade + delta_time / scAssistFadeTime, 1.0f);

		if (wants_balance) {
			ActivateBalance();
		}

		return;
	}

	const physics::BalanceStatus& status = mRagdoll.GetBalanceStatus();

	mBalanceTime += delta_time;

	if (status.State == physics::eBalanceState::Fallen) {
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
	if (!bBalance || mState != eState::Animated || !mRagdoll.IsCreated()) {
		return;
	}

	mRagdoll.SetBalancing(true);

	if (!mRagdoll.IsBalancing()) {
		return;
	}

	mBalanceTime = 0.0f;
	mAssistFade = 0.0f;
}

void NPC::ReturnToAnimation()
{
	Object* root = gObjectManager->GetObject(mRootID);

	if (root != nullptr) {
		const Vec3f offset = mRagdoll.GetBodyOrigin(0) - mRagdoll.GetBodyTargetOrigin(0);

		root->SetPosition(root->GetPosition() + Vec3f(offset.X, 0.0f, offset.Z));
	}

	mRagdoll.RebaseTargets();
	mRagdoll.SetBalancing(false);
	mAssistFade = 0.0f;
}

bool NPC::IsPowered() const
{
	if (!mRagdoll.IsCreated() || mRagdoll.GetBodyCount() == 0) {
		return false;
	}

	return mRagdoll.GetBodyMode(0) == physics::eRagdollBodyMode::Powered;
}

void NPC::SetPowered(bool powered)
{
	if (!mRagdoll.IsCreated() || IsPowered() == powered) {
		return;
	}

	if (powered) {
		mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Powered);
	}
	else {
		mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	}

	mPhysicsBlend = 1.0f;

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mRagdoll.SetBodyBlend(i, mPhysicsBlend);
	}
}

void NPC::UpdatePhysicsBlend(float32 delta_time)
{
	constexpr float32 scMinPoweredTime = 1.0f;
	constexpr float32 scBlendOutTime = 0.25f;

	if (mState != eState::Animated || !IsPowered()) {
		return;
	}

	mPoweredTime += delta_time;

	const bool is_settled = !mRagdoll.IsBalancing() && !mbDying && mPoweredTime >= scMinPoweredTime &&
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

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mRagdoll.SetBodyBlend(i, mPhysicsBlend);
	}
}

void NPC::ApplyStrengths()
{
	if (!mRagdoll.IsCreated()) {
		return;
	}

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mRagdoll.SetBodyStrength(i, Strength * mBehaviours.GetStrength(i));
	}

	float32 assist = 0.0f;

	if (mState == eState::Animated && !mbDying) {
		assist = RootAssist * std::min(mBehaviours.GetStrength(0), 1.0f) * mAssistFade;
	}

	mRagdoll.SetRootAssist(assist);
}

void NPC::AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	OnHit(NPCHit { .Body = body_id, .Point = point, .Impulse = impulse });
}

void NPC::OnHit(const NPCHit& hit)
{
	constexpr float32 scMinSpeed = 1.0f;
	constexpr float32 scMaxSpeed = 4.0f;

	if (!IsCreated()) {
		return;
	}

	if (hit.Damage > 0.0f) {
		Damage(hit.Damage, hit.Body);
	}

	const int32 body = mRagdoll.FindBodyIndex(hit.Body);

	if (mState == eState::Ragdoll || body < 0) {
		mRagdoll.AddImpulse(hit.Body, hit.Impulse, hit.Point);
		return;
	}

	SetPowered(true);
	mPoweredTime = 0.0f;

	const float32 length = hit.Impulse.Length();
	Vec3f direction = Vec3f::sZero;

	if (length > 1e-5f) {
		direction = hit.Impulse / length;
	}

	mBehaviours.HandleEvent(mRagdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Hit,
															  .Body = body,
															  .Point = hit.Point,
															  .Direction = direction,
															  .Magnitude = length,
															  .Damage = hit.Damage });
	ApplyStrengths();
	UpdateBalance(0.0f);
	ApplyStrengths();

	const float32 mass = mRagdoll.GetBodyMass(body);

	if (length <= 1e-5f || mass <= 0.0f) {
		return;
	}

	if (mState != eState::Animated) {
		mRagdoll.AddImpulse(hit.Body, hit.Impulse, hit.Point);
		return;
	}

	const float32 speed = MathUtil::Clamp(length / mass, scMinSpeed, scMaxSpeed);

	mRagdoll.AddImpulse(hit.Body, hit.Impulse * (speed * mass / length), hit.Point);
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
		SetPowered(true);
		mPoweredTime = 0.0f;

		if (behaviour->NeedsFreePose()) {
			EnterFallen();
		}
	}

	const bool started = mBehaviours.Trigger(mRagdoll, name);

	UpdateBalance(0.0f);
	ApplyStrengths();

	return started;
}

bool NPC::StopBehaviour(const char* name)
{
	if (name == nullptr) {
		return false;
	}

	return mBehaviours.Stop(name);
}

float32 GetContactWoundSize(const physics::RagdollContact& contact)
{
	constexpr float32 scMinSize = 0.15f;
	constexpr float32 scMaxSize = 0.4f;
	constexpr float32 scMaxMomentum = 150.0f;

	const float32 strength = MathUtil::Clamp(contact.OtherMass * contact.Speed / scMaxMomentum, 0.0f, 1.0f);

	return scMinSize + (scMaxSize - scMinSize) * strength;
}

float32 NPC::GetContactDamage(const physics::RagdollContact& contact) const
{
	const float32 excess = contact.Speed - SafeImpactSpeed;

	if (excess <= 0.0f) {
		return 0.0f;
	}

	float32 mass = mMass;

	if (contact.OtherMass > 0.0f) {
		mass = (mMass * contact.OtherMass) / (mMass + contact.OtherMass);
	}

	return ImpactDamageScale * mass * excess * GetDamageScale(contact.Body);
}

void NPC::OnContact(const physics::RagdollContact& contact)
{
	constexpr float32 scMaxKnockMass = 80.0f;
	constexpr float32 scWoundCooldown = 0.25f;

	if (!IsCreated()) {
		return;
	}

	const bool is_struck = contact.IsStruck(MinImpactSpeed);

	if (is_struck && BloodCooldown <= 0.0f) {
		AddWound(contact.Body, contact.Point, contact.Normal, GetContactWoundSize(contact));
		BloodCooldown = scWoundCooldown;
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

		OnHit(NPCHit { .Body = contact.Body,
					   .Point = contact.Point,
					   .Impulse = contact.Normal * (knock_mass * contact.Speed) });
		return;
	}

	if (!contact.bOtherStatic || contact.Speed < MinImpactSpeed) {
		return;
	}

	mBehaviours.HandleEvent(mRagdoll, physics::RagdollEvent { .Type = physics::eRagdollEvent::Impact,
															  .Body = mRagdoll.FindBodyIndex(contact.Body),
															  .Point = contact.Point,
															  .Direction = contact.Normal,
															  .Magnitude = contact.Speed });
}

void NPC::AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size)
{
	Object* skinned = GetSkinnedObject();
	uint32 bone_index = 0;

	if (skinned == nullptr || !mpSkeleton.IsValid() || !mRagdoll.FindBoneForBody(body_id, bone_index) ||
		bone_index >= mpSkeleton->SkinningMatrices.Size) {
		return;
	}

	const Mat4f rest_to_world = mpSkeleton->SkinningMatrices[bone_index] * skinned->GetWorldMatrix();

	gDecalManager->AddSkinnedBloodSplat(&(*mpSkeleton), rest_to_world, point, direction, size);
}

void NPC::SetPosition(const Vec3f position)
{
	Object* root = gObjectManager->GetObject(mRootID);

	if (root == nullptr || mState != eState::Animated) {
		return;
	}

	root->SetPosition(position);
	mbTeleported = true;
}

void NPC::SetYaw(float32 yaw)
{
	Object* root = gObjectManager->GetObject(mRootID);

	if (root == nullptr || mState != eState::Animated) {
		return;
	}

	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, yaw));
	mbTeleported = true;
}

void NPC::SetIdleAnimation(const String& name, float32 speed, float32 blend_time)
{
	if (!mpSkeleton.IsValid()) {
		return;
	}

	const Animation* anim = mpSkeleton->FindAnimation(name);

	if (anim == nullptr) {
		LogWarning("NPC: could not find idle animation '{}'", name);
		return;
	}

	mpSkeleton->SetRestAnimation(anim, speed, blend_time);
}

bool NPC::PlayAnimation(const String& name, eAnimationEnd on_end, float32 speed, float32 blend_time)
{
	if (!mpSkeleton.IsValid() || mState == eState::Ragdoll) {
		return false;
	}

	return mpSkeleton->PushAnimation(name, on_end, speed, blend_time);
}

NPC* NPCManager::Spawn(const Object* model, const NPCDesc& desc)
{
	std::unique_ptr<NPC> npc = std::make_unique<NPC>();

	if (!npc->Create(model, desc)) {
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

void NPCManager::Update(float32 delta_time, bool draw_debug)
{
	gPhysics->pBackend->DrainRagdollContacts(mContacts);

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		npc->Strength = Strength;
		npc->RootAssist = RootAssist;
		npc->bBalance = bBalance;
		npc->ImpactDamageScale = ImpactDamageScale;
		npc->MinImpactSpeed = MinImpactSpeed;
		npc->SafeImpactSpeed = SafeImpactSpeed;
	}

	for (const physics::RagdollContact& contact : mContacts) {
		if (NPC* npc = FindByRagdollSerial(contact.RagdollSerial)) {
			npc->OnContact(contact);
		}
	}

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		npc->Update(delta_time, draw_debug);
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
