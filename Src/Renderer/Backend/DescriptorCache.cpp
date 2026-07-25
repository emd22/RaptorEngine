#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/Image.hpp>
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


std::pair<Hash32, VkDescriptorSetLayout>
DsLayoutCache::RequestExisting(const SizedArray<DescriptorEntry>& requested_entries)
{
	Hash32 entries_hash = GetID(requested_entries);

	auto it = Cache.find(entries_hash);

	// If the descriptor layout was not found in the cache, create it
	if (it != Cache.end()) {
		return std::make_pair(entries_hash, it->second);
	}

	DsLayoutBuilder builder {};

	for (const DescriptorEntry& entry : requested_entries) {
		builder.AddBinding(entry.Binding, entry.GetDescriptorType(), entry.ShaderStages);
	}

	Cache[entries_hash] = builder.Build();

	return std::make_pair(entries_hash, Cache[entries_hash]);
}

VkDescriptorSetLayout* DsLayoutCache::RequestExisting(Hash32 descriptor_id)
{
	auto it = Cache.find(descriptor_id);
	if (it == Cache.end()) {
		return nullptr;
	}

	return &it->second;
}

#define ID_HASH_HANDLE(handle_, size_)                                                                                 \
	HashData32(Slice<const uint8>(reinterpret_cast<const uint8*>(handle_), size_), id_result);

Hash32 DsLayoutCache::GetID(const SizedArray<DescriptorEntry>& entries)
{
	Hash32 id_result = FX_HASH32_FNV1A_INIT;

	for (const DescriptorEntry& entry : entries) {
		if (entry.Binding != 0) {
			id_result = ID_HASH_HANDLE(reinterpret_cast<const void*>(&entry.Binding), sizeof(uint32));
		}

		if (entry.IsImage()) {
			Assert(entry.pImage != nullptr);
			id_result = ID_HASH_HANDLE(reinterpret_cast<void*>(&entry.pImage->InternalImage), sizeof(uint64));
		}
		else if (entry.IsBuffer()) {
			Assert(entry.pBuffer != nullptr);
			id_result = ID_HASH_HANDLE(reinterpret_cast<void*>(&entry.pBuffer->Buffer), sizeof(uint64));
		}
	}

	return id_result;
}

void DsLayoutCache::Free(Hash32 descriptor_id)
{
	auto it = Cache.find(descriptor_id);

	// Descriptor layout not found, skip
	if (it == Cache.end()) {
		return;
	}

	vkDestroyDescriptorSetLayout(RenderBackendFwd::GetDevice()->Device, it->second, nullptr);

	Cache.erase(it);
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
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64);
		pool->Create(RenderBackendFwd::GetDevice(), 128, true);
		Pools.Insert(*pool);
	}

	return Pools[0];
}

void DescriptorCache::Free(Hash32 descriptor_id)
{
	gDsLayoutCache->Free(descriptor_id);

	auto it = Cache.find(descriptor_id);

	// Descriptor set not found, skip
	if (it == Cache.end()) {
		return;
	}

	// Free the set from the descriptor pool
	VkDescriptorSet ds = it->second.Get();
	vkFreeDescriptorSets(RenderBackendFwd::GetDevice()->Device, FindPool().Get(), 1, &ds);

	// Remove it from the cache.
	Cache.erase(it);
}

std::pair<Hash32, DescriptorSet*> DescriptorCache::Request(const SizedArray<DescriptorEntry>& entries)
{
	std::pair<Hash32, VkDescriptorSetLayout> layout_result = gDsLayoutCache->RequestExisting(entries);
	const Hash32 descriptor_id = layout_result.first;

	bool has_dynamic_offsets = false;

	for (const DescriptorEntry& entry : entries) {
		if (entry.IsBuffer()) {
			has_dynamic_offsets = true;
			break;
		}
	}

	auto it = Cache.find(descriptor_id);
	if (it != Cache.end()) {
		return std::make_pair(descriptor_id, &it->second);
	}


	DescriptorSet& descriptor = Cache[descriptor_id];

	if (!layout_result.second) {
		LogError("Could not find DS layout for ID ({})", descriptor_id);
		return std::make_pair(HashNull32, nullptr);
	}

	descriptor.Create(FindPool(), descriptor_id, layout_result.second, has_dynamic_offsets);

	for (const DescriptorEntry& entry : entries) {
		if (entry.IsBuffer()) {
			descriptor.AddBuffer(entry.Binding, entry.pBuffer, entry.BufferOffset, entry.BufferRange);
		}
		else if (entry.IsImage()) {
			descriptor.AddImage(entry.Binding, entry.pImage, entry.pSampler);
		}
	}

	descriptor.Build();

	return std::make_pair(descriptor_id, &descriptor);
}

DescriptorSet* DescriptorCache::RequestExisting(Hash32 descriptor_id)
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
