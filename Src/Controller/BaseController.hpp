#pragma once

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx {

class PerspectiveCamera2;

class BaseController
{
public:
	static constexpr float32 scDefaultMovementLerpSpeed = 10.0f;
	static constexpr float32 scDefaultSpeed = 4.3f;
	static constexpr float32 scDefaultSprintSpeed = 6.0f;

public:
	virtual ~BaseController() = default;

	virtual Vec3f Update(double delta_time, const Vec3f input, const PerspectiveCamera2& camera, bool sprinting) = 0;

	virtual void Reset()
	{
		mVelocity = Vec3f::sZero;
		mGoal = Vec3f::sZero;
	}

	void SetVelocity(const Vec3f velocity) { mVelocity = velocity; }

	const Vec3f GetVelocity() const { return mVelocity; }
	const Vec3f GetGoal() const { return mGoal; }

	float32 GetMaxSpeed(bool sprinting) const;

protected:
	/**
	 * @brief Applies direction to the velocity.
	 */
	void Accelerate(const Vec3f direction, double delta_time, bool sprinting);

public:
	float32 Speed = scDefaultSpeed;
	float32 SprintSpeed = scDefaultSprintSpeed;
	float32 SpeedMultiplier = 1.0f;
	float32 MovementLerpSpeed = scDefaultMovementLerpSpeed;

protected:
	Vec3f mVelocity = Vec3f::sZero;
	Vec3f mGoal = Vec3f::sZero;
};

} // namespace fx
