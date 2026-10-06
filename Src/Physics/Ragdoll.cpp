#include "Ragdoll.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"

#include <Color.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Util/RustInterop.hpp>
#include <vector>

namespace fx::physics {

static RxPhysicsWorld* GetWorld() { return gPhysics->pBackend->pWorld; }

bool Ragdoll::Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix)
{
	Destroy();

	if (!skeleton.IsValid() || gPhysics == nullptr || gPhysics->pBackend == nullptr) {
		return false;
	}

	static uint32 sNextSerial = 0;

	const RxLogSink log = { .user = nullptr, .log = RustInterop::Log };
	const Mat4f world_inverse = object_world_matrix.Inverse();

	const uint32 serial = ++sNextSerial;

	mpRagdoll = rx_ragdoll_new(GetWorld(), skeleton->GetRust(), &object_world_matrix.Rows[0].mData[0],
							   &world_inverse.Rows[0].mData[0], serial, rx_impacts_ragdoll_user_data(serial), &log,
							   static_cast<int32>(LC_PHYSICS));

	if (mpRagdoll == nullptr) {
		return false;
	}

	mSerial = serial;
	mpSkeleton = skeleton;

	return true;
}

void Ragdoll::Activate(const Vec3f& velocity)
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid()) {
		return;
	}

	rx_ragdoll_activate(mpRagdoll, GetWorld(), mpSkeleton->GetRust(), &velocity.mData[0]);
}

void Ragdoll::Destroy()
{
	if (mpRagdoll != nullptr) {
		rx_ragdoll_free(mpRagdoll, GetWorld(), mpSkeleton.IsValid() ? mpSkeleton->GetRust() : nullptr);
		mpRagdoll = nullptr;
	}
	else if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(false);
	}

	mpSkeleton = nullptr;
}

void Ragdoll::WriteToSkeleton()
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid()) {
		return;
	}

	rx_ragdoll_write_to_skeleton(mpRagdoll, GetWorld(), mpSkeleton->GetRust());
}

void Ragdoll::DebugDraw() const
{
	if (mpRagdoll == nullptr) {
		return;
	}

	constexpr size_t cMaxBodies = 32;

	float32 matrices[cMaxBodies * 16];
	const size_t count = std::min(cMaxBodies, rx_ragdoll_debug_boxes(mpRagdoll, GetWorld(), matrices, cMaxBodies));

	const Color color = Color::FromRGBA(255, 160, 40, 255);

	for (size_t i = 0; i < count; i++) {
		renderer::gDebugDraw->WireBox(Mat4f::FromRows(&matrices[i * 16]), color);
	}
}

} // namespace fx::physics
