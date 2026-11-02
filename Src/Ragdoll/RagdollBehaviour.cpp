#include "RagdollBehaviour.hpp"

#include "RagdollMath.hpp"

#include <Core/Log.hpp>
#include <Math/MathConsts.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fx::physics {

using namespace RagdollMath;

Vec3f BehaviourContext::ToBodyLocal(uint32 body, const Vec3f point) const
{
	const BehaviourBody& state = Bodies[body];
	return Vec3f::FromJolt(state.Orientation.Conjugated() * Vec3f::ToJolt(point - state.Origin));
}

Vec3f BehaviourContext::FromBodyLocal(uint32 body, const Vec3f local) const
{
	const BehaviourBody& state = Bodies[body];
	return state.Origin + Vec3f::FromJolt(state.Orientation * Vec3f::ToJolt(local));
}

Vec3f BehaviourContext::ToBodyDirection(uint32 body, const Vec3f local_direction) const
{
	return Vec3f::FromJolt(Bodies[body].Orientation * Vec3f::ToJolt(local_direction));
}

Vec3f BehaviourContext::ToLocalDirection(uint32 body, const Vec3f direction) const
{
	return Vec3f::FromJolt(Bodies[body].Orientation.Conjugated() * Vec3f::ToJolt(direction));
}

bool BehaviourContext::IsArmBody(uint32 arm, uint32 body) const
{
	const RagdollRigArm& chain = pRig->Arms[arm];
	return body == chain.Upper || body == chain.Lower || body == chain.Hand;
}

int32 BehaviourContext::FindArm(uint32 body) const
{
	for (uint32 arm = 0; arm < RagdollRig::scLimbCount; arm++) {
		if (IsArmBody(arm, body)) {
			return static_cast<int32>(arm);
		}
	}

	return -1;
}

void BehaviourOutput::Init(uint32 body_count)
{
	Pose.InitSize(body_count);
	Mask.InitSize(body_count);
	Strength.InitSize(body_count);
	Reset();
}

void BehaviourOutput::Reset()
{
	for (uint32 i = 0; i < Pose.Size; i++) {
		Pose[i] = JPH::Quat::sIdentity();
		Mask[i] = 0.0f;
		Strength[i] = 1.0f;
	}

	for (BehaviourGoal& goal : Hands) {
		goal = BehaviourGoal {};
	}

	Balance = eBehaviourBalance::Unset;
	GravityScale = 1.0f;
	bAdditive = true;
}

void BehaviourOutput::AddWorldRotation(const BehaviourContext& ctx, uint32 body, const Vec3f world_axis, float32 angle,
									   float32 mask)
{
	if (body >= Pose.Size || angle == 0.0f) {
		return;
	}

	const JPH::Vec3 local_axis = (ctx.Bodies[body].Orientation.Conjugated() * Vec3f::ToJolt(world_axis))
									 .NormalizedOr(JPH::Vec3::sAxisX());

	Pose[body] = (Pose[body] * JPH::Quat::sRotation(local_axis, angle)).Normalized();
	Mask[body] = std::max(Mask[body], mask);
}

void BehaviourOutput::ScaleStrength(uint32 body, float32 scale)
{
	if (body < Strength.Size) {
		Strength[body] *= scale;
	}
}

void BehaviourOutput::ScaleAllStrengths(float32 scale)
{
	for (float32& strength : Strength) {
		strength *= scale;
	}
}

void BehaviourOutput::ScaleArmStrengths(const RagdollRig& rig, float32 scale)
{
	for (const RagdollRigArm& arm : rig.Arms) {
		ScaleStrength(arm.Clavicle, scale);
		ScaleStrength(arm.Upper, scale);
		ScaleStrength(arm.Lower, scale);
		ScaleStrength(arm.Hand, scale);
	}
}

