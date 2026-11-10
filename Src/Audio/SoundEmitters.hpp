#pragma once

#include <Audio/AudioSystem.hpp>
#include <Core/DynArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx {

struct SoundEmitter
{
	uint32 Id = 0;
	String Name;
	String Sound;
	FVec3 Position = FVec3::sZero;
	float32 Volume = 1.0f;
	float32 MinInterval = 0.0f;
	float32 MaxInterval = 0.0f;

	bool operator==(const SoundEmitter& other) const = default;
};

class SoundEmitterSet
{
public:
	SoundEmitter& Add(const SoundEmitter& emitter);
	bool Remove(uint32 id);
	bool Replace(const SoundEmitter& emitter);
	void Clear();

	SoundEmitter* Find(uint32 id);
	const SoundEmitter* Find(uint32 id) const;

	uint32 NewId() { return mNextId++; }
	String NewName() const;

	const DynArray<SoundEmitter, GrowthFunctions::Double>& GetEmitters() const { return mEmitters; }

	void Update(float32 delta_time, const FVec3 listener);
	void StopAll();

private:
	struct State
	{
		SoundID Voice {};
		Hash32 PlayingSound = 0;
		float32 Timer = -1.0f;
		bool bPlayedOnce = false;
	};

private:
	void Stop(State& state);
	void UpdateEmitter(const SoundEmitter& emitter, State& state, float32 delta_time, const FVec3 listener);

private:
	DynArray<SoundEmitter, GrowthFunctions::Double> mEmitters;
	DynArray<State, GrowthFunctions::Double> mStates;
	uint32 mNextId = 1;
};

} // namespace fx
