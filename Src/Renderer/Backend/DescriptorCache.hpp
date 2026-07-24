#pragma once

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

	std::pair<Hash32, VkDescriptorSetLayout> RequestExisting(const SizedArray<DescriptorEntry>& entries);
	VkDescriptorSetLayout* RequestExisting(Hash32 descriptor_id);

	Hash32 GetID(const SizedArray<DescriptorEntry>& entries);

	void Destroy();
	~DsLayoutCache() { Destroy(); }

public:
	std::unordered_map<Hash32, VkDescriptorSetLayout, Hash32Stl> Cache;
};

class DescriptorCache
{
public:
	DescriptorCache();

	std::pair<Hash32, DescriptorSet*> Request(const SizedArray<DescriptorEntry>& entries);
	DescriptorSet* Request(Hash32 descriptor_id);

	DescriptorPool& FindPool();

	void Destroy();
	~DescriptorCache() { Destroy(); }

public:
	PagedArray<DescriptorPool> Pools;
	std::unordered_map<Hash32, DescriptorSet, Hash32Stl> Cache;
};

} // namespace renderer

} // namespace fx
