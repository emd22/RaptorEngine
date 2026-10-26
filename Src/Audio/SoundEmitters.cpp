#include "SoundEmitters.hpp"

#include <Core/Random.hpp>
#include <Engine.hpp>
#include <algorithm>
#include <limits>

namespace fx {

namespace {

float32 RandomInterval(const SoundEmitter& emitter)
{
	const float32 unit = static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f);
	return emitter.MinInterval + (std::max(emitter.MaxInterval, emitter.MinInterval) - emitter.MinInterval) * unit;
}

} // namespace

SoundEmitter& SoundEmitterSet::Add(const SoundEmitter& emitter)
{
	SoundEmitter& added = mEmitters.emplace_back(emitter);

	if (added.Id == 0) {
		added.Id = NewId();
	}

	mNextId = std::max(mNextId, added.Id + 1);

	if (added.Name.IsEmpty()) {
		added.Name = NewName();
	}

	mStates.emplace_back();

	return added;
}

bool SoundEmitterSet::Remove(uint32 id)
{
	for (size_t i = 0; i < mEmitters.size(); i++) {
		if (mEmitters[i].Id != id) {
			continue;
		}

		Stop(mStates[i]);

		mEmitters.erase(mEmitters.begin() + static_cast<std::ptrdiff_t>(i));
		mStates.erase(mStates.begin() + static_cast<std::ptrdiff_t>(i));

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

	mEmitters.clear();
	mStates.clear();
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
	uint32 index = static_cast<uint32>(mEmitters.size());

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
	for (size_t i = 0; i < mEmitters.size(); i++) {
		UpdateEmitter(mEmitters[i], mStates[i], delta_time, listener);
	}
}

void SoundEmitterSet::UpdateEmitter(const SoundEmitter& emitter, State& state, float32 delta_time,
									const Vec3f listener)
{
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
		state.bPlayedOnce = true;
		should_play = true;
	}

	if (!should_play) {
		return;
	}

	state.Voice = gAudio->Play(sound, emitter.Position);
	gAudio->SetVolume(state.Voice, emitter.Volume);
}

} // namespace fx
