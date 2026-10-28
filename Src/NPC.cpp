#include "NPC.hpp"

#include <Core/Log.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Math/Quat.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>
#include <algorithm>

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

	MaxHealth = desc.MaxHealth;
	RagdollHealth = desc.RagdollHealth;
	mHealth = desc.MaxHealth;
	mState = eState::Animated;
	mbTeleported = true;
	mPendingImpactDamage = 0.0f;

	if (!desc.IdleAnimation.IsEmpty()) {
		SetIdleAnimation(desc.IdleAnimation);
	}

	const physics::RagdollDef* ragdoll_def = physics::GetHumanoidRagdollDef();

	if (ragdoll_def == nullptr || !mRagdoll.Create(mpSkeleton, skinned->GetWorldMatrix(), *ragdoll_def)) {
		LogWarning("NPC '{}': no ragdoll could be built for the skeleton, it will stay animated", desc.Name);
	}

	mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	mRagdoll.SetRootAssist(RootAssist);
	mRagdoll.Drive(skinned->GetWorldMatrix(), 0.0f, true);
	mAssistFade = 1.0f;
	mPoweredTime = 0.0f;
	mPhysicsBlend = 1.0f;

	mBodyStrengths.Free();
	mBodyStrengths.InitCapacity(mRagdoll.GetBodyCount());
	mMass = 0.0f;

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mBodyStrengths.Insert(1.0f);
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

	mRagdoll.Destroy();

	if (!mRootID.IsInvalid()) {
		DestroyObjectTree(mRootID);
	}

	mRootID = ObjectID::scNull;
	mSkinnedID = ObjectID::scNull;
	mpSkeleton = nullptr;
	mState = eState::Animated;
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

	if (mState != eState::Animated) {
		mRagdoll.WriteToSkeleton();
	}
	else if (Object* skinned = GetSkinnedObject()) {
		float32 pose_time = delta_time;

		if (mRagdoll.IsBalancing()) {
			pose_time = 0.0f;
		}

		mpSkeleton->AdvancePose(pose_time);

		UpdateBalance(delta_time);
		UpdateReactions(delta_time);
		UpdatePhysicsBlend(delta_time);

		mRagdoll.Drive(skinned->GetWorldMatrix(), delta_time, mbTeleported);
		mRagdoll.WriteToSkeleton();

		mbTeleported = false;
	}

	if (draw_debug) {
		mRagdoll.DebugDraw();
		DrawDebugHealth();
	}
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

	const float32 fraction = std::clamp(mHealth / MaxHealth, 0.0f, 1.0f);

	renderer::gDebugDraw->Line(left_end, left_end + right * scBarWidth, Color::FromRGBA(60, 60, 60, 255));

	const Color fill = (mState == eState::Ragdoll) ? Color::FromRGBA(150, 150, 150, 255)
												   : Color::FromRGBA(static_cast<uint8>(255.0f * (1.0f - fraction)),
																	 static_cast<uint8>(255.0f * fraction), 40, 255);

	renderer::gDebugDraw->Line(left_end, left_end + right * (scBarWidth * fraction), fill);
}

void NPC::Damage(float32 amount)
{
	if (!IsCreated() || mState == eState::Ragdoll) {
		return;
	}

	mHealth = std::max(mHealth - amount, 0.0f);

	if (mHealth <= RagdollHealth) {
		EnterRagdoll();
	}
}

NPC::eBodyPart NPC::GetBodyPart(JPH::BodyID body_id) const
{
	const int32 body_index = mRagdoll.FindBodyIndex(body_id);

	return (body_index < 0) ? eBodyPart::Torso : mRagdoll.GetBodyPart(static_cast<uint32>(body_index));
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
	if (!IsCreated() || mState == eState::Ragdoll) {
		return;
	}

	if (GetBodyPart(body_id) == eBodyPart::Head && amount >= HeadKillDamage) {
		Damage(mHealth);
		return;
	}

	Damage(amount * GetDamageScale(body_id));
}

