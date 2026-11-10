#pragma once

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx {

class PerspectiveCamera;

class BaseController
{
public:
	static constexpr float32 scDefaultMovementLerpSpeed = 10.0f;
	static constexpr float32 scDefaultSpeed = 4.3f;
	static constexpr float32 scDefaultSprintSpeed = 6.0f;

public:
	virtual ~BaseController() = default;

	virtual FVec3 Update(double delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting) = 0;

	virtual void Reset()
	{
		mVelocity = FVec3::sZero;
		mGoal = FVec3::sZero;
	}

	void SetVelocity(const FVec3 velocity) { mVelocity = velocity; }

	const FVec3 GetVelocity() const { return mVelocity; }
	const FVec3 GetGoal() const { return mGoal; }

	float32 GetMaxSpeed(bool sprinting) const;

	FX_FORCE_INLINE float32 GetGravityMultiplier() const { return GravityMultiplier; }
	FX_FORCE_INLINE void SetGravityMultiplier(const float32 multiplier) { GravityMultiplier = multiplier; }

protected:
	/**
	 * @brief Applies direction to the velocity.
	 */
	void Accelerate(const FVec3 direction, double delta_time, bool sprinting);

public:
	float32 Speed = scDefaultSpeed;
	float32 SprintSpeed = scDefaultSprintSpeed;
	float32 SpeedMultiplier = 1.0f;
	float32 MovementLerpSpeed = scDefaultMovementLerpSpeed;

	float32 GravityMultiplier = 1.0f;

protected:
	FVec3 mVelocity = FVec3::sZero;
	FVec3 mGoal = FVec3::sZero;
};

} // namespace fx
