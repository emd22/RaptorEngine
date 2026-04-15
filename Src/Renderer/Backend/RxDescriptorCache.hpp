#pragma once

#include "RxDescriptors.hpp"

#include <Core/Ref.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>


namespace fx::renderer {

struct RxShaderDescriptorId;
struct RxShaderOutlineEntry;

enum class RxShaderType : uint16;

class RxDescriptorCache
{
    static constexpr uint32 scMaxSections = 6;
    static constexpr uint32 scMaxSetsPerSection = 4;


public:
    using Section = std::unordered_map<Hash32, Ref<RxDescriptorSet>, Hash32Stl>;

public:
    RxDescriptorCache() { mSections.MarkFull(); };

    /**
     * @brief Registers a new descriptor set in the cache.
     * @returns The identifier to access it by
     */
    RxShaderDescriptorId Register(uint32 set, RxShaderType shader_type,
                                  const SizedArray<RxShaderOutlineEntry>& entry_list);

    Ref<RxDescriptorSet> Request(const RxShaderDescriptorId& id);

    ~RxDescriptorCache();

public:
    StackArray<Section, scMaxSections> mSections;
};

} // namespace fx::renderer
