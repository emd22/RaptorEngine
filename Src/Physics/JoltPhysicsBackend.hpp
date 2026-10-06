#pragma once

#include "BodyHandle.hpp"

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <memory>
#include <raptor_ffi.h>
#include <vector>

namespace fx {

namespace physics {

struct RagdollImpact
{
	Vec3f Point = Vec3f::sZero;
	Vec3f Normal = Vec3f::sZero;
	float32 Speed = 0.0f;
	uint32 RagdollSerial = 0;
};

struct RayResult
{
	bool bHit = false;
	Vec3f Point = Vec3f::sZero;
	/// World space normal of the surface at `Point`, facing back towards the ray
	Vec3f Normal = Vec3f::sZero;
	BodyHandle Body {};
};

/**
 * @brief The physics engine. Jolt is brought up here, and everything that is done with it is in the Rust world, which
 * reaches it through a table of functions.
 */
class JoltPhysicsBackend
{
public:
	static constexpr uint32 scMaxBodies = 512;

public:
	JoltPhysicsBackend();

	JoltPhysicsBackend(const JoltPhysicsBackend&) = delete;
	JoltPhysicsBackend& operator=(const JoltPhysicsBackend&) = delete;

	void Create();
	void Update() { rx_physics_update(pWorld); }
	void Destroy();

	void OptimizeBroadPhase() { rx_physics_optimize(pWorld); }

	RayResult Raycast(const Vec3f& origin, const Vec3f& direction, BodyHandle ignore_body = BodyHandle()) const;
	SizedArray<BodyHandle> RaycastObjects(const Vec3f& origin, const Vec3f& direction) const;

	FLOAT4 RaycastGetFaceOfBox(BodyHandle body, const Vec3f& origin, const Vec3f& direction) const;

	FX_FORCE_INLINE void SetPaused(bool paused) { rx_physics_set_paused(pWorld, paused); }

	void SetMinRagdollImpactSpeed(float32 speed);
	void DrainRagdollImpacts(std::vector<RagdollImpact>& out_impacts);

	~JoltPhysicsBackend();

public:
	/// The bodies of the world and what is done with them, in Rust
	RxPhysicsWorld* pWorld = nullptr;

private:
	bool mbIsInited = false;
};

} // namespace physics

} // namespace fx
