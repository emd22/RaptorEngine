#pragma once

#include "Sampler.hpp"
#include "SamplerProps.hpp"

#include <raptor_ffi.h>

namespace fx {
namespace renderer {

class SamplerCache
{
public:
    SamplerCache();

    /**
     * @brief Returns the sampler for `props`, creating it on first request. The pointer stays valid until the cache is
     * destroyed.
     */
    Sampler* Request(const SamplerProps& props);

    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;

    ~SamplerCache();

private:
    RxSamplerCache* mpCache = nullptr;
};


} // namespace renderer
} // namespace fx