RagdollBehaviour::RagdollBehaviour(const char* name, float32 priority) : Priority(priority), mpName(name)
{
	AddSetting("enabled", &Enabled);
	AddSetting("priority", &Priority);
	AddSetting("fade_in", &FadeIn);
	AddSetting("fade_out", &FadeOut);
}

void RagdollBehaviour::AddSetting(const char* name, float32* value)
{
	if (mSettingCount >= scMaxSettings) {
		LogWarning(LC_PHYSICS, "Ragdoll behaviour '{}': too many settings, dropping '{}'", mpName, name);
		return;
	}

	mSettings[mSettingCount++] = BehaviourSetting { .Name = name, .pValue = value };
}

void RagdollBehaviour::Start(const BehaviourContext& ctx)
{
	if (!IsEnabled()) {
		return;
	}

	mTime = 0.0f;
	mbActive = true;

	if (FadeIn <= 0.0f) {
		mWeight = 1.0f;
	}

	OnStart(ctx);
}

void RagdollBehaviour::StartIfIdle(const BehaviourContext& ctx)
{
	if (!mbActive) {
		Start(ctx);
	}
}

void RagdollBehaviour::Stop()
{
	mbActive = false;

	if (FadeOut <= 0.0f) {
		mWeight = 0.0f;
	}
}

void RagdollBehaviour::Reset()
{
	mbActive = false;
	mWeight = 0.0f;
	mTime = 0.0f;
}

void RagdollBehaviour::Advance(float32 delta_time)
{
	if (!IsRunning()) {
		return;
	}

	mTime += delta_time;

	if (mbActive && FadeIn > 0.0f) {
		mWeight = std::min(mWeight + delta_time / FadeIn, 1.0f);
	}
	else if (mbActive) {
		mWeight = 1.0f;
	}
	else if (FadeOut > 0.0f) {
		mWeight = std::max(mWeight - delta_time / FadeOut, 0.0f);
	}
	else {
		mWeight = 0.0f;
	}
}

const BehaviourSetting* RagdollBehaviour::FindSetting(const char* name) const
{
	for (uint32 i = 0; i < mSettingCount; i++) {
		if (std::strcmp(mSettings[i].Name, name) == 0) {
			return &mSettings[i];
		}
	}

	return nullptr;
}

bool RagdollBehaviour::SetSetting(const char* name, float32 value)
{
	const BehaviourSetting* setting = FindSetting(name);

	if (setting == nullptr) {
		return false;
	}

	*setting->pValue = value;

	if (!IsEnabled()) {
		Reset();
	}

	return true;
}

bool RagdollBehaviour::GetSetting(const char* name, float32& out_value) const
{
	const BehaviourSetting* setting = FindSetting(name);

	if (setting == nullptr) {
		return false;
	}

	out_value = *setting->pValue;

	return true;
}

void RagdollBehaviour::CopySettings(const RagdollBehaviour& other)
{
	const uint32 count = std::min(mSettingCount, other.mSettingCount);

	for (uint32 i = 0; i < count; i++) {
		*mSettings[i].pValue = *other.mSettings[i].pValue;
	}
}

void RagdollBehaviour::LogSettings() const
{
	const char* activity = "idle";

	if (mbActive) {
		activity = "active";
	}

	String line = String::Fmt("{} ({}, weight {:.2f})", mpName, activity, mWeight);

	for (uint32 i = 0; i < mSettingCount; i++) {
		line += String::Fmt(" {}={:.3g}", mSettings[i].Name, *mSettings[i].pValue);
	}

	LogInfo(LC_PHYSICS, "{}", line);
}

float32 GetBehaviourNoise(float32 time, float32 seed)
{
	const float32 phase = seed * 12.9898f;

	return 0.5f * std::sin(FX_2PI * time + phase) + 0.3f * std::sin(FX_2PI * 2.17f * time + phase * 1.7f) +
		   0.2f * std::sin(FX_2PI * 3.41f * time + phase * 2.3f);
}

} // namespace fx::physics
