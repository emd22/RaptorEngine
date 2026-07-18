#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/RenderBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/ShaderNames.hpp>


namespace fx::renderer {

/////////////////////////////////////
// Descriptor Layout Cache
/////////////////////////////////////

static VkDescriptorType ReflectionTypeToDescriptorType(eShaderReflectionType type)
{
	switch (type) {
	case eShaderReflectionType::StructuredBuffer:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	case eShaderReflectionType::CBuffer:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case eShaderReflectionType::Texture:
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	default:;
	}

	return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}


VkDescriptorSetLayout DsLayoutCache::Request(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& refl,
											 uint32 set_index)
{
	SizedArray<ShaderReflectionEntry> entries_for_set = GetEntriesForSet(refl, set_index);
	Hash32 entries_hash = GetID(shader_type, entries_for_set);

	auto it = Cache.find(entries_hash);

	// If the descriptor layout was not found in the cache, create it
	if (it != Cache.end()) {
		return it->second;
	}

	DsLayoutBuilder builder {};

	for (const ShaderReflectionEntry& entry : entries_for_set) {
		builder.AddBinding(entry.Binding, ReflectionTypeToDescriptorType(entry.Type), shader_type);
	}

	Cache[entries_hash] = builder.Build();

	return Cache[entries_hash];
}

VkDescriptorSetLayout* DsLayoutCache::RequestExisting(Hash32 descriptor_id)
{
	auto it = Cache.find(descriptor_id);
	if (it == Cache.end()) {
		return nullptr;
	}

	return &it->second;
}

SizedArray<ShaderReflectionEntry> DsLayoutCache::GetEntriesForSet(const SizedArray<ShaderReflectionEntry>& refl,
																  uint32 set_index)
{
	// Get only the entries that apply to the provided descriptor set index
	SizedArray<ShaderReflectionEntry> entries_for_set(refl.Size);

	// Get the entries for the requested set index
	for (const ShaderReflectionEntry& entry : refl) {
		if (entry.Set == set_index) {
			entries_for_set.Insert(entry);
		}
	}

	return entries_for_set;
}

Hash32 DsLayoutCache::GetID(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& entries_for_set)
{
	Hash32 entries_hash = HashData32(Slice(entries_for_set), HashStr32(ShaderUtil::TypeToName(shader_type)));

	return entries_hash;
}

void DsLayoutCache::Destroy()
{
	for (auto& item : Cache) {
		vkDestroyDescriptorSetLayout(RenderBackendFwd::GetDevice()->Device, item.second, nullptr);
	}

	Cache.clear();
}

/////////////////////////////////////
// Descriptor Cache
/////////////////////////////////////

DescriptorCache::DescriptorCache() { Pools.Create(2); }

DescriptorPool& DescriptorCache::FindPool()
{
	// TODO: This should check to see if there is an open entry in a pool, move to the next if not.
	if (Pools.Size() < 1) {
		DescriptorPool* pool = Pools.Insert();
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 40);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 20);
		pool->Create(RenderBackendFwd::GetDevice());

		return *pool;
	}

	return Pools[0];
}

DescriptorSet* DescriptorCache::Request(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& refl,
										uint32 set_index)
{
	SizedArray<ShaderReflectionEntry> entries_for_set = gDsLayoutCache->GetEntriesForSet(refl, set_index);
	Hash32 entries_hash = gDsLayoutCache->GetID(shader_type, entries_for_set);

	bool has_dynamic_offsets = false;

	for (const ShaderReflectionEntry& entry : entries_for_set) {
		if (entry.RequiresOffset()) {
			has_dynamic_offsets = true;
			break;
		}
	}

	auto it = Cache.find(entries_hash);
	if (it != Cache.end()) {
		return &it->second;
	}

	DescriptorSet& descriptor = Cache[entries_hash];

	VkDescriptorSetLayout* layout = gDsLayoutCache->RequestExisting(entries_hash);
	if (!layout) {
		LogError("Could not find DS layout for ID ({})", entries_hash);
		return nullptr;
	}

	descriptor.Create(FindPool(), *layout, has_dynamic_offsets);

	return &descriptor;
}

DescriptorSet* DescriptorCache::Request(Hash32 descriptor_id)
{
	auto it = Cache.find(descriptor_id);
	if (it == Cache.end()) {
		return nullptr;
	}

	return &it->second;
}

void DescriptorCache::Destroy()
{
	Pools.Destroy();
	Cache.clear();
}


} // namespace fx::renderer
