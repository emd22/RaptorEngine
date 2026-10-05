#include "DescriptorCache.hpp"

#include "Shader.hpp"

#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
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


static SizedArray<RxDsLayoutEntry> ToRustEntries(const SizedArray<DescriptorEntry>& entries)
{
	SizedArray<RxDsLayoutEntry> rust_entries(entries.Size);

	for (const DescriptorEntry& entry : entries) {
		rust_entries.Insert(RxDsLayoutEntry {
			.binding = entry.Binding,
			.descriptor_type = entry.GetDescriptorType(),
			.stages = ShaderUtil::ToUnderlyingType(entry.ShaderStages),
			.count = 1,
		});
	}

	return rust_entries;
}

DsLayoutCache::DsLayoutCache() { mpCache = rx_gpu_ds_layout_cache_create(); }

std::pair<DsLayoutID, VkDescriptorSetLayout>
DsLayoutCache::Request(const SizedArray<DescriptorEntry>& requested_entries)
{
	const SizedArray<RxDsLayoutEntry> rust_entries = ToRustEntries(requested_entries);

	uint32 id = HashNull32;
	uint64 layout = 0;

	const VkResult status = static_cast<VkResult>(
		rx_gpu_ds_layout_cache_request(mpCache, GraphicsBackendFwd::GetDevice()->GetRustDevice(), rust_entries.pData,
									   rust_entries.Size, &id, &layout));

	if (status != VK_SUCCESS) {
		LogError("Error building descriptor set layout! (status={})", Util::ResultToStr(status));
		return std::make_pair(DsLayoutID { HashNull32 }, nullptr);
	}

	return std::make_pair(DsLayoutID { id }, RxFromRaw<VkDescriptorSetLayout>(layout));
}

VkDescriptorSetLayout DsLayoutCache::RequestExisting(DsLayoutID layout_id)
{
	return RxFromRaw<VkDescriptorSetLayout>(rx_gpu_ds_layout_cache_get(mpCache, layout_id.ID));
}

DsLayoutID DsLayoutCache::GetID(const SizedArray<DescriptorEntry>& entries)
{
	const SizedArray<RxDsLayoutEntry> rust_entries = ToRustEntries(entries);

	return DsLayoutID { rx_gpu_ds_layout_id(rust_entries.pData, rust_entries.Size) };
}

void DsLayoutCache::Free(DsLayoutID layout_id)
{
	rx_gpu_ds_layout_cache_free(mpCache, GraphicsBackendFwd::GetDevice()->GetRustDevice(), layout_id.ID);
}

void DsLayoutCache::Destroy()
{
	RxGpuDevice* device = (gGraphics != nullptr) ? gGraphics->GetDevice()->GetRustDevice() : nullptr;

	rx_gpu_ds_layout_cache_destroy(mpCache, device);

	mpCache = nullptr;
}

/////////////////////////////////////
// Descriptor Cache
/////////////////////////////////////

DescriptorCache::DescriptorCache() { mpCache = rx_descriptor_cache_new(); }

std::pair<DescriptorID, DescriptorSet*> DescriptorCache::Request(const SizedArray<DescriptorEntry>& entries)
{
	std::vector<RxDescriptorEntry> rust_entries;
	rust_entries.reserve(entries.Size);

	for (const DescriptorEntry& entry : entries) {
		if (!entry.IsInvalid()) {
			rust_entries.push_back(ToRustEntry(entry));
		}
	}

	uint32 id = HashNull32;
	RxDescriptorSet* record = nullptr;

	const VkResult status = static_cast<VkResult>(
		rx_descriptor_cache_request(mpCache, gDsLayoutCache->GetRustCache(), GraphicsBackendFwd::GetDevice()->GetRustDevice(),
									rust_entries.data(), rust_entries.size(), &id, &record));

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorCache", "Could not build the descriptor set", status);
	}

	const DescriptorID descriptor_id { id };

	auto it = mSets.find(id);

	if (it == mSets.end()) {
#ifdef FX_BUILD_DEBUG
		LogInfo(LC_CORE, "** Creating descriptor set {}", descriptor_id);
#endif
		it = mSets.emplace(id, DescriptorSet(record, descriptor_id, DsLayoutID { record->layout_id })).first;
	}

	return std::make_pair(descriptor_id, &it->second);
}

DescriptorSet* DescriptorCache::Wrap(RxDescriptorSet* record)
{
	auto it = mSets.find(record->id);

	if (it == mSets.end()) {
		it = mSets
				 .emplace(record->id, DescriptorSet(record, DescriptorID { record->id }, DsLayoutID { record->layout_id }))
				 .first;
	}

	return &it->second;
}

DescriptorSet* DescriptorCache::RequestExisting(DescriptorID descriptor_id)
{
	auto it = mSets.find(descriptor_id.ID);
	if (it == mSets.end()) {
		return nullptr;
	}

	return &it->second;
}

void DescriptorCache::Free(DescriptorID id)
{
	auto it = mSets.find(id.ID);

	// Descriptor set not found, skip
	if (it == mSets.end()) {
		return;
	}

	// Note we aren't going to destroy the attached DsLayout here. This is mainly because its likely that multiple other
	// descriptor sets are using the same layout, but also the size is pretty small. There also aren't really _that_
	// many combinations for descriptor set layouts, so destroying it here wouldn't really matter.
	rx_descriptor_cache_free(mpCache, GraphicsBackendFwd::GetDevice()->GetRustDevice(), gGraphics->GpuAllocator,
							 id.ID);

	mSets.erase(it);
}

void DescriptorCache::RebuildAll()
{
	const VkResult status = static_cast<VkResult>(rx_descriptor_cache_rebuild_all(
		mpCache, gDsLayoutCache->GetRustCache(), GraphicsBackendFwd::GetDevice()->GetRustDevice()));

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorCache", "Failed to allocate descriptor set!", status);
	}
}

void DescriptorCache::Destroy()
{
	mSets.clear();

	GraphicsBackend* graphics = gGraphics;

	const bool can_free = (graphics != nullptr) && (graphics->GetDevice()->GetRustDevice() != nullptr) &&
						  (graphics->GpuAllocator != nullptr);

	rx_descriptor_cache_destroy(mpCache, can_free ? graphics->GetDevice()->GetRustDevice() : nullptr,
								can_free ? graphics->GpuAllocator : nullptr);

	mpCache = nullptr;
}

} // namespace fx::renderer
