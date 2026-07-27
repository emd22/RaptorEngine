#pragma once

#include "DescriptorID.hpp"
#include "Descriptors.hpp"

#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>


namespace fx {

struct ShaderReflectionEntry;
enum class eShaderType : uint16;

namespace renderer {


class DsLayoutCache
{
public:
	DsLayoutCache() = default;

	std::pair<DsLayoutID, VkDescriptorSetLayout> Request(const SizedArray<DescriptorEntry>& entries);
	VkDescriptorSetLayout* RequestExisting(DsLayoutID layout_id);

	/**
	 * @brief Frees a descriptor set layout from the cache.
	 */
	void Free(DsLayoutID layout_id);

	DsLayoutID GetID(const SizedArray<DescriptorEntry>& entries);

	void Destroy();
	~DsLayoutCache() { Destroy(); }

public:
	std::unordered_map<Hash32, VkDescriptorSetLayout, Hash32Stl> Cache;
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

	void Destroy();
	~DescriptorCache() { Destroy(); }

public:
	PagedArray<DescriptorPool> Pools;
	std::unordered_map<Hash32, DescriptorSet, Hash32Stl> Cache;
};

} // namespace renderer

} // namespace fx