void NPC::EnterRagdoll()
{
	if (!IsCreated() || mState == eState::Ragdoll || !mRagdoll.IsCreated()) {
		return;
	}

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
	mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Limp);

	for (uint32 i = 0; i < mRagdoll.GetBodyCount(); i++) {
		mRagdoll.SetBodyBlend(i, 1.0f);
	}

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}

	SetCullable(false);

	mState = eState::Fallen;
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
	constexpr float32 scMinBalanceTime = 0.75f;
	constexpr float32 scAssistFadeTime = 0.5f;

	if (mState != eState::Animated) {
		return;
	}

	if (!bBalance) {
		mRagdoll.SetBalancing(false);
		mAssistFade = 1.0f;
		return;
	}

	if (!mRagdoll.IsBalancing()) {
		mAssistFade = std::min(mAssistFade + delta_time / scAssistFadeTime, 1.0f);
		return;
	}

	const physics::BalanceStatus& status = mRagdoll.GetBalanceStatus();

	mBalanceTime += delta_time;

	if (status.State == physics::eBalanceState::Fallen) {
		EnterFallen();
		return;
	}

	if (mBalanceTime > scMinBalanceTime && status.bCalm) {
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

	bool is_settled = !mRagdoll.IsBalancing() && mPoweredTime >= scMinPoweredTime && mAssistFade >= 1.0f;

	for (const float32 body_strength : mBodyStrengths) {
		if (body_strength < 1.0f) {
			is_settled = false;
		}
	}

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

void NPC::UpdateReactions(float32 delta_time)
{
	const float32 recover_rate = (ReactRecoverTime > 0.0f) ? delta_time / ReactRecoverTime : 1.0f;

	for (float32& body_strength : mBodyStrengths) {
		body_strength = std::min(body_strength + recover_rate, 1.0f);
	}

	ApplyStrengths();
}

void NPC::ApplyStrengths()
{
	if (mBodyStrengths.IsEmpty()) {
		return;
	}

	for (uint32 i = 0; i < mBodyStrengths.Size; i++) {
		mRagdoll.SetBodyStrength(i, Strength * mBodyStrengths[i]);
	}

	mRagdoll.SetRootAssist(RootAssist * mBodyStrengths[0] * mAssistFade);
}

void NPC::AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	if (!IsCreated()) {
		return;
	}

	if (mState != eState::Animated) {
		mRagdoll.AddImpulse(body_id, impulse, point);
		return;
	}

	React(body_id, impulse, point);
}

void NPC::React(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	constexpr float32 scMinSpeed = 1.0f;
	constexpr float32 scMaxSpeed = 4.0f;

	if (!IsCreated() || mState != eState::Animated || !mRagdoll.IsCreated()) {
		return;
	}

	const int32 body = mRagdoll.FindBodyIndex(body_id);
	const uint32 body_count = mRagdoll.GetBodyCount();

	if (body < 0) {
		return;
	}

	SetPowered(true);
	mPoweredTime = 0.0f;

	if (!mRagdoll.IsAnchor(body)) {
		const auto child_count = [&](int32 parent)
		{
			uint32 count = 0;

			for (uint32 i = 0; i < body_count; i++) {
				count += (mRagdoll.GetParentBody(i) == parent) ? 1 : 0;
			}

			return count;
		};

		int32 top = body;

		for (int32 parent = mRagdoll.GetParentBody(top);
			 parent >= 0 && !mRagdoll.IsAnchor(parent) && child_count(parent) == 1;
			 parent = mRagdoll.GetParentBody(top)) {
			top = parent;
		}

		for (uint32 i = 0; i < body_count; i++) {
			for (int32 walk = static_cast<int32>(i); walk >= 0; walk = mRagdoll.GetParentBody(walk)) {
				if (walk == top) {
					mBodyStrengths[i] = std::min(mBodyStrengths[i], ReactStrength);
					break;
				}
			}
		}
	}
	else {
		mBodyStrengths[body] = std::min(mBodyStrengths[body], ReactStrength);
	}

	ApplyStrengths();

	const float32 length = impulse.Length();
	const float32 mass = mRagdoll.GetBodyMass(body);

	if (length <= 1e-5f || mass <= 0.0f) {
		return;
	}

	ActivateBalance();
	ApplyStrengths();

	const float32 speed = std::clamp(length / mass, scMinSpeed, scMaxSpeed);

	mRagdoll.AddImpulse(body_id, impulse * (speed * mass / length), point);
}

float32 GetContactWoundSize(const physics::RagdollContact& contact)
{
	constexpr float32 scMinSize = 0.15f;
	constexpr float32 scMaxSize = 0.4f;
	constexpr float32 scMaxMomentum = 150.0f;

	const float32 strength = std::clamp(contact.OtherMass * contact.Speed / scMaxMomentum, 0.0f, 1.0f);

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

	if (!is_struck) {
		return;
	}

	float32 knock_mass = scMaxKnockMass;

	if (contact.OtherMass > 0.0f) {
		knock_mass = std::min(contact.OtherMass, scMaxKnockMass);
	}

	AddImpulse(contact.Body, contact.Normal * (knock_mass * contact.Speed), contact.Point);
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
		npc->ReactStrength = ReactStrength;
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
