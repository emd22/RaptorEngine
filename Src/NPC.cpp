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

	std::vector<Object*> parts;
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

	if (!desc.IdleAnimation.IsEmpty()) {
		SetIdleAnimation(desc.IdleAnimation);
	}

	if (!mRagdoll.Create(mpSkeleton, skinned->GetWorldMatrix())) {
		LogWarning("NPC '{}': no ragdoll could be built for the skeleton, it will stay animated", desc.Name);
	}

	mRagdoll.SetAllBodyModes(physics::eRagdollBodyMode::Kinematic);
	mRagdoll.Drive(skinned->GetWorldMatrix(), 0.0f, true);

	mSettleTimes.assign(mRagdoll.GetBodyCount(), 0.0f);

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

	if (mState == eState::Ragdoll) {
		mRagdoll.WriteToSkeleton();
	}
	else if (Object* skinned = GetSkinnedObject()) {
		mpSkeleton->AdvancePose(delta_time);

		UpdateReactions(delta_time);

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

	const Color fill = (mState == eState::Ragdoll)
						   ? Color::FromRGBA(150, 150, 150, 255)
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

void NPC::Damage(float32 amount, JPH::BodyID body_id)
{
	if (!IsCreated() || mState == eState::Ragdoll) {
		return;
	}

	switch (GetBodyPart(body_id)) {
	case eBodyPart::Head:
		if (amount >= HeadKillDamage) {
			Damage(mHealth);
			return;
		}

		Damage(amount * HeadDamageScale);
		return;
	case eBodyPart::Arm:
	case eBodyPart::Leg:
		Damage(amount * LimbDamageScale);
		return;
	case eBodyPart::Torso:
		Damage(amount * TorsoDamageScale);
		return;
	}
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

	std::vector<Object*> parts;
	CollectObjectTree(root, parts);

	for (Object* part : parts) {
		part->SetCullable(false);
	}

	mRagdoll.GoLimp(skinned->GetWorldMatrix());
	mState = eState::Ragdoll;

	mSettleTimes.assign(mSettleTimes.size(), 0.0f);
}

void NPC::UpdateReactions(float32 delta_time)
{
	constexpr float32 scSettleDistance = 0.0075f;
	constexpr float32 scSettleAngle = 0.035f;
	constexpr float32 scSettleTime = 0.15f;

	for (uint32 i = 0; i < mSettleTimes.size(); i++) {
		if (mRagdoll.GetBodyMode(i) != physics::eRagdollBodyMode::Powered) {
			continue;
		}

		float32 distance = 0.0f;
		float32 angle = 0.0f;

		if (!mRagdoll.GetBodyTrackingError(i, distance, angle) || distance > scSettleDistance || angle > scSettleAngle) {
			mSettleTimes[i] = 0.0f;
			continue;
		}

		mSettleTimes[i] += delta_time;

		if (mSettleTimes[i] >= scSettleTime) {
			mSettleTimes[i] = 0.0f;

			mRagdoll.SetBodyMode(i, physics::eRagdollBodyMode::Kinematic);
			mRagdoll.SetBodyStrength(i, 1.0f);
		}
	}
}

void NPC::AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	if (!IsCreated()) {
		return;
	}

	if (mState == eState::Ragdoll) {
		mRagdoll.AddImpulse(body_id, impulse, point);
		return;
	}

	React(body_id, impulse, point);
}

void NPC::React(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	constexpr float32 scMinSpeed = 1.0f;
	constexpr float32 scMaxSpeed = 4.0f;
	constexpr float32 scAnchorScale = 0.5f;

	if (!IsCreated() || mState == eState::Ragdoll || !mRagdoll.IsCreated()) {
		return;
	}

	const int32 hit_body = mRagdoll.FindBodyIndex(body_id);
	const uint32 body_count = mRagdoll.GetBodyCount();

	if (hit_body < 0) {
		return;
	}

	int32 body = hit_body;
	Vec3f at = point;
	float32 scale = 1.0f;

	if (mRagdoll.IsAnchor(hit_body)) {
		body = -1;

		for (uint32 i = 0; i < body_count; i++) {
			const int32 parent = mRagdoll.GetParentBody(i);

			if (!mRagdoll.IsAnchor(i) && parent >= 0 && mRagdoll.IsAnchor(parent)) {
				body = static_cast<int32>(i);
				break;
			}
		}

		if (body < 0) {
			return;
		}

		at = mRagdoll.GetBodyPosition(body);
		scale = scAnchorScale;
	}

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
		bool in_chain = false;

		for (int32 walk = static_cast<int32>(i); walk >= 0; walk = mRagdoll.GetParentBody(walk)) {
			if (walk == top) {
				in_chain = true;
				break;
			}
		}

		if (!in_chain) {
			continue;
		}

		mRagdoll.SetBodyMode(i, physics::eRagdollBodyMode::Powered);
		mRagdoll.SetBodyStrength(i, ReactStrength);
		mRagdoll.SetBodyBlend(i, 1.0f);

		mSettleTimes[i] = 0.0f;
	}

	const float32 length = impulse.Length();
	const float32 mass = mRagdoll.GetBodyMass(body);

	if (length <= 1e-5f || mass <= 0.0f) {
		return;
	}

	const float32 speed = std::clamp(length * scale / mass, scMinSpeed, scMaxSpeed);

	mRagdoll.AddImpulse(mRagdoll.GetBodyID(body), impulse * (speed * mass / length), at);
}

