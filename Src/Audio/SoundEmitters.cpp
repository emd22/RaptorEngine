#include "SoundEmitters.hpp"

#include <Core/Random.hpp>
#include <Engine.hpp>
#include <algorithm>
#include <limits>

namespace fx {

namespace {

float32 RandomInterval(const SoundEmitter& emitter)
{
	return emitter.MinInterval +
		   (std::max(emitter.MaxInterval, emitter.MinInterval) - emitter.MinInterval) * FastRandUnit();
}

} // namespace

SoundEmitter& SoundEmitterSet::Add(const SoundEmitter& emitter)
{
	SoundEmitter& added = mEmitters.Emplace(emitter);

	if (added.Id == 0) {
		added.Id = NewId();
	}

	mNextId = std::max(mNextId, added.Id + 1);

	if (added.Name.IsEmpty()) {
		added.Name = NewName();
	}

	mStates.Emplace();

	return added;
}

bool SoundEmitterSet::Remove(uint32 id)
{
	for (uint32 i = 0; i < mEmitters.Size; i++) {
		if (mEmitters[i].Id != id) {
			continue;
		}

		Stop(mStates[i]);

		mEmitters.RemoveAt(i);
		mStates.RemoveAt(i);

		return true;
	}

	return false;
}

bool SoundEmitterSet::Replace(const SoundEmitter& emitter)
{
	SoundEmitter* existing = Find(emitter.Id);

	if (existing == nullptr) {
		return false;
	}

	*existing = emitter;

	return true;
}

void SoundEmitterSet::Clear()
{
	StopAll();

	mEmitters.Clear();
	mStates.Clear();
	mNextId = 1;
}

SoundEmitter* SoundEmitterSet::Find(uint32 id)
{
	for (SoundEmitter& emitter : mEmitters) {
		if (emitter.Id == id) {
			return &emitter;
		}
	}

	return nullptr;
}

const SoundEmitter* SoundEmitterSet::Find(uint32 id) const { return const_cast<SoundEmitterSet*>(this)->Find(id); }

String SoundEmitterSet::NewName() const
{
	uint32 index = mEmitters.Size;

	while (true) {
		const String name = String::Fmt("Sound_{}", index++);

		const bool taken = std::any_of(mEmitters.begin(), mEmitters.end(),
									   [&name](const SoundEmitter& emitter) { return emitter.Name == name; });

		if (!taken) {
			return name;
		}
	}
}

void SoundEmitterSet::Stop(State& state)
{
	gAudio->Kill(state.Voice);

	state = State {};
}

void SoundEmitterSet::StopAll()
{
	for (State& state : mStates) {
		Stop(state);
	}
}

void SoundEmitterSet::Update(float32 delta_time, const Vec3f listener)
{
	for (uint32 i = 0; i < mEmitters.Size; i++) {
		UpdateEmitter(mEmitters[i], mStates[i], delta_time, listener);
	}
}

void SoundEmitterSet::UpdateEmitter(const SoundEmitter& emitter, State& state, float32 delta_time, const Vec3f listener)
{
	constexpr float32 scStartReachScale = 1.1f;
	constexpr float32 scStopReachScale = 1.25f;

	const Hash32 sound = HashStr32(emitter.Sound.CStr());

	if (state.PlayingSound != sound) {
		Stop(state);
		state.PlayingSound = sound;
	}

	const SoundDef* def = gAudio->FindDef(sound);

	if (def == nullptr) {
		return;
	}

	float32 reach = std::numeric_limits<float32>::max();

	if (def->bSpatial) {
		reach = def->MaxDistance;
	}

	const float32 distance = (emitter.Position - listener).Length();

	if (gAudio->IsPlaying(state.Voice)) {
		if (def->bLoop && distance > reach * scStopReachScale) {
			gAudio->Kill(state.Voice);
			return;
		}

		gAudio->SetPosition(state.Voice, emitter.Position);
		gAudio->SetVolume(state.Voice, emitter.Volume);
		return;
	}

	const bool in_reach = distance <= reach * scStartReachScale;
	bool should_play = false;

	if (distance > reach * scStopReachScale) {
		state.bPlayedOnce = false;
	}

	if (def->bLoop) {
		should_play = in_reach;
	}
	else if (emitter.MaxInterval > 0.0f) {
		if (state.Timer < 0.0f) {
			state.Timer = RandomInterval(emitter);
		}

		state.Timer -= delta_time;

		if (state.Timer <= 0.0f) {
			state.Timer = RandomInterval(emitter);
			should_play = in_reach;
		}
	}
	else if (!state.bPlayedOnce && in_reach) {
		should_play = true;
	}

	if (!should_play) {
		return;
	}

	state.Voice = gAudio->Play(sound, emitter.Position);
	gAudio->SetVolume(state.Voice, emitter.Volume);
	state.bPlayedOnce = gAudio->IsPlaying(state.Voice);
}

} // namespace fx
