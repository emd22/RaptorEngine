#pragma once

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::physics {

class Ragdoll;

/**
 * @brief Something striking a standing or falling body, described by where it landed and how much momentum it carries
 */
struct RagdollKnock
{
	/// The body struck, which decides how much of the blow reaches the rest of the figure. -1 hits the whole figure
	int32 Body = -1;
	Vec3f Point = Vec3f::sZero;
	Vec3f Impulse = Vec3f::sZero;

	/// Limits on the speed the blow gives the centre of mass (m/s). The minimum is scaled down for limbs
	float32 MinSpeed = 0.0f;
	float32 MaxSpeed = 10.0f;

	/// A blow on a body still moving from the last one tops its speed back up, and adds at least this fraction of its
	/// own
	float32 RepeatFraction = 1.0f;
};

/**
 * @brief Moves the whole figure as one rigid body in response to a blow, which gives every joint a velocity it can
 * already follow.
 *
 * A figure with a foot on the ground pivots about that foot, so its centre of mass gains speed according to how high
 * the blow lands and an off-centre blow turns it. The planted feet aren't moved, and nothing has to drag them across
 * the floor. A figure in the air turns about its centre of mass instead.
 *
 * @returns The velocity the centre of mass gained
 */
Vec3f ApplyRagdollKnock(Ragdoll& ragdoll, const RagdollKnock& knock);

/**
 * @brief The struck limb's own reaction to a blow, on top of the figure's: it leads and its parents follow.
 */
void ApplyRagdollLocalHit(Ragdoll& ragdoll, uint32 body, const Vec3f impulse, const Vec3f point);

/**
 * @brief Gives every body the same velocity, for a blow big enough to take the figure off its feet outright.
 */
void LaunchRagdoll(Ragdoll& ragdoll, const Vec3f velocity);

} // namespace fx::physics
