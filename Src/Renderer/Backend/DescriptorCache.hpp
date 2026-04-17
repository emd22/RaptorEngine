#pragma once

#include "Descriptors.hpp"

#include <Core/Ref.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>


namespace fx::renderer {

struct ShaderDescriptorId;
struct ShaderOutlineEntry;

enum class eShaderType : uint16;

class DescriptorCache
{
    static constexpr uint32 scMaxSections = 6;
    static constexpr uint32 scMaxSetsPerSection = 4;


public:
    using Section = std::unordered_map<Hash32, Ref<DescriptorSet>, Hash32Stl>;

public:
    DescriptorCache() { mSections.MarkFull(); };

    /**
     * @brief Registers a new descriptor set in the cache.
     * @returns The identifier to access it by
     */
    ShaderDescriptorId Register(uint32 set, ShaderType shader_type, const SizedArray<ShaderOutlineEntry>& entry_list);

    Ref<DescriptorSet> Request(const ShaderDescriptorId& id);

    ~DescriptorCache();

public:
    StackArray<Section, scMaxSections> mSections;
};

} // namespace fx::renderer
