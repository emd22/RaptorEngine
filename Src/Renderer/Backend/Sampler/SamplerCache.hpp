#pragma once

#include "Sampler.hpp"
#include "SamplerProps.hpp"

#include <Core/Hash.hpp>

namespace fx {
namespace renderer {

class SamplerCache
{
public:
    SamplerCache() = default;

    Sampler* Request(const SamplerProps& props);

    ~SamplerCache() = default;

private:
    std::unordered_map<Hash32, Sampler, Hash32Stl> mCache;
};


} // namespace renderer
} // namespace fx
