#pragma once

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <raptor_ffi.h>

namespace fx::physics {

/**
 * @brief A ragdoll for a humanoid skeleton. It is built and posed by Rust, from the bones of the skeleton.
 */
class Ragdoll
{
public:
	Ragdoll() = default;
	Ragdoll(const Ragdoll&) = delete;
	Ragdoll& operator=(const Ragdoll&) = delete;

	bool Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix);

	void Activate(const Vec3f& velocity);

	void Destroy();

	void WriteToSkeleton();

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsCreated() const { return mpRagdoll != nullptr; }
	FX_FORCE_INLINE uint32 GetSerial() const { return mSerial; }

	~Ragdoll() { Destroy(); }

private:
	Ref<Skeleton> mpSkeleton { nullptr };
	RxRagdoll* mpRagdoll = nullptr;
	uint32 mSerial = 0;
};

} // namespace fx::physics
