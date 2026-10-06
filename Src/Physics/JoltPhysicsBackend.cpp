#include "JoltPhysicsBackend.hpp"




#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <algorithm>
#include <cstdarg>

namespace fx {
namespace physics {

JoltPhysicsBackend::JoltPhysicsBackend() {}

JoltPhysicsBackend::~JoltPhysicsBackend() { Destroy(); }

void JoltPhysicsBackend::Create()
{
	if (mbIsInited) {
		LogWarning(LC_PHYSICS, "JoltPhysicsBackend is already initialized!");
		return;
	}

	pWorld = rx_physics_new();
	mbIsInited = (pWorld != nullptr);
}

void JoltPhysicsBackend::SetMinRagdollImpactSpeed(float32 speed) { rx_physics_set_min_ragdoll_impact_speed(pWorld, speed); }

void JoltPhysicsBackend::DrainRagdollImpacts(std::vector<RagdollImpact>& out_impacts)
{
	constexpr size_t cCapacity = 64;

	RxImpact impacts[cCapacity];
	const size_t count = rx_physics_drain_impacts(pWorld, impacts, cCapacity);

	for (size_t i = 0; i < count; i++) {
		out_impacts.push_back(RagdollImpact { .Point = Vec3f(impacts[i].point[0], impacts[i].point[1], impacts[i].point[2]),
											  .Normal = Vec3f(impacts[i].normal[0], impacts[i].normal[1], impacts[i].normal[2]),
											  .Speed = impacts[i].speed,
											  .RagdollSerial = impacts[i].ragdoll_serial });
	}
}

RayResult JoltPhysicsBackend::Raycast(const Vec3f& origin, const Vec3f& direction, BodyHandle ignore_body) const
{
	RxRayResult rust_hit {};

	if (!rx_physics_raycast(pWorld, &origin.mData[0], &direction.mData[0], ignore_body.Id, &rust_hit)) {
		return RayResult { false, Vec3f::sZero };
	}

	RayResult hit { .bHit = true, .Body = BodyHandle { rust_hit.body } };
	hit.Point = Vec3f(rust_hit.point[0], rust_hit.point[1], rust_hit.point[2]);
	hit.Normal = Vec3f(rust_hit.normal[0], rust_hit.normal[1], rust_hit.normal[2]);

	return hit;
}

FLOAT4 JoltPhysicsBackend::RaycastGetFaceOfBox(BodyHandle body, const Vec3f& origin, const Vec3f& direction) const
{
	float32 face[3];
	rx_physics_raycast_face_of_box(pWorld, body.Id, &origin.mData[0], &direction.mData[0], face);

	return simd::LoadFloat4(face[0], face[1], face[2], 0.0f);
}

SizedArray<BodyHandle> JoltPhysicsBackend::RaycastObjects(const Vec3f& origin, const Vec3f& direction) const
{
	constexpr size_t cCapacity = 256;

	uint32 bodies[cCapacity];
	const size_t count = std::min(cCapacity, rx_physics_raycast_objects(pWorld, &origin.mData[0], &direction.mData[0],
																			bodies, cCapacity));

	SizedArray<BodyHandle> hits;
	hits.InitCapacity(count);

	for (size_t i = 0; i < count; i++) {
		hits.Insert(BodyHandle { bodies[i] });
	}

	return hits;
}

void JoltPhysicsBackend::Destroy()
{
	if (!mbIsInited) {
		return;
	}

	rx_physics_free(pWorld);
	pWorld = nullptr;
	mbIsInited = false;
}

} // namespace physics
} // namespace fx
