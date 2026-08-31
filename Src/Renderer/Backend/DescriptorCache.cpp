#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Backend/Image.hpp>
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


std::pair<DsLayoutID, VkDescriptorSetLayout>
DsLayoutCache::Request(const SizedArray<DescriptorEntry>& requested_entries)
{
	DsLayoutID entries_hash = GetID(requested_entries);

	auto it = Cache.find(entries_hash.ID);

	// If the descriptor layout was not found in the cache, create it
	if (it != Cache.end()) {
		return std::make_pair(entries_hash, it->second);
	}

	DsLayoutBuilder builder {};

	LogInfo(LC_CORE, "DS Layout Builder:");
	for (const DescriptorEntry& entry : requested_entries) {
		LogInfo("\t Entry {} -> {} || {}", entry.Binding, DescriptorEntryUtil::GetTypeName(entry.GetType()),
				ShaderUtil::TypeToName(entry.ShaderStages));
		builder.AddBinding(entry.Binding, entry.GetDescriptorType(), entry.ShaderStages);
	}

	Cache[entries_hash.ID] = builder.Build();

	return std::make_pair(entries_hash, Cache[entries_hash.ID]);
}

VkDescriptorSetLayout* DsLayoutCache::RequestExisting(DsLayoutID layout_id)
{
	auto it = Cache.find(layout_id.ID);
	if (it == Cache.end()) {
		return nullptr;
	}

	return &it->second;
}

#define ID_HASH_HANDLE(handle_, size_)                                                                                 \
	HashData32(Slice<const uint8>(reinterpret_cast<const uint8*>(handle_), size_), id_result);

DsLayoutID DsLayoutCache::GetID(const SizedArray<DescriptorEntry>& entries)
{
	Hash32 id_result = FX_HASH32_FNV1A_INIT;

	for (const DescriptorEntry& entry : entries) {
		// Add the binding number
		id_result = ID_HASH_HANDLE(reinterpret_cast<const void*>(&entry.Binding), sizeof(uint32));

		const VkDescriptorType dtype = entry.GetDescriptorType();
		id_result = ID_HASH_HANDLE(&dtype, sizeof(dtype));
	}

	return DsLayoutID { id_result };
}

void DsLayoutCache::Free(DsLayoutID layout_id)
{
	auto it = Cache.find(layout_id.ID);

	// Descriptor layout not found, skip
	if (it == Cache.end()) {
		return;
	}

	vkDestroyDescriptorSetLayout(GraphicsBackendFwd::GetDevice()->Device, it->second, nullptr);

	Cache.erase(it);
}

void DsLayoutCache::Destroy()
{
	for (auto& item : Cache) {
		vkDestroyDescriptorSetLayout(GraphicsBackendFwd::GetDevice()->Device, item.second, nullptr);
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
		pool->Create(GraphicsBackendFwd::GetDevice(), 128, true);
		Pools.Insert(*pool);
	}

	return Pools[0];
}

void DescriptorCache::Free(DescriptorID id)
{
	auto it = Cache.find(id.ID);

	// Descriptor set not found, skip
	if (it == Cache.end()) {
		return;
	}

	// Note we aren't going to destroy the attached DsLayout here. This is mainly because its likely that multiple other
	// descriptor sets are using the same layout, but also the size is pretty small. There also aren't really _that_
	// many combinations for descriptor set layouts, so destroying it here wouldn't really matter.

	// Free the set from the descriptor pool
	VkDescriptorSet ds = it->second.Get();
	vkFreeDescriptorSets(GraphicsBackendFwd::GetDevice()->Device, FindPool().Get(), 1, &ds);

	// Remove it from the cache.
	Cache.erase(it);
}

DescriptorID DescriptorCache::GetID(const SizedArray<DescriptorEntry>& entries)
{
	// The ID's for actual descriptor sets are created based on the values of the image/buffer Vulkan handles. Layouts
	// are much less granular, so they only require the descriptor entry types.

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

	return DescriptorID { id_result };
}

std::pair<DescriptorID, DescriptorSet*> DescriptorCache::Request(const SizedArray<DescriptorEntry>& entries)
{
	std::pair<DsLayoutID, VkDescriptorSetLayout> layout_result = gDsLayoutCache->Request(entries);

	const DescriptorID descriptor_id = GetID(entries);

	AssertMsg(layout_result.first.IsValid(), "Could not retrieve descriptor set layout");

	bool has_dynamic_offsets = false;

	for (const DescriptorEntry& entry : entries) {
		if (entry.IsBuffer()) {
			has_dynamic_offsets = true;
			break;
		}
	}

	auto it = Cache.find(descriptor_id.ID);

	// If the descriptor set is already created, return it
	if (it != Cache.end()) {
		return std::make_pair(descriptor_id, &it->second);
	}

	DescriptorSet& descriptor = Cache[descriptor_id.ID];
	descriptor.Create(FindPool(), descriptor_id, layout_result.first, has_dynamic_offsets);

	LogInfo(LC_CORE, "** Creating descriptor set {}", descriptor_id);
	// Build the descriptor set
	for (const DescriptorEntry& entry : entries) {
		LogInfo(LC_CORE, "\tEntry: {} -> {} || {}", entry.Binding, DescriptorEntryUtil::GetTypeName(entry.GetType()),
				ShaderUtil::TypeToName(entry.ShaderStages));

		if (entry.IsBuffer()) {
			Assert(entry.pBuffer != nullptr);
			Assert(entry.pBuffer->Buffer != VK_NULL_HANDLE);
			descriptor.AddBuffer(entry.Binding, entry.pBuffer, entry.BufferOffset, entry.BufferRange);
		}
		else if (entry.IsImage()) {
			descriptor.AddImage(entry.Binding, entry.pImage, entry.pSampler);
		}
	}

	descriptor.Build();

	return std::make_pair(descriptor_id, &descriptor);
}

DescriptorSet* DescriptorCache::RequestExisting(DescriptorID descriptor_id)
{
	auto it = Cache.find(descriptor_id.ID);
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

void DescriptorCache::RebuildAll()
{
	DescriptorPool& pool = FindPool();

	for (auto& [hash, descriptor_set] : Cache) {
		descriptor_set.Rebuild(pool);
	}
}


} // namespace fx::renderer
