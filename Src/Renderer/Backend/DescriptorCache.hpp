#pragma once

#include "DescriptorID.hpp"
#include "Descriptors.hpp"

#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>
#include <Core/HashMap.hpp>


namespace fx {

struct ShaderReflectionEntry;
enum class eShaderType : uint16;

namespace renderer {


class DsLayoutCache
{
public:
	DsLayoutCache();

	std::pair<DsLayoutID, VkDescriptorSetLayout> Request(const SizedArray<DescriptorEntry>& entries);

	/**
	 * @brief Returns the cached layout for `layout_id`, or a null handle if there is none.
	 */
	VkDescriptorSetLayout RequestExisting(DsLayoutID layout_id);

	/**
	 * @brief Frees a descriptor set layout from the cache.
	 */
	void Free(DsLayoutID layout_id);

	DsLayoutID GetID(const SizedArray<DescriptorEntry>& entries);

	void Destroy();
	~DsLayoutCache() { Destroy(); }

	DsLayoutCache(const DsLayoutCache&) = delete;
	DsLayoutCache& operator=(const DsLayoutCache&) = delete;

private:
	RxDsLayoutCache* mpCache = nullptr;
};


class DescriptorCache
{
public:
	DescriptorCache();

	std::pair<DescriptorID, DescriptorSet*> Request(const SizedArray<DescriptorEntry>& entries);
	DescriptorSet* RequestExisting(DescriptorID descriptor_id);

	/**
	 * @brief Frees a descriptor set and its linked layout from the cache.
	 */
	void Free(DescriptorID descriptor_id);

	DescriptorID GetID(const SizedArray<DescriptorEntry>& entries);

	DescriptorPool& FindPool();

	void RebuildAll();

	void Destroy();
	~DescriptorCache() { Destroy(); }

public:
	PagedArray<DescriptorPool> Pools;
	std::unordered_map<Hash32, DescriptorSet, Hash32Stl> Cache;
};

} // namespace renderer

} // namespace fx
