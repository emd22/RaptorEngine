#pragma once

#include <Core/Types.hpp>
#include <raptor_ffi.h>

namespace fx::physics {

/**
 * @brief A body in the physics world, by the id the world gives it
 */
struct BodyHandle
{
	uint32 Id = RX_NO_BODY;

	FX_FORCE_INLINE bool IsValid() const { return Id != RX_NO_BODY; }
	FX_FORCE_INLINE bool operator==(const BodyHandle& other) const { return Id == other.Id; }
};

} // namespace fx::physics
