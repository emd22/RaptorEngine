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


std::pair<Hash32, VkDescriptorSetLayout> DsLayoutCache::Request(const SizedArray<DescriptorEntry>& requested_entries)
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


Hash32 DsLayoutCache::GetID(const SizedArray<DescriptorEntry>& entries)
{
	// This function is diabolical

	// Hash the handles instead of just the possibly local ptrs.

	uint8* buffer = new uint8[512];
	uint32 offset = 0;


	auto SaveHandleToBuffer = [&](const void* handle, uint32 size)
	{
		memcpy(static_cast<void*>(buffer + offset), handle, size);
		offset += size;
	};

	for (const DescriptorEntry& entry : entries) {
		// SKETCHY_COPY_VKHANDLE_TO_BUFFER(entry.BindIndex, sizeof(uint32));
		SaveHandleToBuffer(reinterpret_cast<const void*>(&entry.Binding), sizeof(uint32));

		if (entry.IsImage()) {
			SaveHandleToBuffer(reinterpret_cast<void*>(&entry.pImage->InternalImage), sizeof(uint64));
		}
		else if (entry.IsBuffer()) {
			SaveHandleToBuffer(reinterpret_cast<void*>(&entry.pBuffer->Buffer), sizeof(uint64));
		}
	}

	Hash32 entries_hash = HashData32(Slice(buffer, offset));

	delete[] buffer;

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
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
		pool->AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 20);
		pool->Create(RenderBackendFwd::GetDevice());

		return *pool;
	}

	return Pools[0];
}

std::pair<Hash32, DescriptorSet*> DescriptorCache::Request(const SizedArray<DescriptorEntry>& entries)
{
	std::pair<Hash32, VkDescriptorSetLayout> layout_result = gDsLayoutCache->Request(entries);

	bool has_dynamic_offsets = false;

	for (const DescriptorEntry& entry : entries) {
		if (entry.IsBuffer()) {
			has_dynamic_offsets = true;
			break;
		}
	}

	auto it = Cache.find(layout_result.first);
	if (it != Cache.end()) {
		return std::make_pair(layout_result.first, &it->second);
	}

	DescriptorSet& descriptor = Cache[layout_result.first];

	if (!layout_result.second) {
		LogError("Could not find DS layout for ID ({})", layout_result.first);
		return std::make_pair(HashNull32, nullptr);
	}

	descriptor.Create(FindPool(), layout_result.second, has_dynamic_offsets);

	for (const DescriptorEntry& entry : entries) {
		if (entry.IsBuffer()) {
			descriptor.AddBuffer(entry.Binding, entry.pBuffer, entry.BufferOffset, entry.BufferRange);
		}
		else if (entry.IsImage()) {
			descriptor.AddImage(entry.Binding, entry.pImage, entry.pSampler);
		}
	}

	descriptor.Build();

	return std::make_pair(layout_result.first, &descriptor);
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
