#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Core/RefUtil.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/DeferredRenderer.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

ShaderDescriptorId DescriptorCache::Register(uint32 set, eShaderType shader_type,
                                             const SizedArray<ShaderOutlineEntry>& entries)
{
    Section& section = mSections[set];

    // Hash -> shader type and outline entries
    Hash32 hash = HashData32(Slice(entries), HashStr32(ShaderUtil::TypeToName(shader_type)));

    LogInfo("");
    LogInfo("=== Registering Set {} for {} ===", set, ShaderUtil::TypeToName(shader_type));

    // Check if the DS already exists
    if (section.find(hash) != section.end()) {
        LogInfo("");
        return ShaderDescriptorId { .Set = set, .Hash = hash };
    }

    // Insert the entry into the hashmap
    Ref<DescriptorSet>& entry = section[hash];
    entry = MakeRef<DescriptorSet>();

    DescriptorPool& dp = gRenderer->pDeferredRenderer->DescriptorPool;
    DsLayoutBuilder layout_builder {};

    bool has_dynamic_offsets = false;

    for (const ShaderOutlineEntry& entry : entries) {
        using SOType = eShaderOutlineEntryType;

        if (entry.ShaderType != shader_type) {
            continue;
        }

        VkDescriptorType ds_type;

        if (entry.bUseDynamicType) {
            has_dynamic_offsets = true;
        }

        const char* binding_name = "None";

        switch (entry.Type) {
        case SOType::Sampler2D:
            ds_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding_name = "Sampler2D";
            break;
        case SOType::StructuredBuffer:
            ds_type = entry.bUseDynamicType ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                            : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding_name = "Structured Buffer";
            break;
        case SOType::UniformBuffer:
            ds_type = entry.bUseDynamicType ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                            : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding_name = "Uniform Buffer";

            break;
        }

        LogInfo("\tBinding {} => {} ({}) -> ST: {}", entry.Binding, binding_name,
                entry.bUseDynamicType ? "Dynamic" : "Normal", ShaderUtil::TypeToName(shader_type));

        layout_builder.AddBinding(entry.Binding, ds_type, shader_type);
    }

    LogInfo("");

    entry->Create(dp, layout_builder.Build(), has_dynamic_offsets);

    return ShaderDescriptorId { .Set = set, .Hash = hash, .bContainsDynamicEntry = has_dynamic_offsets };
}

Ref<DescriptorSet> DescriptorCache::Request(const ShaderDescriptorId& id)
{
    Section& section = mSections[id.Set];
    return section[id.Hash];
}


DescriptorCache::~DescriptorCache()
{
    for (uint32 section_index = 0; section_index < scMaxSections; section_index++) {
        Section& section = mSections[section_index];

        for (auto& [key, ref] : section) {
            ref->DestroyLayout();
            ref.DestroyRef();
        }
    }
}

} // namespace fx::renderer
