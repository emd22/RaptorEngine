#include "SamplerCache.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

SamplerCache::SamplerCache() { mpCache = rx_gpu_sampler_cache_create(); }

Sampler* SamplerCache::Request(const SamplerProps& props)
{
    const RxSamplerProps rust_props = {
        .min_filter = static_cast<uint8>(props.MinFilter),
        .mag_filter = static_cast<uint8>(props.MagFilter),
        .mip_filter = static_cast<uint8>(props.MipFilter),
        .address_mode = static_cast<uint8>(props.AddressMode),
        .border_color = static_cast<uint8>(props.BorderColor),
        .compare_op = static_cast<uint8>(props.CompareOp),
        .max_anisotropy = props.MaxAnisotropy,
        .min_lod = props.MinLOD,
        .max_lod = props.MaxLOD,
    };

    const void* entry = rx_gpu_sampler_cache_request(mpCache, gGraphics->GetDevice()->GetRustDevice(), &rust_props);

    return reinterpret_cast<Sampler*>(const_cast<void*>(entry));
}

SamplerCache::~SamplerCache()
{
    RxGpuDevice* device = (gGraphics != nullptr) ? gGraphics->GetDevice()->GetRustDevice() : nullptr;

    rx_gpu_sampler_cache_free(mpCache, device);

    mpCache = nullptr;
}

} // namespace fx::renderer
