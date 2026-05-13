#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/ShaderNames.hpp>


namespace fx::renderer {

static VkDescriptorType ReflectionTypeToDescriptorType(eShaderReflectionType type)
{
    switch (type) {
    case eShaderReflectionType::StructuredBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case eShaderReflectionType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case eShaderReflectionType::Texture:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    default:;
    }

    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}


VkDescriptorSetLayout DsLayoutCache::Request(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& refl,
                                             uint32 set)
{
    SizedArray<ShaderReflectionEntry> entries_for_set(refl.Size);

    // Get the entries for the requested set index
    for (const ShaderReflectionEntry& entry : refl) {
        if (entry.Set == set) {
            entries_for_set.Insert(entry);
        }
    }

    if (entries_for_set.IsEmpty()) {
        LogInfo("SKIPPING");
        return nullptr;
    }

    Hash64 entries_hash = HashData64(Slice(entries_for_set), HashStr32(ShaderUtil::TypeToName(shader_type)));

    auto it = Cache.find(entries_hash);

    // If the descriptor layout was not found in the cache, create it
    if (it != Cache.end()) {
        return it->second;
    }

    DsLayoutBuilder builder {};

    LogInfo("DSLAYOUT CREATED");
    for (const ShaderReflectionEntry& entry : entries_for_set) {
        LogInfo("Shader: {}, Set={}, Binding={}", ShaderUtil::TypeToName(shader_type), set, entry.Binding);
        builder.AddBinding(entry.Binding, ReflectionTypeToDescriptorType(entry.Type), shader_type);
    }

    VkDescriptorSetLayout layout = builder.Build();
    Cache[entries_hash] = layout;

    return layout;
}


} // namespace fx::renderer
