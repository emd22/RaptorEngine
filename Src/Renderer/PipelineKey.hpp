#pragma once

#include "ShaderNames.hpp"
#include "Vertex.hpp"

#include <raptor_ffi.h>

#include <Core/Hash.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

/**
 * @brief The index of the pipeline into the pipeline cache. Smaller than something like ObjectID, as it is intended to
 * be just the index and no other identifying information.
 */
struct PipelineHandle
{
	static constexpr uint32 scInvalidIndex = UINT32_MAX;

public:
	FX_FORCE_INLINE bool IsValid() const { return Index != scInvalidIndex; }
	FX_FORCE_INLINE bool operator==(const PipelineHandle& other) const { return Index == other.Index; }

public:
	uint32 Index = scInvalidIndex;
};

} // namespace fx::renderer
