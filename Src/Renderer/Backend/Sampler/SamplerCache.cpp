#include "SamplerCache.hpp"

#include "Sampler.hpp"
#include "SamplerProps.hpp"

#include <Core/Slice.hpp>

namespace fx::renderer {

Sampler* SamplerCache::Request(const SamplerProps& props)
{
    const Hash32 hash = HashObj32(props);

    auto it = mCache.find(hash);

    if (it == mCache.end()) {
        Sampler& sampler = mCache[hash];
        sampler.Create(props);

        return &sampler;
    }

    return &it->second;
}


} // namespace fx::renderer
