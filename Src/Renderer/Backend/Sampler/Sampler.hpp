#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>
#include <raptor_ffi.h>

namespace fx::renderer {

class Sampler
{
public:
    FX_FORCE_INLINE VkSampler Get() const { return InternalSampler; }

public:
    VkSampler InternalSampler = nullptr;
};

static_assert(sizeof(Sampler) == sizeof(uint64), "Sampler must mirror the Rust SamplerEntry");

} // namespace fx::renderer
