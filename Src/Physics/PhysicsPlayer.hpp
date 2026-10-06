#pragma once

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <raptor_ffi.h>

namespace fx::physics {

/**
 * @brief The character the player moves around in. The character and how it moves are in Rust.
 */
class PhysicsPlayer
{
public:
	static constexpr float32 scStandingHeight = 1.72f;

public:
	PhysicsPlayer() = default;

	PhysicsPlayer(const PhysicsPlayer&) = delete;
	PhysicsPlayer& operator=(const PhysicsPlayer&) = delete;

	void Create();
	void Teleport(const Vec3f& position);
	void ApplyMovement(const Vec3f& direction);

	void SetCollisionEnabled(bool value);

	void Update(float64 delta_time);

	void SetGravityDisabled(bool value);
	bool IsGravityDisabled() const { return mbGravityDisabled; }

	bool IsGrounded() const;

	Vec3f GetPosition() const;
	Vec3f GetLinearVelocity() const;

	~PhysicsPlayer();

private:
	RxCharacter* mpCharacter = nullptr;

	bool mbGravityDisabled = false;
};

} // namespace fx::physics