float32 GetHitWoundSize(const physics::RagdollHit& hit)
{
	constexpr float32 scMinSize = 0.15f;
	constexpr float32 scMaxSize = 0.4f;
	constexpr float32 scMaxMomentum = 150.0f;

	const float32 strength = std::clamp(hit.Mass * hit.Speed / scMaxMomentum, 0.0f, 1.0f);

	return scMinSize + (scMaxSize - scMinSize) * strength;
}

void NPC::OnObjectHit(const physics::RagdollHit& hit, float32 damage_scale)
{
	constexpr float32 scMaxKnockMass = 80.0f;
	constexpr float32 scWoundCooldown = 0.25f;

	if (!IsCreated()) {
		return;
	}

	if (BloodCooldown <= 0.0f) {
		AddWound(hit.Body, hit.Point, hit.Direction, GetHitWoundSize(hit));
		BloodCooldown = scWoundCooldown;
	}

	if (mState == eState::Ragdoll) {
		return;
	}

	const float32 momentum = std::min(hit.Mass, scMaxKnockMass) * hit.Speed;

	Damage(momentum * damage_scale, hit.Body);

	AddImpulse(hit.Body, hit.Direction * momentum, hit.Point);
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

	if (root == nullptr || mState == eState::Ragdoll) {
		return;
	}

	root->SetPosition(position);
	mbTeleported = true;
}

void NPC::SetYaw(float32 yaw)
{
	Object* root = gObjectManager->GetObject(mRootID);

	if (root == nullptr || mState == eState::Ragdoll) {
		return;
	}

	root->SetRotation(Quat::FromAxisAngle(Vec3f::sUp, yaw));
	mbTeleported = true;
}

void NPC::SetIdleAnimation(const String& name, float32 speed)
{
	if (!mpSkeleton.IsValid()) {
		return;
	}

	const Animation* anim = mpSkeleton->FindAnimation(name);

	if (anim == nullptr) {
		LogWarning("NPC: could not find idle animation '{}'", name);
		return;
	}

	mpSkeleton->SetRestAnimation(anim, speed);
}

bool NPC::PlayAnimation(const String& name, eAnimationEnd on_end, float32 speed)
{
	if (!mpSkeleton.IsValid() || mState == eState::Ragdoll) {
		return false;
	}

	return mpSkeleton->PushAnimation(name, on_end, speed);
}

NPC* NPCManager::Spawn(const Object* model, const NPCDesc& desc)
{
	std::unique_ptr<NPC> npc = std::make_unique<NPC>();

	if (!npc->Create(model, desc)) {
		return nullptr;
	}

	mNPCs.push_back(std::move(npc));
	return mNPCs.back().get();
}

void NPCManager::Destroy(NPC* npc)
{
	const auto found = std::find_if(mNPCs.begin(), mNPCs.end(),
									[npc](const std::unique_ptr<NPC>& entry) { return entry.get() == npc; });

	if (found != mNPCs.end()) {
		mNPCs.erase(found);
	}
}

void NPCManager::Clear() { mNPCs.clear(); }

void NPCManager::ClearRagdolls()
{
	mNPCs.erase(std::remove_if(mNPCs.begin(), mNPCs.end(),
							   [](const std::unique_ptr<NPC>& npc) { return npc->IsRagdoll(); }),
				mNPCs.end());
}

void NPCManager::Update(float32 delta_time, bool draw_debug)
{
	gPhysics->pBackend->SetMinRagdollHitSpeed(MinImpactSpeed);

	mHits.clear();
	gPhysics->pBackend->DrainRagdollHits(mHits);

	for (const physics::RagdollHit& hit : mHits) {
		if (NPC* npc = FindByRagdollSerial(hit.RagdollSerial)) {
			npc->OnObjectHit(hit, ImpactDamageScale);
		}
	}

	for (std::unique_ptr<NPC>& npc : mNPCs) {
		npc->ReactStrength = ReactStrength;
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
